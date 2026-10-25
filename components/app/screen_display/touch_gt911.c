/*
 * The Sunton 8048S050C's touch controller: a GT911 on I2C0, read as LVGL's pointer.
 *
 * Everything here is the board's: the bus and its pins, the address selected by the
 * level of INT during reset, and the cadence at which the controller is asked. The
 * view knows none of it.
 *
 * Four measured facts decide the shape of the read, and each one was a defect where
 * it was ignored (the reference project, ~/Documents/SolidGL, records them in its
 * docs/hardware.md section 2):
 *
 *  - The axes are not swapped. A raw controller point is a panel point.
 *  - The controller reports sparsely while a finger is down, and leaves the last
 *    point in the driver between reports. A read that clears the driver's point count
 *    turns one press into hundreds of taps, so this read does not clear it: the last
 *    point stays pressed until the controller reports none.
 *  - The interrupt line does not carry the touch on this board. It idles high through
 *    a sustained press, so LVGL's timer polls the controller every TOUCH_READ_MS
 *    instead of the port's event mode. The INT pin is still configured because the
 *    driver drives it during reset to select the I2C address.
 *  - The release is the absence of a point, not a coordinate. The last point is what
 *    the press/release line reports at the release.
 *
 * This is the arrangement the reference project (SolidGL,
 * components/solidgl_platform/src/solidgl_platform.c) runs on the same board.
 */
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

#include "backlight.h"
#include "touch_gt911.h"

static const char *TAG = "touch";

#define TOUCH_I2C_PORT   0
#define TOUCH_SDA_GPIO   19
#define TOUCH_SCL_GPIO   20
#define TOUCH_INT_GPIO   18
#define TOUCH_RST_GPIO   38
/* 0x5D is the address the GT911 selects when INT is low during reset; it is the
 * address this board's controller answers on. */
#define TOUCH_I2C_ADDRESS ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS

/* Two I2C transactions a read, often enough that a tap shorter than a frame is seen. */
#define TOUCH_READ_MS 10

/* How far the midpoint of two contacts has to travel before it is a two-finger swipe.
 * It is a little shorter than LVGL's 50 px one-finger threshold: two fingers are a
 * deliberate gesture and the midpoint averages them, so it moves less than either. */
#define TOUCH_TWO_FINGER_PX 30

static esp_lcd_touch_handle_t s_touch;
static touch_two_finger_cb_t s_two_finger_cb;
/* The midpoint of the first two contacts, from when they landed. A swipe is one
 * report per touch: after the callback the latch stays until the fingers lift. */
static bool s_two_down;
static bool s_two_sent;
static int32_t s_two_x, s_two_y;

/* The first press after the backlight dimmed only wakes the screen. The whole press
 * is withheld from LVGL until the finger lifts, so the wake cannot also skip a track,
 * open the cover, or slide the label: `s_wake_only` stays set for the press and is
 * cleared on the release. */
static bool s_wake_only;

void touch_gt911_set_two_finger_cb(touch_two_finger_cb_t cb)
{
    s_two_finger_cb = cb;
}

/* A two-finger swipe, from the two contacts themselves. The controller's track ids are
 * not part of it: the midpoint of the first two points is enough, and it does not care
 * which finger the controller numbers first. */
static void touch_two_finger_check(const esp_lcd_touch_point_data_t *point, uint8_t points)
{
    if (points < 2) {
        s_two_down = false;
        s_two_sent = false;
        return;
    }

    const int32_t mx = ((int32_t)point[0].x + point[1].x) / 2;
    const int32_t my = ((int32_t)point[0].y + point[1].y) / 2;
    if (!s_two_down) {
        s_two_down = true;
        s_two_sent = false;
        s_two_x = mx;
        s_two_y = my;
        return;
    }
    if (s_two_sent) return;

    const int32_t dx = mx - s_two_x;
    const int32_t dy = my - s_two_y;
    const int32_t ax = dx < 0 ? -dx : dx;
    const int32_t ay = dy < 0 ? -dy : dy;
    if (ax < TOUCH_TWO_FINGER_PX && ay < TOUCH_TWO_FINGER_PX) return;

    s_two_sent = true;
    ESP_LOGI(TAG, "two-finger swipe: midpoint moved %d,%d", (int)dx, (int)dy);
    if (s_two_finger_cb) s_two_finger_cb((int)dx, (int)dy);
}

