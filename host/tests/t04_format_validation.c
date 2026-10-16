/*
 * T12 and ticket 04: the station format is validated before anything is served.
 *
 * The single most important assertion here is that the *lowest* MPEG tier
 * returns zero and is still valid. A truthiness test would reject every MPEG-2.5
 * configuration while passing every test at the recommended 44.1 kHz default, so
 * getting this wrong is invisible until somebody picks an 8 kHz station.
 */
#include "test_util.h"

#include "mi_encode.h"
#include "mini_icecast.h"

int main(void)
{
    char err[200];

    /* The recommended default, and the neighbouring combinations. */
    err[0] = 0;
    CHECK_EQ(mi_enc_check_format(44100, 128, err, sizeof err), 3, "44100/128 is MPEG-I (3)");
    CHECK_EQ(mi_enc_check_format(48000, 192, err, sizeof err), 3, "48000/192 is MPEG-I");
    CHECK_EQ(mi_enc_check_format(32000, 96, err, sizeof err), 3, "32000/96 is MPEG-I");
    CHECK_EQ(mi_enc_check_format(22050, 64, err, sizeof err), 2, "22050/64 is MPEG-II");
    CHECK_EQ(mi_enc_check_format(16000, 64, err, sizeof err), 2, "16000/64 is MPEG-II");
    CHECK_EQ(mi_enc_check_format(8000, 64, err, sizeof err), 0, "8000/64 is MPEG-2.5, i.e. zero");
    CHECK(strcmp(mi_enc_mpeg_name(0), "MPEG-2.5") == 0, "zero is named, not treated as failure");

    /* A rate the encoder does not know is refused, and the message names it. */
    err[0] = 0;
    CHECK(mi_enc_check_format(44101, 128, err, sizeof err) < 0, "44101 Hz is refused");
    CHECK(strstr(err, "44101") != NULL, "the refusal names the rate: '%s'", err);
    err[0] = 0;
    CHECK(mi_enc_check_format(11000, 128, err, sizeof err) < 0, "11000 Hz is refused");
    CHECK(mi_enc_check_format(0, 128, err, sizeof err) < 0, "0 Hz is refused");
    CHECK(mi_enc_check_format(96000, 128, err, sizeof err) < 0, "96000 Hz is refused");

    /* A plausible-looking rate/bit-rate pair that the encoder cannot represent
     * is refused, naming both values. */
    err[0] = 0;
    CHECK(mi_enc_check_format(8000, 128, err, sizeof err) < 0, "8000/128 is refused");
    CHECK(strstr(err, "8000") && strstr(err, "128"), "and names both values: '%s'", err);
    err[0] = 0;
    CHECK(mi_enc_check_format(44100, 12, err, sizeof err) < 0, "44100/12 is refused");
    CHECK(strstr(err, "44100") && strstr(err, "12"), "and names both: '%s'", err);

    /* A configuration at the lowest tier really does start, and really does use
     * the smaller encoder frame those rates imply. */
    mi_enc_t *e = mi_enc_new(8000, 64, err, sizeof err);
    CHECK(e != NULL, "an MPEG-2.5 station starts: %s", err);
    if (e) {
        CHECK_EQ(mi_enc_samples_per_frame(e), 576, "MPEG-2.5 frames are 576 samples, not 1152");
        mi_enc_free(e);
    }
    e = mi_enc_new(44100, 128, err, sizeof err);
    CHECK(e != NULL, "the recommended default starts: %s", err);
    if (e) {
        CHECK_EQ(mi_enc_samples_per_frame(e), 1152, "MPEG-I frames are 1152 samples");
        mi_enc_free(e);
    }
    CHECK(mi_enc_new(44101, 128, err, sizeof err) == NULL, "an invalid format allocates nothing");

    /* Deprecated-but-documented: the default bit rate and meta interval are the
     * ones the header promises. */
    CHECK_EQ(MINI_ICECAST_METAINT_DEFAULT, 16000, "the documented metadata interval");
    CHECK_EQ(MINI_ICECAST_BITRATE_DEFAULT, 128, "the documented bit rate");
    CHECK_EQ(MINI_ICECAST_MAX_CLIENTS, 4, "the documented client-table size");
    TEST_END();
}
