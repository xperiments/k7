#include "player_mp3.h"

#include <string.h>

/* Layer III only: the station format is MP3, and a player that quietly accepted Layer II
 * would compute the wrong frame length for it. */
static const uint16_t k_bitrate_v1[16] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};
static const uint16_t k_bitrate_v2[16] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0};
static const uint16_t k_rate_v1[3] = {44100, 48000, 32000};
static const uint16_t k_rate_v2[3] = {22050, 24000, 16000};
static const uint16_t k_rate_v25[3] = {11025, 12000, 8000};

/* A valid Layer III header at h: version not reserved, layer III, real bit rate and rate. */
static bool header_ok(const uint8_t *h)
{
    if (h[0] != 0xFF || (h[1] & 0xE0) != 0xE0) return false;
    unsigned version = (h[1] >> 3) & 3;
    unsigned layer = (h[1] >> 1) & 3;
    if (version == 1 || layer != 1) return false;
    unsigned bri = (h[2] >> 4) & 15;
    unsigned sri = (h[2] >> 2) & 3;
    if (bri == 0 || bri == 15 || sri == 3) return false;
    return true;
}

static bool header_info(const uint8_t *h, player_mp3_info_t *info, size_t *frame_len)
{
    unsigned version = (h[1] >> 3) & 3; /* 3 = MPEG-1, 2 = MPEG-2, 0 = MPEG-2.5 */
    unsigned bri = (h[2] >> 4) & 15;
    unsigned sri = (h[2] >> 2) & 3;
    unsigned pad = (h[2] >> 1) & 1;
    unsigned mode = h[3] >> 6;

    unsigned bitrate = (version == 3 ? k_bitrate_v1 : k_bitrate_v2)[bri];
    unsigned rate = (version == 3 ? k_rate_v1 : version == 2 ? k_rate_v2 : k_rate_v25)[sri];
    /* MPEG-1 Layer III: 144 kbps/rate. MPEG-2/2.5 Layer III halves the coefficient. */
    size_t len = (size_t)((version == 3 ? 144u : 72u) * bitrate * 1000u / rate) + pad;
    if (len < 4) return false;

    if (info) {
        info->rate = rate;
        info->bitrate = bitrate;
        info->channels = (mode == 3) ? 1 : 2;
    }
    if (frame_len) *frame_len = len;
    return true;
}

void player_mp3_init(player_mp3_t *p)
{
    if (!p) return;
    memset(p, 0, sizeof *p);
}

size_t player_mp3_feed(player_mp3_t *p, const uint8_t *data, size_t len,
                       player_mp3_frame_fn on_frame, void *ctx)
{
    if (!p) return 0;
    size_t emitted = 0;

    while (len > 0) {
        /* Top the carry up. */
        size_t space = sizeof p->buf - p->len;
        size_t take = len < space ? len : space;
        if (take) {
            memcpy(p->buf + p->len, data, take);
            p->len += take;
            data += take;
            len -= take;
        }

        for (;;) {
            /* Drop anything before a header. */
            size_t off = 0;
            while (off + 4 <= p->len && !header_ok(p->buf + off)) off++;
            if (off) {
                memmove(p->buf, p->buf + off, p->len - off);
                p->len -= off;
                p->skipped += (unsigned)off;
            }
            if (p->len < 4) break;

            size_t flen = 0;
            if (!header_info(p->buf, p->info_set ? NULL : &p->info, &flen)) {
                /* A header that does not compute: treat its first byte as garbage. */
                memmove(p->buf, p->buf + 1, p->len - 1);
                p->len--;
                p->skipped++;
                continue;
            }
            if (p->len < flen) break; /* the rest of this frame is still on the wire */

            if (!p->info_set) p->info_set = true;
            if (on_frame) on_frame(ctx, p->buf, flen);
            memmove(p->buf, p->buf + flen, p->len - flen);
            p->len -= flen;
            p->frames++;
            emitted++;
        }

        /* No room and no progress: the buffer is full of something that is not a frame. Give
         * up a byte at a time rather than spinning; the framer will resynchronise. */
        if (take == 0 && len > 0 && emitted == 0) {
            memmove(p->buf, p->buf + 1, p->len - 1);
            p->len--;
            p->skipped++;
        }
    }
    return emitted;
}

bool player_mp3_streaming(const player_mp3_t *p) { return p && p->frames > 0; }
unsigned player_mp3_skipped(const player_mp3_t *p) { return p ? p->skipped : 0; }
