idf_component_register(SRCS "mini_files.c"
                       INCLUDE_DIRS "include"
                       REQUIRES library
                       PRIV_REQUIRES audio_format http_server mini_icecast lwip esp_timer)
