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
idf_component_register(SRCS ${SHINE_SRCS} INCLUDE_DIRS "src/lib"
                       LDFRAGMENTS "linker.lf")

# Placed in IRAM as well as optimised. Measured on the board, IRAM placement alone
# is worth about 17% of the encoder's cost at every format (44100/256: 13.8 ms per
# frame against 16.7 ms), because instruction fetch from flash is a real part of
# it. The linker fragment below does that without touching a line of shine's source.
#
# Optimised deliberately. This is the one genuinely hot loop in the firmware, and
# the specification's encoder-cost figures (and the host baseline) were measured
# with -O2; the project default is -Og, which measured 33.4 ms per frame on the
# device - 128% of one core, so the broadcast starved continuously. The sources
# themselves are untouched: this is a build setting on a vendored component, not a
# modification of it.
set_source_files_properties(${SHINE_SRCS} PROPERTIES COMPILE_OPTIONS "-O3")
