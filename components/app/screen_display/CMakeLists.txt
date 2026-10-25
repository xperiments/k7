# The panel and touch drivers only exist on the board that has them. This is a
# build-time choice inside the screen display, not a target check: the RGB panel and
# the headless framebuffer are two adapters behind the same public interface, and the
# GT911 is the panel board's input adapter. The label cover and the Artwork level
# belong to that input too - the vertical gestures are what reach them - so a board
# without the touch adapter carries neither the cover blob nor the artwork loader.
set(screen_srcs "screen_display.c" "cassette_view.c" "cassette_tapes.c" "cassette_meter.c" "cassette_levels.c" "cassette_buttons.c" "cassette_eq.c" "cassette_style.c" "cassette_widgets.c" "screen_input.c" "lvgl_mem.c" "font_jura_36.c" "font_jura_28.c")
set(screen_reqs mini_icecast cassette artwork esp_timer esp_lcd esp_driver_gpio esp_driver_ledc)
# The hubs are drawn by cassette_view.c, so the only art left to embed belongs to the
# touch board's Levels.
set(screen_embeds "")
if(CONFIG_APP_BOARD_SUNTON_8048S050C)
    list(APPEND screen_srcs "panel_rgb.c" "touch_gt911.c" "backlight.c" "artwork_loader.c" "cassette_peek.c")
    list(APPEND screen_reqs esp_lcd_touch esp_lcd_touch_gt911 esp_driver_i2c lvgl__lvgl)
    list(APPEND screen_embeds "assets/label_cover.bin" "assets/cover.jpg")
endif()

# LVGL vendors TJpgDec but does not export its header. The artwork loader calls it
# directly: LVGL's image decoder wants a file or the memfs driver, and its band-at-a-
# time interface does not produce the single fitted buffer the screen needs. The
# Artwork backdrop is embedded as a JPEG and decoded through the same decoder at start,
# which is 45 KB of flash instead of a 768 KB raw RGB565 copy.
# CMAKE_SOURCE_DIR is the build directory during the component-requirements pass, so
# the project root is reached from this file instead.
set(lvgl_libs "${CMAKE_CURRENT_LIST_DIR}/../../../managed_components/lvgl__lvgl/src/libs")
if(CONFIG_APP_BOARD_SUNTON_8048S050C AND NOT EXISTS "${lvgl_libs}/tjpgd/tjpgd.h")
    message(FATAL_ERROR "the LVGL TJpgDec header is not where the artwork loader expects it: ${lvgl_libs}")
endif()

idf_component_register(SRCS ${screen_srcs}
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "." "${lvgl_libs}/tjpgd"
                       EMBED_FILES ${screen_embeds}
                       REQUIRES ${screen_reqs})
