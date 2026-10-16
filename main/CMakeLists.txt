idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_driver_gpio esp_driver_sdmmc esp_driver_sdspi esp_driver_spi fatfs nvs_flash
                                esp_wifi esp_netif esp_event spi_flash esp_psram
                                http_server mini_icecast mini_files)
