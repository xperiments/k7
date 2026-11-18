idf_component_register(SRCS "player_status.c" "player_net.c" "player_ring.c" "player_mp3.c"
                            "player_http.c" "player_stream.c" "player_playout.c"
                            "player_light.c"
                       INCLUDE_DIRS "include"
                       REQUIRES handover dr_libs esp_timer)
