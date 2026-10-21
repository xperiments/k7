# The panel and touch drivers only exist on the board that has them. This is a
# build-time choice inside the screen display, not a target check: the RGB panel and
# the headless framebuffer are two adapters behind the same public interface, and the
# GT911 is the panel board's input adapter. The big label cover belongs to that input
# too: its two-finger gesture is what opens it, so a board without the touch adapter
# does not carry its 814 KB blob.
set(screen_srcs "screen_display.c" "cassette_view.c" "lvgl_mem.c" "font_oxanium_36.c")
set(screen_reqs mini_icecast cassette esp_timer esp_lcd esp_driver_gpio esp_driver_ledc)
set(screen_embeds "assets/hub_frames.bin")
if(CONFIG_APP_BOARD_SUNTON_8048S050C)
    list(APPEND screen_srcs "panel_rgb.c" "touch_gt911.c" "backlight.c")
    list(APPEND screen_reqs esp_lcd_touch esp_lcd_touch_gt911 esp_driver_i2c)
    list(APPEND screen_embeds "assets/label_cover.bin")
endif()

idf_component_register(SRCS ${screen_srcs}
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "."
                       EMBED_FILES ${screen_embeds}
                       REQUIRES ${screen_reqs})
