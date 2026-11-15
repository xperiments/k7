idf_component_register(SRCS "playlist_http.c"
                       INCLUDE_DIRS "include"
                       REQUIRES json http_json http_server playlist_file playlist_select library)
