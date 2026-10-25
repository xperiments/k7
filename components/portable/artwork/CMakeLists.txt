idf_component_register(SRCS "artwork.c" "id3.c" "flac.c" "tags.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "."
                       REQUIRES audio_format)
