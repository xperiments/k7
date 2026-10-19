# The tape effect runs once per PCM chunk on the broadcast task, so it gets the
# same treatment as its neighbours in mini_icecast/: optimised, not the project's
# -Og default. At -O2 everything inlines; at -Og the biquads become function calls.
#
# Not placed in IRAM. Unlike shine (where IRAM is worth ~17 % of the encoder),
# this is ~1.2 KB of code executing 9 % of the time; the measurement in
# docs/research/tape-fx-output-stream.md does not justify the IRAM.
idf_component_register(SRCS "tape_fx.c"
                       INCLUDE_DIRS "include")
set_source_files_properties(tape_fx.c PROPERTIES COMPILE_OPTIONS "-O2")
