# The Menu's model, and the Pad's press classification beside it: no LVGL, no card, no
# board. The view hands the model a Library lease and the broadcast's status, and feeds the
# Pad raw pointer samples (docs/specs/menu.md).
idf_component_register(SRCS "menu.c" "pad.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "."
                       REQUIRES cassette library mini_icecast)
