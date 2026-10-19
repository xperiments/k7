/* Offline render: raw s16le stereo in, same out, through the effect in 1024-frame
 * chunks exactly as the station does. Args: <in> <out> <head_q|-> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tape_fx.h"

#define CHUNK 1024

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: render in.raw out.raw head_q|-\n"); return 2; }
    FILE *in = fopen(argv[1], "rb"), *out = fopen(argv[2], "wb");
    if (!in || !out) { perror("open"); return 1; }

    static tape_fx_t fx;
    tape_fx_init(&fx, 44100.f);
    if (strcmp(argv[3], "-") != 0) tape_fx_set_head_quality(&fx, (float)atof(argv[3]));

    static int16_t buf[CHUNK * 2];
    size_t n;
    while ((n = fread(buf, 4, CHUNK, in)) > 0) {
        if (strcmp(argv[3], "-") != 0) tape_fx_process_s16(&fx, buf, n, 2);
        fwrite(buf, 4, n, out);
    }
    fclose(in); fclose(out);
    return 0;
}
