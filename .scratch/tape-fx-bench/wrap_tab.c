/* The two variants share every symbol, so this translation unit renames them
 * and includes the original source unchanged. */
#define tape_fx_init            tab_init
#define tape_fx_set_samplerate  tab_set_samplerate
#define tape_fx_set_tape_type   tab_set_tape_type
#define tape_fx_set_dolby       tab_set_dolby
#define tape_fx_set_tape_quality tab_set_tape_quality
#define tape_fx_set_head_quality tab_set_head_quality
#define tape_fx_set_motor_quality tab_set_motor_quality
#define tape_fx_process_s16     tab_process
#include "tape_fx_opt.c"
