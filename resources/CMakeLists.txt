idf_component_register(SRCS "mini_icecast.c" "mi_decode.c" "mi_resample.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "third_party"
                       REQUIRES lwip esp_timer)
# hot loops (resampler, FLAC/MP3 decode): speed over size
set_source_files_properties(mi_decode.c mi_resample.c PROPERTIES COMPILE_OPTIONS "-O2;-Wno-unused-function;-Wno-unused-variable;-Wno-maybe-uninitialized")
