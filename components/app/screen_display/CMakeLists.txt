# The panel and touch drivers only exist on the board that has them. This is a
# build-time choice inside the screen display, not a target check: the RGB panel and
# the headless framebuffer are two adapters behind the same public interface, and the
# GT911 is the panel board's input adapter. The peek and its tags belong to that input.
#
# The Label cover and Artwork screens are gone; the Artwork cover pipeline (TJpgDec, the
# Case backdrop and the cover blob) is parked in artwork_loader.c and the assets stay in
# assets/ for when that screen comes back. It is deliberately not built here.
set(screen_srcs "screen_display.c" "cassette_view.c" "cassette_tapes.c" "cassette_meter.c" "cassette_levels.c" "cassette_buttons.c" "cassette_eq.c" "cassette_header.c" "cassette_footer.c" "cassette_window.c" "cassette_style.c" "cassette_widgets.c" "screen_input.c" "lvgl_mem.c" "font_jura_36.c" "font_jura_28.c")
set(screen_reqs mini_icecast cassette artwork esp_timer esp_lcd esp_driver_gpio esp_driver_ledc)
if(CONFIG_APP_BOARD_SUNTON_8048S050C)
    list(APPEND screen_srcs "panel_rgb.c" "touch_gt911.c" "backlight.c" "tags_loader.c" "cassette_peek.c")
    list(APPEND screen_reqs esp_lcd_touch esp_lcd_touch_gt911 esp_driver_i2c lvgl__lvgl)
endif()

idf_component_register(SRCS ${screen_srcs}
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "."
                       REQUIRES ${screen_reqs}
                       EMBED_FILES "assets/header_logo.bin")
