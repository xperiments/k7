idf_component_register(SRCS "mini_icecast.c" "mi_station.c" "mi_burst.c" "mi_icy.c" "mi_ring.c" "mi_encode.c" "mi_decode.c" "mi_resample.c" "mi_shuffle.c"
                       INCLUDE_DIRS "include"
                       REQUIRES audio_format library
                       PRIV_REQUIRES http_server lwip esp_timer dr_libs shine tone_eq)
# Hot loops (resampler, FLAC/MP3 decode, encoder staging): speed over size.
set_source_files_properties(mi_encode.c mini_icecast.c
                            PROPERTIES COMPILE_OPTIONS "-O2;-Wno-unused-function;-Wno-unused-variable;-Wno-maybe-uninitialized")
# The decoder's rice/LPC loops and the resampler's tap loop are the two hottest
# hand-written loops after shine; -O3 is a measured setting, not a preference.
set_source_files_properties(mi_decode.c mi_resample.c
                            PROPERTIES COMPILE_OPTIONS "-O3;-Wno-unused-function;-Wno-unused-variable;-Wno-maybe-uninitialized")
