idf_component_register(SRCS "mini_files.c" "music_dir.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES audio_format http_server mini_icecast lwip esp_timer)
