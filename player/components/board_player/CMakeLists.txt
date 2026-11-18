# The board adapter is the *implementation* of seams whose callers live in the portable
# components: player_net calls player_board_*, player_playout calls player_output_*, and
# player_light calls player_board_light. The link order IDF derives from REQUIRES puts a
# component's dependencies before it, so with plain archives the adapter can be scanned before
# the code that needs it and its objects are never pulled in. WHOLE_ARCHIVE makes the adapter
# always part of the link, which is what a board adapter is for: every one of its objects is
# needed by definition.
idf_component_register(SRCS "board_player_esp32.c" "output_i2s.c" "light_led.c"
                       INCLUDE_DIRS "."
                       REQUIRES player nvs_flash esp_wifi esp_netif esp_event mdns mbedtls lwip
                                esp_driver_i2s esp_driver_ledc
                       WHOLE_ARCHIVE)
