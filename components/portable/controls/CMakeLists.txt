# The Controls model: no LVGL, no board, no clock. cassette_view.c owns the pose animation
# and the paint; this owns the ladder, the Page rule and the changed-mask (ADR 0020,
# ADR 0022).
idf_component_register(SRCS "controls.c"
                       INCLUDE_DIRS "include")
