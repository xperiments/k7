idf_component_register(SRCS "app_services.c"
                       INCLUDE_DIRS "include"
                       REQUIRES http_server mini_icecast mini_files front_door app_board library library_http mcp snapshot playlist_select flash_job
                       PRIV_REQUIRES esp_timer pthread)