/*
 * What LVGL reads when it asks the pointer where it is.
 *
 * Runs on the port's task, inside lv_timer_handler(), so it may call LVGL. A
 * controller read that fails is not fatal and not repeated per frame: the pointer
 * simply reports no point for that read. The transition line is the board's witness
 * that the controller answered, and the point on the release is the last one the
 * finger was at because the controller reports the release as the absence of a point.
 *
 * Two contacts are also the two-finger swipe, which the view reserves for the menu. It
 * is detected here, from the midpoint of the two points, so it does not depend on the
 * controller's track ids or on LVGL's multi-touch recognizer.
 */
static void touch_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    static lv_indev_state_t last_state = LV_INDEV_STATE_RELEASED;
    static int32_t last_x, last_y;
    static uint8_t last_points;

    data->state = LV_INDEV_STATE_RELEASED;
    bool read_ok = s_touch != NULL && esp_lcd_touch_read_data(s_touch) == ESP_OK;

    esp_lcd_touch_point_data_t point[2] = {0};
    uint8_t points = 0;
    bool have_point = read_ok && esp_lcd_touch_get_data(s_touch, point, &points, 2) == ESP_OK &&
                      points > 0;
    if (points > 2) points = 2;

    /* Temporary diagnostic: whether the controller reports one contact or two. */
    if (points != last_points) {
        last_points = points;
        ESP_LOGI(TAG, "[GDBG] contacts=%u", points);
    }

    /* A press on a dimmed screen is the wake-up, not an action. Set the latch on the
     * landing and keep it until the finger lifts; report no point for the whole press
     * so LVGL sees no click and no gesture from it. */
    bool wake_only = s_wake_only;
    if (have_point && !wake_only && backlight_is_dimmed()) {
        s_wake_only = true;
        wake_only = true;
        backlight_set_percent(CONFIG_APP_UI_BACKLIGHT_AWAKE_PCT);
        /* The press is deliberately not reported to LVGL, so LVGL will not reset its
         * own inactivity clock from it. Do it here: otherwise the screen task sees the
         * old idle time on its next pass and dims the screen straight back down. */
        lv_display_trigger_activity(lv_indev_get_display(indev));
        ESP_LOGI(TAG, "touch wakes the backlight; the press is not an action");
    }
    if (!have_point) s_wake_only = false;

    if (wake_only) {
        /* Keep the multi-touch detector out of this press too: two fingers on a dark
         * screen are a wake, not the menu gesture. */
        s_two_down = false;
        s_two_sent = false;
    } else {
        touch_two_finger_check(point, points);
    }

    if (have_point) {
        last_x = point[0].x;
        last_y = point[0].y;
        if (!wake_only) {
            data->point.x = point[0].x;
            data->point.y = point[0].y;
            data->state = LV_INDEV_STATE_PRESSED;
        }
    }

    if (data->state == last_state) return;
    last_state = data->state;
    ESP_LOGI(TAG, "pointer %s at %d,%d",
             data->state == LV_INDEV_STATE_PRESSED ? "pressed" : "released",
             (int)last_x, (int)last_y);
}

/* Release whatever was brought up before a later step refused. The driver deletes
 * its own handle; the panel IO and the I2C bus are this module's. */
static void touch_fail(esp_lcd_touch_handle_t touch, esp_lcd_panel_io_handle_t io,
                       i2c_master_bus_handle_t bus)
{
    if (touch) esp_lcd_touch_del(touch);
    if (io) esp_lcd_panel_io_del(io);
    if (bus) i2c_del_master_bus(bus);
}

