idf_component_register(SRCS "playlist_select.c"
                       INCLUDE_DIRS "include"
                       REQUIRES library snapshot mini_icecast
                       PRIV_REQUIRES pthread)
