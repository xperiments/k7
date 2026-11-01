idf_component_register(SRCS "front_door.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES http_server library mini_icecast playlist_select flash_job)
