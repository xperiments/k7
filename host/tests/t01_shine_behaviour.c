/*
 * T1 (and ticket 03): pin the encoder's real behaviour, including the two traps
 * that the research plan got wrong. If shine is ever upgraded, these assertions
 * are what tells us the specification's assumptions moved.
 */
#include "test_util.h"

#include "layer3.h"
#include "mi_encode.h"
#include "types.h" /* read-only: the vendored encoder's own struct, for sizeof */

int main(void)
{
    /* shine_check_config reports the MPEG tier. MPEG_25 is 0 and means success,
     * so validity must be tested as "not an error" (E1). */
    CHECK_EQ(shine_check_config(44100, 128), MPEG_I, "44100/128 is MPEG-I");
    CHECK_EQ(shine_check_config(48000, 192), MPEG_I, "48000/192 is MPEG-I");
    CHECK_EQ(shine_check_config(16000, 64), MPEG_II, "16000/64 is MPEG-II");
    CHECK_EQ(shine_check_config(8000, 64), MPEG_25, "8000/64 is MPEG-2.5");
    CHECK_EQ(MPEG_25, 0, "MPEG-2.5 is zero: a truthiness test would reject it");
    CHECK(shine_check_config(8000, 64) >= 0, "8000/64 is VALID even though it returns 0");
    CHECK_EQ(shine_check_config(8000, 128), -1, "8000/128 is not a valid pair");
    CHECK_EQ(shine_check_config(44101, 128), -1, "44101 Hz is not a valid rate");
    CHECK_EQ(SHINE_MAX_SAMPLES, 1152, "the encoder's largest frame");

    /* Frame granularity and the byte total, which depends on the frame count
     * alone (the padding schedule), not on the samples fed in. */
    shine_config_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.wave.channels = PCM_STEREO;
    cfg.wave.samplerate = 44100;
    shine_set_config_mpeg_defaults(&cfg.mpeg);
    cfg.mpeg.mode = JOINT_STEREO;
    cfg.mpeg.bitr = 128;
    shine_t s = shine_initialise(&cfg);
    CHECK(s != NULL, "the encoder initialises for 44100/128");
    if (!s) TEST_END();

    CHECK_EQ(shine_samples_per_pass(s), 1152, "one pass is 1152 samples per channel");

    enum { PASSES = 100 };
    static uint8_t stream[64 * 1024];
    static int16_t pcm[1152 * 2];
    size_t total = 0;
    int calls_with_output = 0, calls_null = 0;
    pcm_gen_t gen;
    pcm_gen_init(&gen, 20241016u);
    for (int p = 0; p < PASSES; p++) {
        pcm_gen_fill(&gen, pcm, 1152);
        int written = 0;
        unsigned char *b = shine_encode_buffer_interleaved(s, pcm, &written);
        if (!b) { calls_null++; continue; }
        if (written > 0) calls_with_output++;
        CHECK(written <= 512, "one pass emits one frame's worth of bytes (%d)", written);
        CHECK(total + (size_t)written <= sizeof stream, "the reference stream fits");
        if (total + (size_t)written > sizeof stream) break;
        memcpy(stream + total, b, (size_t)written);
        total += (size_t)written;
    }
    CHECK_EQ(calls_null, 0, "every pass returns a buffer");
    CHECK_EQ(calls_with_output, PASSES, "every pass writes something");
    CHECK_EQ(total, 41796, "100 passes at 44100/128 emit exactly 41796 bytes");
    size_t leftover = 0;
    long frames = mp3_count_frames(stream, total, &leftover);
    CHECK_EQ(frames, PASSES, "the concatenated output is exactly one frame per pass");
    CHECK_EQ(leftover, 0, "no stray bytes between frames");

    /* shine_flush() is not a flush (E3): it reports nothing pending, after full
     * frames, repeatedly, and on a fresh encoder too. */
    for (int i = 0; i < 3; i++) {
        int written = -1;
        unsigned char *b = shine_flush(s, &written);
        CHECK(b != NULL, "flush returns a pointer");
        CHECK_EQ(written, 0, "flush after full frames reports 0 bytes");
        CHECK_EQ((long long)total, 41796, "flush adds nothing to the stream");
    }
    shine_close(s);

    shine_config_t fresh;
    memset(&fresh, 0, sizeof fresh);
    fresh.wave.channels = PCM_STEREO;
    fresh.wave.samplerate = 44100;
    shine_set_config_mpeg_defaults(&fresh.mpeg);
    fresh.mpeg.mode = JOINT_STEREO;
    fresh.mpeg.bitr = 128;
    shine_t s2 = shine_initialise(&fresh);
    CHECK(s2 != NULL, "a second encoder initialises");
    if (s2) {
        int written = -1;
        unsigned char *b = shine_flush(s2, &written);
        CHECK(b != NULL && written == 0, "flush on a fresh encoder reports 0 bytes");
        shine_close(s2);
    }

    /* The state the specification budgets for. */
    CHECK_EQ(sizeof(shine_global_config), 95896, "sizeof(shine_global_config)");
    CHECK_EQ(mi_enc_state_bytes(), sizeof(shine_global_config), "reported state size matches");

    /* And the sizes the station format implies. */
    char err[160];
    CHECK_EQ(mi_enc_check_format(44100, 128, err, sizeof err), MPEG_I, "44100/128 accepted");
    CHECK_EQ(mi_enc_check_format(8000, 64, err, sizeof err), MPEG_25, "MPEG-2.5 accepted");
    CHECK(mi_enc_check_format(44101, 128, err, sizeof err) < 0, "44101 Hz refused");
    TEST_END();
}
