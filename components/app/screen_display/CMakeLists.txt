# The panel and touch drivers only exist on the board that has them. This is a
# build-time choice inside the screen display, not a target check: the RGB panel and
# the headless framebuffer are two adapters behind the same public interface, and the
# GT911 is the panel board's input adapter. The peek, the white page's cover and the
# Track loader belong to that input.
#
# The cover loader (TJpgDec and the 603x385 cover buffer) is the board's: the white page
# and the peek are the touch board's overlays, and the headless board has neither. The
# old Artwork screen's Case backdrop is not part of this build.
set(screen_srcs "screen_display.c" "cassette_view.c" "cassette_tapes.c" "cassette_meter.c" "cassette_levels.c" "cassette_buttons.c" "cassette_eq.c" "cassette_header.c" "cassette_footer.c" "cassette_window.c" "cassette_style.c" "cassette_widgets.c" "screen_input.c" "lvgl_mem.c" "font_jura_36.c" "font_jura_28.c" "font_jura_20.c" "font_xtape_24.c" "font_xtape_36.c")
set(screen_reqs mini_icecast cassette artwork menu app_services app_board library esp_timer esp_lcd esp_driver_gpio esp_driver_ledc app_update esp_app_format)
set(screen_priv ".")
if(CONFIG_APP_BOARD_SUNTON_8048S050C)
    list(APPEND screen_srcs "panel_rgb.c" "touch_gt911.c" "backlight.c" "artwork_loader.c" "cassette_peek.c" "cassette_cover.c" "cassette_menu.c")
    list(APPEND screen_reqs esp_lcd_touch esp_lcd_touch_gt911 esp_driver_i2c lvgl__lvgl)

    # LVGL vendors TJpgDec but does not export its header. The loader calls it directly:
    # LVGL's image decoder wants a file or the memfs driver, and its band-at-a-time
    # interface does not produce the single fitted buffer the cover needs. CMAKE_SOURCE_DIR
    # is the build directory during the component-requirements pass, so the project root
    # is reached from this file instead.
    set(lvgl_libs "${CMAKE_CURRENT_LIST_DIR}/../../../managed_components/lvgl__lvgl/src/libs")
    if(NOT EXISTS "${lvgl_libs}/tjpgd/tjpgd.h")
        message(FATAL_ERROR "the LVGL TJpgDec header is not where the cover loader expects it: ${lvgl_libs}")
    endif()
    list(APPEND screen_priv "${lvgl_libs}/tjpgd")
endif()

idf_component_register(SRCS ${screen_srcs}
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS ${screen_priv}
                       REQUIRES ${screen_reqs}
                       EMBED_FILES "assets/header_logo.bin")
