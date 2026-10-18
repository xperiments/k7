idf_component_register(SRCS "app_services.c"
                       INCLUDE_DIRS "include"
                       REQUIRES http_server mini_icecast mini_files app_board)