esp_err_t touch_gt911_start(lv_display_t *display)
{
    if (display == NULL) return ESP_ERR_INVALID_ARG;
    if (s_touch != NULL) return ESP_ERR_INVALID_STATE;

    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = TOUCH_I2C_PORT,
        .sda_io_num = TOUCH_SDA_GPIO,
        .scl_io_num = TOUCH_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus = NULL;
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "no I2C bus on SDA %d / SCL %d: %s",
                 TOUCH_SDA_GPIO, TOUCH_SCL_GPIO, esp_err_to_name(err));
        return err;
    }

    /* The driver reads this while it selects the address, and keeps the pointer, so
     * it has to outlive the call. */
    static esp_lcd_touch_io_gt911_config_t gt911 = {0};
    gt911.dev_addr = TOUCH_I2C_ADDRESS;

    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    io_cfg.dev_addr = TOUCH_I2C_ADDRESS;

    esp_lcd_panel_io_handle_t io = NULL;
    err = esp_lcd_new_panel_io_i2c(bus, &io_cfg, &io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "no panel IO for the GT911 at 0x%02X: %s",
                 (unsigned)TOUCH_I2C_ADDRESS, esp_err_to_name(err));
        touch_fail(NULL, NULL, bus);
        return err;
    }

    const esp_lcd_touch_config_t touch_cfg = {
        /* The controller reports native panel coordinates; a rotation would have to
         * transform them, and this panel is not rotated. */
        .x_max = lv_display_get_original_horizontal_resolution(display),
        .y_max = lv_display_get_original_vertical_resolution(display),
        .rst_gpio_num = TOUCH_RST_GPIO,
        .int_gpio_num = TOUCH_INT_GPIO,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
        .driver_data = &gt911,
    };
    err = esp_lcd_touch_new_i2c_gt911(io, &touch_cfg, &s_touch);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "the GT911 did not answer at 0x%02X (SDA %d, SCL %d): %s",
                 (unsigned)TOUCH_I2C_ADDRESS, TOUCH_SDA_GPIO, TOUCH_SCL_GPIO,
                 esp_err_to_name(err));
        s_touch = NULL;
        touch_fail(NULL, io, bus);
        return err;
    }

    /* The controller is up; hand it to LVGL. The display the caller passed is the one
     * the point is in, so LVGL turns the controller's raw point into that display's
     * logical coordinates. */
    if (!lvgl_port_lock(1000)) {
        ESP_LOGE(TAG, "the LVGL port did not give its lock; the touch controller has no pointer");
        touch_fail(s_touch, io, bus);
        s_touch = NULL;
        return ESP_ERR_TIMEOUT;
    }

    lv_indev_t *indev = lv_indev_create();
    if (indev == NULL) {
        lvgl_port_unlock();
        ESP_LOGE(TAG, "no memory for the touch input device");
        touch_fail(s_touch, io, bus);
        s_touch = NULL;
        return ESP_ERR_NO_MEM;
    }
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read);
    lv_indev_set_display(indev, display);
    lv_timer_set_period(lv_indev_get_read_timer(indev), TOUCH_READ_MS);
    /* The GT911 reports sparsely: between two of its points the latched one makes
     * LVGL's movement vector zero, and the engine's default velocity gate would reset
     * the gesture sum on every such read, so a swipe across a quiet gap would never
     * add up to the 50 px threshold. With the gate at zero the sum survives the quiet
     * reads; a tap is still a tap because it never moves 50 px. */
    lv_indev_set_gesture_min_velocity(indev, 0);
    /* LVGL's double-click rule uses the scroll limit as the distance the two taps may
     * drift apart. A finger double-tap on a 96 px knob can easily move 12-20 px, so the
     * default 10 would drop the second click back to a single one; the screen has nothing
     * to scroll, so the wider limit only relaxes the double-tap test. The window is 500 ms
     * rather than the 400 default so a deliberate touch double-tap is not read as two
     * single clicks. */
    lv_indev_set_scroll_limit(indev, 20);
    lv_indev_set_double_click_time(indev, 500);
    lvgl_port_unlock();

    ESP_LOGI(TAG, "GT911 on I2C%d at 0x%02X (SDA %d, SCL %d, INT %d, RST %d) drives LVGL's pointer, "
             "one read every %d ms",
             TOUCH_I2C_PORT, (unsigned)TOUCH_I2C_ADDRESS, TOUCH_SDA_GPIO, TOUCH_SCL_GPIO,
             TOUCH_INT_GPIO, TOUCH_RST_GPIO, TOUCH_READ_MS);
    return ESP_OK;
}
