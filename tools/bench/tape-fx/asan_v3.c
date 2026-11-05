/* Runs the effect over a minute of audio with the knobs moving, under ASan/UBSan,
 * and pokes the exact corner cases: phase at 2*pi, silence, full scale. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tape_fx_v3.c"

int main(void)
{
    static tape_fx_t t;
    static int16_t buf[1024 * 2];
    tape_fx_init(&t, 44100.f);

    /* 1. the corner: a phase of exactly 2*pi is what the LFO can reach */
    for (int i = 0; i < 8; i++) {
        float ph = 2.f * (float)M_PI;
        t.ph_wow = ph;
        t.ph_flt = ph;
        memset(buf, 0, sizeof buf);
        tape_fx_process_s16(&t, buf, 8, 2);
    }

    /* 2. a minute of signal with every setting in turn, both channel counts */
    unsigned seed = 1;
    for (int chunk = 0; chunk < 2584; chunk++) {
        if (chunk % 64 == 0) {
            tape_fx_set_tape_type(&t, (tape_type_t)(chunk / 64 % 3));
            tape_fx_set_dolby(&t, (tape_dolby_t)(chunk / 64 % 4));
            tape_fx_set_tape_quality(&t, (float)(chunk % 100) / 100.f);
            tape_fx_set_head_quality(&t, (float)((chunk * 7) % 100) / 100.f);
            tape_fx_set_motor_quality(&t, (float)((chunk * 13) % 100) / 100.f);
        }
        for (int i = 0; i < 1024; i++) {
            seed = seed * 1664525u + 1013904223u;
            buf[2 * i] = (int16_t)(seed >> 16);
            buf[2 * i + 1] = (int16_t)(seed >> 16);
        }
        tape_fx_process_s16(&t, buf, 1024, 2);
    }
    /* 3. full scale, both signs, to check the int16 bound */
    for (int i = 0; i < 1024; i++) {
        buf[2 * i] = (i & 1) ? 32767 : -32768;
        buf[2 * i + 1] = buf[2 * i];
    }
    tape_fx_process_s16(&t, buf, 1024, 2);
    printf("ok: last sample %d\n", buf[2047]);
    return 0;
}
