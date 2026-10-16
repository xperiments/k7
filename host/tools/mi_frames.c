/*
 * Reports what the decode layer sees in a track: sample rate, channels and the
 * exact number of frames. The integration test uses this to predict how many
 * encoder frames a fixture must produce (ceil(frames / 1152)), so the assertion
 * about the end-of-track flush is computed from the file rather than hard-coded.
 *
 *   mi_frames <file>
 */
#include <stdio.h>

#include "mi_decode.h"

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <file.{mp3,flac,wav}>\n", argv[0]);
        return 2;
    }
    mi_dec_t *d = mi_dec_open(argv[1]);
    if (!d) {
        fprintf(stderr, "cannot decode %s\n", argv[1]);
        return 1;
    }
    static int16_t pcm[MI_READ_FRAMES * 2];
    unsigned long long frames = 0;
    size_t n;
    while ((n = mi_dec_read(d, pcm, MI_READ_FRAMES)) > 0) frames += n;
    printf("hz=%u frames=%llu channels=2\n", (unsigned)mi_dec_hz(d), frames);
    mi_dec_close(d);
    return 0;
}
