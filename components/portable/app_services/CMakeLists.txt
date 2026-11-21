idf_component_register(SRCS "app_services.c" "art.c"
                       INCLUDE_DIRS "include"
                       REQUIRES card_path card_use covers_http image_http http_json playlist_http snapshot_http http_server mini_icecast mini_files front_door setup share app_board library library_http mcp snapshot playlist_select flash_job label
                       PRIV_REQUIRES json esp_timer pthread)
