# The peek's two text tags and the white page's cover both come from a Track's
# container: tags.c reads the labels, artwork.c/id3.c/flac.c locate the picture and fit
# it. The loader that decodes the JPEG is the screen's, not this component's.
idf_component_register(SRCS "artwork.c" "id3.c" "flac.c" "tags.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "."
                       REQUIRES audio_format)
