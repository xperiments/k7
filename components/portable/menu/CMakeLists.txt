# The Menu's model: no LVGL, no card, no board. The view hands it a Library lease and the
# broadcast's status and draws what it says (docs/specs/menu.md).
idf_component_register(SRCS "menu.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "."
                       REQUIRES cassette library mini_icecast)
