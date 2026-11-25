idf_component_register(SRCS "setup.c" "setup_http.c"
                       INCLUDE_DIRS "include"
                       REQUIRES app_board wifi_record device_name flash_job http_server http_json json
                                esp_timer)
