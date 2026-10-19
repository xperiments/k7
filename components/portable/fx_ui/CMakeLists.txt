idf_component_register(SRCS "fx_ui.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES http_server mini_icecast tape_fx esp_timer nvs_flash)
set_source_files_properties(fx_ui.c PROPERTIES COMPILE_OPTIONS "-O2")
