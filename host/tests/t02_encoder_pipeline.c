/*
 * T2 and T2b (ticket 05): the encoder stage emits whole MP3 frames, never drops a
 * sample at the 1024 -> 1152 boundary, and loses nothing at the end of a track.
 *
 * The reference is a second, independent encoder run over exactly the same PCM
 * with the block size the encoder itself wants. Byte-identity with that reference
 * is a much stronger statement than "it sounds right": it says no byte was
 * duplicated, reordered or dropped anywhere in the pipeline.
 *
 * One thing to know about the encoder, measured and not assumed: shine's bit
 * writer emits whole 32-bit words and carries a frame's last few bits into the
 * next call, so a track's final frame can be a couple of bytes short of
 * complete at the moment of the flush. Those bytes are not lost - they are
 * emitted as soon as the next block is encoded, which the T2b half of this test
 * asserts. That is why the frame count is checked at the catch-up point and not
 * at the flush.
 */
#include "test_util.h"

#include <stdbool.h>

#include "mi_encode.h"
#include "layer3.h"

#define HZ 44100
#define KBPS 128
#define FRAME 1152

static uint8_t *ref;      /* reference stream from a standalone encoder */
static size_t ref_n;
static long ref_calls;

/* The reference implementation: same PCM, encoded 1152 frames at a time. */
static void build_reference(size_t total_frames, uint32_t seed)
{
    shine_config_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.wave.channels = PCM_STEREO;
    cfg.wave.samplerate = HZ;
    shine_set_config_mpeg_defaults(&cfg.mpeg);
    cfg.mpeg.mode = JOINT_STEREO;
    cfg.mpeg.bitr = KBPS;
    shine_t s = shine_initialise(&cfg);
    if (!s) {
        printf("FATAL: reference encoder did not initialise\n");
        exit(2);
    }
    size_t cap = total_frames / HZ * (KBPS * 1000 / 8) + 65536;
    ref = malloc(cap);
    ref_n = 0;
    ref_calls = 0;
    pcm_gen_t gen;
    pcm_gen_init(&gen, seed);
    static int16_t pcm[FRAME * 2];
    for (size_t done = 0; done < total_frames; done += FRAME) {
        size_t n = total_frames - done < FRAME ? total_frames - done : FRAME;
        pcm_gen_fill(&gen, pcm, n);
        if (n < FRAME) memset(pcm + 2 * n, 0, (FRAME - n) * 2 * sizeof(int16_t));
        int written = 0;
        unsigned char *b = shine_encode_buffer_interleaved(s, pcm, &written);
        ref_calls++;
        if (b && written > 0) {
            if (ref_n + (size_t)written > cap) {
                printf("FATAL: reference buffer too small\n");
                exit(2);
            }
            memcpy(ref + ref_n, b, (size_t)written);
            ref_n += (size_t)written;
        }
    }
    shine_close(s);
}

/* What the encoder stage handed to the broadcast. */
static uint8_t *out;
static size_t out_n, out_cap;
static long out_frames;
static uint64_t calls;
static int bad_frames;

static void collect(const uint8_t *frame, size_t bytes, void *ctx)
{
    (void)ctx;
    /* Every call must carry exactly one complete frame. */
    size_t len = mp3_frame_len(frame);
    if (len == 0 || len != bytes) bad_frames++;
    if (out_n + bytes > out_cap) {
        printf("FATAL: output buffer too small\n");
        exit(2);
    }
    memcpy(out + out_n, frame, bytes);
    out_n += bytes;
    out_frames++;
}

/* Feed exactly `frames` frames of the generated stream to the encoder stage, in
 * pieces of `piece` frames - the 1024-frame chunker, or an irregular pattern. */
static void feed(mi_enc_t *e, size_t frames, size_t piece, pcm_gen_t *gen)
{
    static int16_t buf[4096 * 2];
    while (frames) {
        size_t n = frames < piece ? frames : piece;
        if (n > 4096) n = 4096;
        pcm_gen_fill(gen, buf, n);
        mi_enc_feed(e, buf, n, collect, NULL);
        frames -= n;
    }
}

/* The same thing with deliberately uneven pieces, so nothing can depend on the
 * chunk size dividing the encoder frame. */
