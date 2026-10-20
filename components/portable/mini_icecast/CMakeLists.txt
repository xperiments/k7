idf_component_register(SRCS "mini_icecast.c" "mi_station.c" "mi_burst.c" "mi_icy.c" "mi_ring.c" "mi_encode.c" "mi_decode.c" "mi_resample.c"
                       INCLUDE_DIRS "include"
                       REQUIRES audio_format library
                       PRIV_REQUIRES http_server lwip esp_timer dr_libs shine)
# Hot loops (resampler, FLAC/MP3 decode, encoder staging): speed over size.
set_source_files_properties(mi_decode.c mi_resample.c mi_encode.c mini_icecast.c
                            PROPERTIES COMPILE_OPTIONS "-O2;-Wno-unused-function;-Wno-unused-variable;-Wno-maybe-uninitialized")
