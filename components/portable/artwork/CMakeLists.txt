# The peek's two text tags are all the app still uses. artwork.c, id3.c and flac.c are
# the parked cover pipeline for the Artwork screen - the picture locator and the fit; the
# host tests (host/tests/t15_artwork.c) still build and test them. Keep them in the tree
# for when the screen comes back; do not add them to the firmware until the view has an
# image to hand them to again.
idf_component_register(SRCS "tags.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "."
                       REQUIRES audio_format)
