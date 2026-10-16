# Vendored, unmodified: toots/shine, LGPL-2.0-only. See COPYING and
# THIRD_PARTY_NOTICES. Kept as its own component so a recipient can replace or
# patch it and relink, which is what LGPL-2.0 section 6 requires.
set(SHINE_SRCS
    src/lib/bitstream.c
    src/lib/huffman.c
    src/lib/l3bitstream.c
    src/lib/l3loop.c
    src/lib/l3mdct.c
    src/lib/l3subband.c
    src/lib/layer3.c
    src/lib/reservoir.c
    src/lib/tables.c)
idf_component_register(SRCS ${SHINE_SRCS} INCLUDE_DIRS "src/lib")
