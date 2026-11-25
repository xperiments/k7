idf_component_register(SRCS "board_esp32.c"
                       INCLUDE_DIRS "."
                       REQUIRES app_board wifi_record snapshot espnow_stream label esp_driver_sdmmc esp_driver_sdspi
                                esp_driver_spi fatfs nvs_flash esp_wifi esp_netif esp_event esp_app_format esp_hw_support
                                spi_flash esp_psram mdns esp_timer mbedtls)
