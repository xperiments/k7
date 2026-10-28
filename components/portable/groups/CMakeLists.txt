# The groups model: no LVGL, no board, no clock. cassette_view.c applies the mask; this owns
# which of the right bar's panels is up (ADR 0026).
idf_component_register(SRCS "groups.c"
                       INCLUDE_DIRS "include")