static void feed_cycled(mi_enc_t *e, size_t frames, pcm_gen_t *gen)
{
    static const size_t pieces[] = {1024, 513, 7, 999, 1152, 33, 481};
    for (size_t i = 0; frames; i++) {
        size_t piece = pieces[i % (sizeof pieces / sizeof pieces[0])];
        feed(e, frames < piece ? frames : piece, piece, gen);
        frames -= frames < piece ? frames : piece;
    }
}

static void reset_out(void)
{
    free(out);
    out_cap = ref_n + 4 * FRAME * 4 + 65536;
    out = malloc(out_cap);
    out_n = out_frames = 0;
    bad_frames = 0;
}

int main(void)
{
    /* ---- T2: more frames than one accumulator holds, 1024-frame chunks ---- */
    const size_t chunks = 2620, total = chunks * 1024;
    build_reference(total, 12345u);
    reset_out();

    char err[160];
    mi_enc_t *e = mi_enc_new(HZ, KBPS, err, sizeof err);
    CHECK(e != NULL, "encoder stage starts: %s", err);
    if (!e) TEST_END();

    pcm_gen_t gen;
    pcm_gen_init(&gen, 12345u);
    feed(e, total, 1024, &gen);

    /* Mid-stream: the left-over 1024 frames are still in the accumulator. The
     * encoder has never been handed a short block. */
    calls = mi_enc_frames_encoded(e);
    CHECK_EQ(calls, (long)(total / FRAME), "only whole blocks have been encoded so far");
    CHECK_EQ(mi_enc_buffered_frames(e), (long)(total % FRAME), "the remainder waits in the accumulator");

    mi_enc_flush(e, collect, NULL);
    calls = mi_enc_frames_encoded(e);
    size_t pending_t2 = mi_enc_pending_bytes(e);

    CHECK_EQ(calls, (long)((total + FRAME - 1) / FRAME), "encoder calls == ceil(frames/1152)");
    CHECK_EQ(bad_frames, 0, "every broadcast() call carried exactly one whole frame");
    CHECK_EQ(out_n + pending_t2, ref_n, "emitted + still-pending == the reference byte total");
    CHECK(memcmp(out, ref, out_n) == 0, "the emitted stream is byte-identical to the reference");
    CHECK(pending_t2 < MI_ENC_MAX_FRAME_BYTES, "at most one frame is left pending (%u B)",
          (unsigned)pending_t2);
    size_t leftover = 1;
    long whole = mp3_count_frames(out, out_n, &leftover);
    CHECK_EQ(whole, (long)out_frames, "every emitted call is a frame, and they tile exactly");
    CHECK_EQ(leftover, 0, "no stray bytes between emitted frames");

    /* ---- T2b: a track whose length is not a whole number of frames ---- */
    mi_enc_free(e);
    const size_t odd = 100000; /* 100000 % 1152 = 928 */
    CHECK(odd % FRAME != 0, "the track length is deliberately not a multiple of 1152");
    build_reference(odd, 777u);
    reset_out();

    e = mi_enc_new(HZ, KBPS, err, sizeof err);
    CHECK(e != NULL, "encoder stage restarts: %s", err);
    if (!e) TEST_END();

    pcm_gen_init(&gen, 777u);
    feed_cycled(e, odd, &gen); /* ends mid-block: the flush has real work to do */
    mi_enc_flush(e, collect, NULL);

    long expect_calls = (long)((odd + FRAME - 1) / FRAME);
    CHECK_EQ(mi_enc_frames_encoded(e), expect_calls, "the residual block was padded and encoded once");
    size_t pending = mi_enc_pending_bytes(e);
    CHECK_EQ(out_n + pending, ref_n, "emitted + pending == the reference (nothing dropped, nothing doubled)");
    CHECK(memcmp(out, ref, out_n) == 0, "the emitted stream matches the reference byte for byte");
    CHECK_EQ(bad_frames, 0, "every call still carried exactly one whole frame");

    /* The withheld tail is deferred, not lost: one more block of audio brings it
     * out, and track one's bytes are then all present, in order. */
    size_t emitted_before = out_n;
    feed(e, FRAME, FRAME, &gen); /* the next track's first block */
    CHECK(out_n > emitted_before, "the next block released the withheld tail");
    CHECK(out_n >= ref_n, "every byte of the track has now been emitted");
    CHECK(memcmp(out, ref, ref_n) == 0, "and it is byte-identical to the reference");
    CHECK_EQ(bad_frames, 0, "the catch-up frame is also a whole frame");

    mi_enc_free(e);
    free(out);
    free(ref);
    TEST_END();
}
