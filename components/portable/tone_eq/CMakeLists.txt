idf_component_register(SRCS "tone_eq.c" INCLUDE_DIRS "include")
# The biquad loop runs on the broadcast task, on the PCM chunk the encoder takes.
set_source_files_properties(tone_eq.c PROPERTIES COMPILE_OPTIONS "-O2")
