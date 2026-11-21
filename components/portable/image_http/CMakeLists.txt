idf_component_register(SRCS "image_http.c"
                       INCLUDE_DIRS "include"
                       REQUIRES card_path http_server esp_common esp_http_server)
