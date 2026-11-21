idf_component_register(SRCS "snapshot_http.c"
                       INCLUDE_DIRS "include"
                       REQUIRES json http_json http_server snapshot flash_job app_board app_services)
