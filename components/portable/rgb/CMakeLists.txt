# The strip's colour model and effects: no LVGL, no board, no clock. The RGB group's panel
# publishes into it (ADR 0026).
idf_component_register(SRCS "rgb.c"
                       INCLUDE_DIRS "include")
