/* Does switching the effect in and out of a stream click?
 * Same material, same chunking as mini_icecast: one run never toggles (control),
 * one toggles off and on. A click shows up as a sample-to-sample jump larger than
 * anything the control run produces on the same audio. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include "tape_fx.h"

#define CH 1024
static int16_t chunk[CH * 2], scratch[CH * 2];

int main(int argc, char **argv)
{
    const char *in = argc > 1 ? argv[1] : "/tmp/src.raw";
    const int toggle_off = argc > 2 ? atoi(argv[2]) : 300;
    const int toggle_on  = argc > 3 ? atoi(argv[3]) : 450;

    for (int run = 0; run < 3; run++) {
        FILE *f = fopen(in, "rb");
        if (!f) { perror("open"); return 1; }
        static tape_fx_t fx;
        tape_fx_init(&fx, 44100.f);
        float gain = 1.f, maxd = 0;
        int maxchunk = -1, maxidx = -1;
        bool ready = true, enabled = true;
        int n = 0, prev = 0, first = 1;
        size_t got;
        while ((got = fread(chunk, 4, CH, f)) > 0) {
            if (run == 1) enabled = false;            /* control: all dry */
            else if (run == 2) {                      /* the case under test */
                if (n == toggle_off) enabled = false;
                if (n == toggle_on)  enabled = true;
            }
            float target = enabled ? 1.f : 0.f;
            if (gain == 0.f && target == 0.f) { ready = false; }
            else if (!ready) {
                memcpy(scratch, chunk, got * 4);
                tape_fx_process_s16(&fx, scratch, got, 2);
                ready = true;
            } else if (gain >= 1.f && target >= 1.f) {
                tape_fx_process_s16(&fx, chunk, got, 2);
            } else {
                memcpy(scratch, chunk, got * 4);
                tape_fx_process_s16(&fx, scratch, got, 2);
                size_t cnt = got * 2;
                float step = (target - gain) / (float)cnt;
                for (size_t i = 0; i < cnt; i++) {
                    gain += step;
                    float w = gain < 0 ? 0 : (gain > 1 ? 1 : gain);
                    float dry = chunk[i], wet = scratch[i];
                    chunk[i] = (int16_t)(dry + (wet - dry) * w);
                }
                gain = target;
            }
            for (size_t i = 0; i < got * 2; i++) {
                if (!first) {
                    float d = fabsf((float)chunk[i] - (float)prev);
                    if (d > maxd) { maxd = d; maxchunk = n; maxidx = (int)i; }
                }
                first = 0;
                prev = chunk[i];
            }
            n++;
        }
        fclose(f);
        printf("%s: max salto %.1f LSB en chunk %d muestra %d\n",
               run == 0 ? "todo wet   " : run == 1 ? "todo seco  " : "con toggles",
               maxd, maxchunk, maxidx);
    }
    return 0;
}
