idf_component_register(SRCS "appliance.c"
                       INCLUDE_DIRS "include"
                       REQUIRES app_services app_board board_esp32 screen_display mini_files mini_icecast
                                flash_job nvs_flash esp_driver_gpio)
