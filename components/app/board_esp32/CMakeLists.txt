idf_component_register(SRCS "board_esp32.c"
                       INCLUDE_DIRS "."
                       REQUIRES app_board snapshot esp_driver_sdmmc esp_driver_sdspi esp_driver_spi fatfs
                                esp_wifi esp_netif esp_event spi_flash esp_psram mdns spiffs esp_timer)

# The credentials are reached with __has_include(), which does not appear in the
# compiler's dependency file - so without this, creating or editing
# wifi_credentials.h does not recompile board_esp32.c and the previously built
# placeholders are flashed instead, silently. Both properties are needed: one makes
# ninja recompile the object, the other makes CMake reconfigure when it changes.
set(SS_CREDENTIALS "${CMAKE_CURRENT_LIST_DIR}/wifi_credentials.h")
if(EXISTS "${SS_CREDENTIALS}")
    set_source_files_properties(board_esp32.c PROPERTIES OBJECT_DEPENDS "${SS_CREDENTIALS}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SS_CREDENTIALS}")
else()
    message(STATUS "components/app/board_esp32/wifi_credentials.h is absent: building with placeholder credentials")
endif()
