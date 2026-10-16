/*
 * T2 for every station format, not just the recommended one.
 *
 * Two things are checked for each rate and bit rate the specification allows:
 *
 *  1. Every buffer handed to the broadcast is exactly one frame *as its own
 *     header describes it*. A header whose padding bit disagrees with the frame
 *     actually emitted is a stream no decoder can parse - ffmpeg's words are
 *     "Failed to find two consecutive MPEG audio frames" - and it is what the
 *     vendored encoder produces at formats where the exact slot count is a whole
 *     number and its double-precision arithmetic lands just below it (measured at
 *     32000 Hz / 96 kbps: 432 bytes emitted, 433 claimed).
 *  2. The concatenated output tiles into whole frames with nothing left over, and
 *     the encoder's calls are all accounted for: at most one frame is still
 *     buffered when the track ends.
 *
 * The first version of this test only ran 44100/128, which is why the fault got
 * as far as the board.
 */
#include "test_util.h"

#include <stdbool.h>

#include "mi_encode.h"

static const struct {
    uint32_t hz, kbps;
} k_formats[] = {
    {44100, 32},  {44100, 128}, {44100, 320},
    {48000, 64},  {48000, 128}, {48000, 192},
    {32000, 32},  {32000, 96},  {32000, 128},
    {22050, 64},  {24000, 48},  {16000, 32},
    {11025, 32},  {12000, 24},  {8000, 16},
};

static uint8_t stream[128 * 1024];
static size_t stream_n;
static long frames_out;
static long bad_buffers;
static long unframed;

static void collect(const uint8_t *frame, size_t bytes, void *ctx)
{
    (void)ctx;
    size_t len = mp3_frame_len(frame); /* trusts the header, padding bit included */
    if (len != bytes) {
        bad_buffers++;
        if (bad_buffers <= 3)
            printf("      %u-byte buffer whose header claims %u\n", (unsigned)bytes, (unsigned)len);
    }
    if (stream_n + bytes <= sizeof stream) {
        memcpy(stream + stream_n, frame, bytes);
        stream_n += bytes;
    } else {
        unframed++; /* did not fit: treated as a failure below */
    }
    frames_out++;
}

int main(void)
{
    char err[200];
    for (size_t f = 0; f < sizeof k_formats / sizeof k_formats[0]; f++) {
        uint32_t hz = k_formats[f].hz, kbps = k_formats[f].kbps;
        mi_enc_t *e = mi_enc_new(hz, kbps, err, sizeof err);
        CHECK(e != NULL, "%u/%u constructs: %s", (unsigned)hz, (unsigned)kbps, err);
        if (!e) continue;

        stream_n = 0;
        frames_out = bad_buffers = unframed = 0;

        /* Feed in 1024-frame chunks, the chunker's size, which never divides the
         * encoder frame at any rate. */
        size_t spf = mi_enc_samples_per_frame(e);
        pcm_gen_t gen;
        pcm_gen_init(&gen, 4242u);
        static int16_t chunk[1024 * 2];
        size_t fed = 0;
        while (fed < 40 * spf) {
            size_t n = 1024;
            if (fed + n > 40 * spf) n = 40 * spf - fed;
            pcm_gen_fill(&gen, chunk, n);
            mi_enc_feed(e, chunk, n, collect, NULL);
            fed += n;
        }
        mi_enc_flush(e, collect, NULL);

        long calls = (long)mi_enc_frames_encoded(e);
        size_t withheld = mi_enc_pending_bytes(e);
        size_t leftover = 1;
        long whole = mp3_count_frames(stream, stream_n, &leftover);

        CHECK_EQ(calls, (long)((40 * spf + spf - 1) / spf), "encoder calls at the format");
        CHECK(bad_buffers == 0, "%u/%u: every buffer is one frame as its header describes it (%ld bad)",
              (unsigned)hz, (unsigned)kbps, bad_buffers);
        CHECK(whole == frames_out, "%u/%u: the stream tiles into whole frames (%ld of %ld)",
              (unsigned)hz, (unsigned)kbps, whole, frames_out);
        CHECK(leftover == 0, "%u/%u: no stray bytes between frames (%u left over)",
              (unsigned)hz, (unsigned)kbps, (unsigned)leftover);
        CHECK(unframed == 0, "%u/%u: the whole stream fits (%ld over)", (unsigned)hz,
              (unsigned)kbps, unframed);
        CHECK(frames_out + (withheld ? 1 : 0) >= calls,
              "%u/%u: no frame was dropped (%ld emitted, %ld encoded, %u B withheld)",
              (unsigned)hz, (unsigned)kbps, frames_out, calls, (unsigned)withheld);
        mi_enc_free(e);
    }
    TEST_END();
}
