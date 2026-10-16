/*
 * T2c (ticket 02, defect D4): extreme downsampling is refused instead of writing
 * past the resampler's kernel row.
 *
 * The fault is unreachable from configuration once the station format is
 * validated, so this test drives the constructor directly: 48000 -> 3000 and
 * 44100 -> 1000 both need more taps than the kernel row holds, and both used to
 * write past the end of a 256-element stack array.
 */
#include "test_util.h"

#include "mi_resample.h"

static void emit(const int16_t *s, size_t n, void *ctx)
{
    (void)s;
    *(size_t *)ctx += n;
}

int main(void)
{
    char err[200];

    /* The two pairs from the specification's arithmetic. */
    err[0] = 0;
    CHECK(mi_rs_new(48000, 3000, err, sizeof err) == NULL, "48000 -> 3000 is refused");
    CHECK(strstr(err, "48000") && strstr(err, "3000"), "the refusal names both rates: '%s'", err);
    CHECK(strstr(err, "taps") != NULL, "the refusal says what the limit is: '%s'", err);

    err[0] = 0;
    CHECK(mi_rs_new(44100, 1000, err, sizeof err) == NULL, "44100 -> 1000 is refused");
    CHECK(strstr(err, "44100") && strstr(err, "1000"), "the refusal names both rates: '%s'", err);

    /* A diagnostic, not an assertion or a crash: the caller can log it and carry
     * on with the next track. */
    CHECK(err[0] != 0, "a message is produced");

    /* Ordinary and moderately low station rates still work. */
    struct {
        uint32_t in, out;
    } good[] = {
        {44100, 44100}, {48000, 44100}, {32000, 44100}, {44100, 48000},
        {44100, 22050}, {44100, 8000},  {48000, 8000},  {8000, 44100},
        {44100, 12000}, {11025, 8000},
    };
    for (size_t i = 0; i < sizeof good / sizeof good[0]; i++) {
        err[0] = 0;
        mi_rs_t *r = mi_rs_new(good[i].in, good[i].out, err, sizeof err);
        CHECK(r != NULL, "%u -> %u Hz works (%s)", (unsigned)good[i].in, (unsigned)good[i].out, err);
        mi_rs_free(r);
    }

    /* And the guard is a guard, not a change of behaviour: a supported pair
     * still passes audio through end to end. */
    mi_rs_t *r = mi_rs_new(48000, 44100, err, sizeof err);
    CHECK(r != NULL, "48000 -> 44100 constructs");
    if (r) {
        static int16_t buf[1024 * 2];
        pcm_gen_t gen;
        pcm_gen_init(&gen, 5u);
        size_t produced = 0;
        for (int i = 0; i < 20; i++) {
            pcm_gen_fill(&gen, buf, 1024);
            mi_rs_process(r, buf, 1024, emit, &produced);
        }
        CHECK(produced > 0, "the resampler produced %u frames", (unsigned)produced);
        CHECK(produced < 20u * 1024u, "and fewer than it consumed (downsampling)");
        mi_rs_free(r);
    }
    TEST_END();
}
