# The panel driver only exists on the board that has a panel. This is a build-time
# choice in the application, not a target check in a component: the components do not
# know which board they are on (H2).
set(app_srcs "main.c" "ui.c" "ui_cassette.c" "board_esp32.c")
if(CONFIG_APP_BOARD_SUNTON_8048S050C)
    list(APPEND app_srcs "panel_rgb.c")
endif()

idf_component_register(SRCS ${app_srcs}
                       INCLUDE_DIRS "."
                       EMBED_FILES "assets/hub_frames.bin"
                       REQUIRES esp_driver_gpio esp_driver_sdmmc esp_driver_sdspi esp_driver_spi fatfs nvs_flash
                                esp_wifi esp_netif esp_event spi_flash esp_psram esp_timer esp_lcd
                                http_server mini_icecast mini_files cassette app_services)

# board_esp32.c reaches the credentials with __has_include(), which does not appear in
# the compiler's dependency file - so without this, creating or editing
# main/wifi_credentials.h does not recompile board_esp32.c and the previously built
# placeholders are flashed instead, silently. Both properties are needed: one makes
# ninja recompile the object, the other makes CMake reconfigure when it changes.
set(SS_CREDENTIALS "${CMAKE_CURRENT_LIST_DIR}/wifi_credentials.h")
if(EXISTS "${SS_CREDENTIALS}")
    set_source_files_properties(board_esp32.c PROPERTIES OBJECT_DEPENDS "${SS_CREDENTIALS}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SS_CREDENTIALS}")
else()
    message(STATUS "main/wifi_credentials.h is absent: building with placeholder credentials")
endif()
