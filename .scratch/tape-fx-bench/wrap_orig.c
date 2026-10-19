/* The two variants share every symbol, so this translation unit renames them
 * and includes the original source unchanged. */
#define tape_fx_init            orig_init
#define tape_fx_set_samplerate  orig_set_samplerate
#define tape_fx_set_tape_type   orig_set_tape_type
#define tape_fx_set_dolby       orig_set_dolby
#define tape_fx_set_tape_quality orig_set_tape_quality
#define tape_fx_set_head_quality orig_set_head_quality
#define tape_fx_set_motor_quality orig_set_motor_quality
#define tape_fx_process_s16     orig_process
#include "tape_fx.c"
