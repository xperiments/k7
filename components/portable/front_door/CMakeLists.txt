idf_component_register(SRCS "front_door.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES json http_server library mini_icecast playlist_select)
