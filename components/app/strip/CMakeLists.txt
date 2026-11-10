# The strip's board half: one RMT channel, one task, eight pixels. What the light *shows* - the
# channels, the brightness and the six effects - is `components/portable/rgb` and compiles on the
# host; what is here cannot, because it is a peripheral and a task.
idf_component_register(SRCS "strip.c"
                       INCLUDE_DIRS "include"
                       REQUIRES rgb mini_icecast screen_display esp_timer esp_driver_rmt led_strip)
