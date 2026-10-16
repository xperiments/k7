/* Host test: gcc -O2 -I../include -I../third_party host_convert.c ../mi_decode.c ../mi_resample.c -lm -o host_convert
 * Usage: ./host_convert in.{mp3,flac,wav} out_hz out.raw   (raw = s16le stereo) */
#include <stdio.h>
#include <stdlib.h>
#include "mi_decode.h"
#include "mi_resample.h"

static void emit(const int16_t *s, size_t n, void *ctx) { fwrite(s, 4, n, (FILE *)ctx); }

int main(int argc, char **argv)
{
    if (argc < 4) return 2;
    mi_dec_t *d = mi_dec_open(argv[1]);
    if (!d) { fprintf(stderr, "open failed\n"); return 1; }
    FILE *o = fopen(argv[3], "wb");
    mi_rs_t *r = mi_rs_new(mi_dec_hz(d), (uint32_t)atoi(argv[2]));
    static int16_t buf[MI_READ_FRAMES * 2];
    size_t n, total = 0;
    while ((n = mi_dec_read(d, buf, MI_READ_FRAMES)) > 0) { total += n; mi_rs_process(r, buf, n, emit, o); }
    fprintf(stderr, "in: %u Hz, %zu frames\n", mi_dec_hz(d), total);
    fclose(o); mi_rs_free(r); mi_dec_close(d);
    return 0;
}
