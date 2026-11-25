idf_component_register(SRCS "player_recv.c" "player_status.c" "player_net.c" "player_ring.c" "player_mp3.c"
                            "player_stream.c" "player_playout.c" "player_tone.c"
                            "player_light.c"
                       INCLUDE_DIRS "include"
                       REQUIRES handover device_name dr_libs esp_timer espnow_stream tone_eq)
