idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_driver_gpio esp_driver_sdmmc esp_driver_sdspi esp_driver_spi fatfs nvs_flash
                                esp_wifi esp_netif esp_event spi_flash esp_psram
                                http_server mini_icecast mini_files)

# main.c reaches the credentials with __has_include(), which does not appear in the
# compiler's dependency file - so without this, creating or editing
# main/wifi_credentials.h does not recompile main.c and the previously built
# placeholders are flashed instead, silently. Both properties are needed: one makes
# ninja recompile the object, the other makes CMake reconfigure when it changes.
set(SS_CREDENTIALS "${CMAKE_CURRENT_LIST_DIR}/wifi_credentials.h")
if(EXISTS "${SS_CREDENTIALS}")
    set_source_files_properties(main.c PROPERTIES OBJECT_DEPENDS "${SS_CREDENTIALS}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SS_CREDENTIALS}")
else()
    message(STATUS "main/wifi_credentials.h is absent: building with placeholder credentials")
endif()
