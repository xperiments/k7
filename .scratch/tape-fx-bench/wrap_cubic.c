/* The two variants share every symbol, so this translation unit renames them
 * and includes the original source unchanged. */
#define tape_fx_init            cubic_init
#define tape_fx_set_samplerate  cubic_set_samplerate
#define tape_fx_set_tape_type   cubic_set_tape_type
#define tape_fx_set_dolby       cubic_set_dolby
#define tape_fx_set_tape_quality cubic_set_tape_quality
#define tape_fx_set_head_quality cubic_set_head_quality
#define tape_fx_set_motor_quality cubic_set_motor_quality
#define tape_fx_process_s16     cubic_process
#include "tape_fx_v3.c"
