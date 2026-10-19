/* The two variants share every symbol, so this translation unit renames them
 * and includes the original source unchanged. */
#define tape_fx_init            poly_init
#define tape_fx_set_samplerate  poly_set_samplerate
#define tape_fx_set_tape_type   poly_set_tape_type
#define tape_fx_set_dolby       poly_set_dolby
#define tape_fx_set_tape_quality poly_set_tape_quality
#define tape_fx_set_head_quality poly_set_head_quality
#define tape_fx_set_motor_quality poly_set_motor_quality
#define tape_fx_process_s16     poly_process
#include "tape_fx_v4.c"
