# The entry point. The device's policy lives in components/app/appliance; the board
# adapter in components/app/board_esp32, selected here.
idf_component_register(SRCS "main.c"
                       REQUIRES appliance board_esp32)
