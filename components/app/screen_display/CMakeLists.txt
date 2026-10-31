# The screen display is one composition with two boards behind it. Everything above the
# hardware compiles on both - the Peek, the white page's Cover, the Menu, the artwork loader,
# the Card - and asks screen_caps() whether this build has them (screen_caps.h). What is
# selected here is only the hardware: the RGB panel, its GT911, and the pin its backlight
# hangs from (with a no-op adapter on the board that has none).
set(screen_srcs "screen_display.c" "splash.c" "cassette_view.c" "cassette_tapes.c" "cassette_meter.c" "cassette_levels.c" "cassette_bars.c" "cassette_buttons.c" "cassette_eq.c" "cassette_header.c" "cassette_footer.c" "cassette_window.c" "cassette_style.c" "cassette_widgets.c" "screen_input.c" "lvgl_mem.c" "font_jura_36.c" "font_jura_28.c" "font_jura_20.c" "font_xtape_24.c" "font_xtape_36.c" "font_xtape_logo_128.c" "cassette_button.c" "cassette_pad.c" "knob_editor.c" "knob_panel.c" "chrome_entrance.c" "cassette_home.c" "cassette_rgb.c" "knob_cell.c" "cassette_card.c" "screen_caps.c" "cassette_peek.c" "cassette_cover.c" "cassette_menu.c" "artwork_loader.c")
set(screen_reqs mini_icecast cassette knob_model controls groups rgb artwork menu app_services app_board library esp_timer esp_lcd esp_driver_gpio esp_driver_ledc app_update esp_app_format)
set(screen_priv ".")
# LVGL vendors TJpgDec but does not export its header. The loader calls it directly: LVGL's
# image decoder wants a file or the memfs driver, and its band-at-a-time interface does not
# produce the single fitted buffer the cover needs. CMAKE_SOURCE_DIR is the build directory
# during the component-requirements pass, so the project root is reached from this file
# instead. The loader compiles on both boards now, so the header is on both paths.
set(lvgl_libs "${CMAKE_CURRENT_LIST_DIR}/../../../managed_components/lvgl__lvgl/src/libs")
if(NOT EXISTS "${lvgl_libs}/tjpgd/tjpgd.h")
    message(FATAL_ERROR "the LVGL TJpgDec header is not where the cover loader expects it: ${lvgl_libs}")
endif()
list(APPEND screen_priv "${lvgl_libs}/tjpgd")

# What is left of the board difference is the hardware itself: the RGB panel, its touch
# controller, and the pin its backlight hangs from. Everything above them compiles on both
# boards and asks screen_caps() what it has.
if(CONFIG_APP_BOARD_SUNTON_8048S050C)
    list(APPEND screen_srcs "panel_rgb.c" "touch_gt911.c" "backlight.c")
    list(APPEND screen_reqs esp_lcd_touch esp_lcd_touch_gt911 esp_driver_i2c lvgl__lvgl)
else()
    list(APPEND screen_srcs "backlight_none.c")
endif()

# The screen over HTTP is a diagnostic, and it is built on either board: the panel serves the
# frame LVGL draws into the panel's own buffers, the headless path serves the full frame it
# keeps for the console dump. Which of those to read is screen_display.c's question, not the
# build's. Nothing in a normal build compiles it, and the front door's route table is sized for
# the two routes it adds.
if(CONFIG_APP_UI_SCREEN_HTTP)
    list(APPEND screen_srcs "screen_frame.c")
    list(APPEND screen_reqs http_server)
endif()

idf_component_register(SRCS ${screen_srcs}
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS ${screen_priv}
                       REQUIRES ${screen_reqs}
                       EMBED_FILES "assets/header_logo.bin")
