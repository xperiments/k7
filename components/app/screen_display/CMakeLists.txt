# The panel driver only exists on the board that has a panel. This is a build-time
# choice inside the screen display, not a target check: the RGB panel and the
# headless framebuffer are two adapters behind the same public interface.
set(screen_srcs "screen_display.c" "cassette_view.c" "lvgl_mem.c")
if(CONFIG_APP_BOARD_SUNTON_8048S050C)
    list(APPEND screen_srcs "panel_rgb.c")
endif()

idf_component_register(SRCS ${screen_srcs}
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "."
                       EMBED_FILES "assets/hub_frames.bin"
                       REQUIRES mini_icecast cassette esp_timer esp_lcd esp_driver_gpio)
