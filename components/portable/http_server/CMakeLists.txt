idf_component_register(SRCS "http_server.c" "http_parse.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES lwip esp_timer)
