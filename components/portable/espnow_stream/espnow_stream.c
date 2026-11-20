#include "espnow_stream.h"

#include <string.h>

#define STATUS_HEADER 8

size_t es_fragment(uint16_t seq, const uint8_t *frame, size_t len,
                   uint8_t *out, size_t lens[ES_MAX_FRAGS])
{
    if (!frame || !len || !out || !lens) return 0;
    size_t nfrags = (len + ES_MAX_PAYLOAD - 1) / ES_MAX_PAYLOAD;
    if (nfrags == 0 || nfrags > ES_MAX_FRAGS) return 0;

    size_t off = 0;
    for (size_t i = 0; i < nfrags; i++) {
        uint8_t *p = out + i * ES_MTU;
        size_t take = len - off;
        if (take > ES_MAX_PAYLOAD) take = ES_MAX_PAYLOAD;
        p[0] = ES_VERSION;
        p[1] = ES_TYPE_AUDIO;
        p[2] = (uint8_t)(seq >> 8);
        p[3] = (uint8_t)(seq & 0xFF);
        p[4] = (uint8_t)i;
        p[5] = (uint8_t)nfrags;
        memcpy(p + ES_HEADER, frame + off, take);
        lens[i] = ES_HEADER + take;
        off += take;
    }
    return nfrags;
}

bool es_parse_frag(const uint8_t *pkt, size_t len, es_frag_t *out)
{
    if (!pkt || !out || len < ES_HEADER) return false;
    if (pkt[0] != ES_VERSION || pkt[1] != ES_TYPE_AUDIO) return false;
    uint8_t frag = pkt[4], nfrags = pkt[5];
    if (nfrags == 0 || nfrags > ES_MAX_FRAGS || frag >= nfrags) return false;
    if (len - ES_HEADER > ES_MAX_PAYLOAD) return false;
    /* Every fragment but the last carries a full payload: a short one is a damaged packet, and
     * accepting it would splice a hole into a frame that claims to be whole. */
    if (frag + 1 < nfrags && len - ES_HEADER != ES_MAX_PAYLOAD) return false;
    out->seq = (uint16_t)((pkt[2] << 8) | pkt[3]);
    out->frag = frag;
    out->nfrags = nfrags;
    out->payload = pkt + ES_HEADER;
    out->payload_len = len - ES_HEADER;
    return true;
}

size_t es_status_encode(const es_status_t *s, uint8_t *out, size_t cap)
{
    if (!s || !out) return 0;
    if (s->nack_count > ES_MAX_NACK) return 0;
    size_t need = STATUS_HEADER + 2u * s->nack_count + 1u;
    if (cap < need) return 0;

    out[0] = ES_VERSION;
    out[1] = ES_TYPE_STATUS;
    out[2] = (uint8_t)(s->free_bytes >> 16);
    out[3] = (uint8_t)(s->free_bytes >> 8);
    out[4] = (uint8_t)(s->free_bytes & 0xFF);
    out[5] = (uint8_t)(s->highest >> 8);
    out[6] = (uint8_t)(s->highest & 0xFF);
    out[7] = s->nack_count;
    for (size_t i = 0; i < s->nack_count; i++) {
        out[STATUS_HEADER + 2 * i] = (uint8_t)(s->nack[i] >> 8);
        out[STATUS_HEADER + 2 * i + 1] = (uint8_t)(s->nack[i] & 0xFF);
    }
    /* E3a: the optional flags byte is always written by this firmware and ignored by a receiver
     * that predates it, which is what makes an old K7 and a new player a valid pair. */
    out[STATUS_HEADER + 2u * s->nack_count] = s->flags;
    return need;
}

bool es_status_parse(const uint8_t *pkt, size_t len, es_status_t *out)
{
    if (!pkt || !out || len < STATUS_HEADER) return false;
    if (pkt[0] != ES_VERSION || pkt[1] != ES_TYPE_STATUS) return false;
    if (pkt[7] > ES_MAX_NACK) return false;
    if (len < STATUS_HEADER + 2u * pkt[7]) return false;

    memset(out, 0, sizeof *out);
    out->free_bytes = ((uint32_t)pkt[2] << 16) | ((uint32_t)pkt[3] << 8) | pkt[4];
    out->highest = (uint16_t)((pkt[5] << 8) | pkt[6]);
    out->nack_count = pkt[7];
    for (unsigned i = 0; i < out->nack_count; i++)
        out->nack[i] = (uint16_t)((pkt[STATUS_HEADER + 2 * i] << 8) | pkt[STATUS_HEADER + 2 * i + 1]);
    /* An old player's packet ends after the list; that is flags 0, not a short packet. */
    if (len > STATUS_HEADER + 2u * out->nack_count)
        out->flags = pkt[STATUS_HEADER + 2u * out->nack_count];
    return true;
}

/* ------------------------------------------------------------------- the tone */

static int16_t clamp_cdb(int v, int lo, int hi)
{
    if (v < lo) return (int16_t)lo;
    if (v > hi) return (int16_t)hi;
    return (int16_t)v;
}

static int16_t get16(const uint8_t *p) { return (int16_t)((uint16_t)p[0] << 8 | p[1]); }

static void put16(uint8_t *p, int16_t v)
{
    p[0] = (uint8_t)((uint16_t)v >> 8);
    p[1] = (uint8_t)((uint16_t)v & 0xFF);
}

size_t es_tone_encode(const es_tone_t *t, uint8_t *out, size_t cap)
{
    if (!t || !out || cap < ES_TONE_SIZE) return 0;
    out[0] = ES_VERSION;
    out[1] = ES_TYPE_TONE;
    put16(out + 2, clamp_cdb(t->gain_cdb, ES_TONE_GAIN_MIN_CDB, ES_TONE_GAIN_MAX_CDB));
    put16(out + 4, clamp_cdb(t->low_cdb, ES_TONE_BAND_MIN_CDB, ES_TONE_BAND_MAX_CDB));
    put16(out + 6, clamp_cdb(t->mid_cdb, ES_TONE_BAND_MIN_CDB, ES_TONE_BAND_MAX_CDB));
    put16(out + 8, clamp_cdb(t->high_cdb, ES_TONE_BAND_MIN_CDB, ES_TONE_BAND_MAX_CDB));
    return ES_TONE_SIZE;
}

bool es_tone_parse(const uint8_t *pkt, size_t len, es_tone_t *out)
{
    if (!pkt || !out || len != ES_TONE_SIZE) return false;
    if (pkt[0] != ES_VERSION || pkt[1] != ES_TYPE_TONE) return false;

    out->gain_cdb = clamp_cdb(get16(pkt + 2), ES_TONE_GAIN_MIN_CDB, ES_TONE_GAIN_MAX_CDB);
    out->low_cdb = clamp_cdb(get16(pkt + 4), ES_TONE_BAND_MIN_CDB, ES_TONE_BAND_MAX_CDB);
    out->mid_cdb = clamp_cdb(get16(pkt + 6), ES_TONE_BAND_MIN_CDB, ES_TONE_BAND_MAX_CDB);
    out->high_cdb = clamp_cdb(get16(pkt + 8), ES_TONE_BAND_MIN_CDB, ES_TONE_BAND_MAX_CDB);
    return true;
}

/* ------------------------------------------------------------------- the sender */

static bool send_frame(es_emitter_t *e, uint16_t seq, const uint8_t *frame, size_t len)
{
    size_t lens[ES_MAX_FRAGS];
    size_t n = es_fragment(seq, frame, len, e->pktbuf, lens);
    if (!n) return false;
    for (size_t i = 0; i < n; i++) {
        /* A radio that refuses one packet has lost the frame: the receiver's gap will name it
         * again if it is still in the history. */
        if (!e->send || !e->send(e->pktbuf + i * ES_MTU, lens[i], e->ctx)) return false;
        e->packets++;
    }
    return true;
}

void es_emitter_init(es_emitter_t *e, uint8_t *arena, size_t arena_bytes,
                     es_send_fn send, void *ctx)
{
    if (!e) return;
    memset(e, 0, sizeof *e);
    e->arena = arena;
    e->slots = (unsigned)(arena_bytes / ES_MAX_FRAME);
    if (e->slots > ES_HISTORY_FRAMES) e->slots = ES_HISTORY_FRAMES;
    e->send = send;
    e->ctx = ctx;
}

bool es_emitter_push(es_emitter_t *e, const uint8_t *frame, size_t len)
{
    if (!e || !frame || !e->arena || !e->slots || len == 0 || len > ES_MAX_FRAME) return false;

    uint16_t seq = e->seq_next++;
    unsigned slot = seq % e->slots;
    memcpy(e->arena + (size_t)slot * ES_MAX_FRAME, frame, len);
    e->ent[slot].seq = seq;
    e->ent[slot].len = (uint16_t)len;
    e->ent[slot].used = true;

    if (e->credit >= len) {
        e->credit -= (uint32_t)len;
        if (send_frame(e, seq, frame, len)) {
            e->sent++;
        } else {
            e->dropped++;
        }
    } else {
        /* The ring is full for now: the frame waits in the history and the receiver's next gap
         * names it. That is what makes a full ring a pause rather than a loss. */
        e->held++;
    }
    return true;
}

unsigned es_emitter_status(es_emitter_t *e, const uint8_t *pkt, size_t len)
{
    es_status_t st;
    if (!e || !pkt || !es_status_parse(pkt, len, &st)) return 0;

    e->have_status = true;
    e->tone_capable = (st.flags & ES_STATUS_FLAG_TONE) != 0;
    e->credit = st.free_bytes;

    unsigned resent = 0;
    for (unsigned i = 0; i < st.nack_count; i++) {
        uint16_t seq = st.nack[i];
        unsigned slot = seq % e->slots;
        if (!e->slots || !e->ent[slot].used || e->ent[slot].seq != seq) continue;
        size_t flen = e->ent[slot].len;
        if (e->credit < flen) break;
        e->credit -= (uint32_t)flen;
        if (send_frame(e, seq, e->arena + (size_t)slot * ES_MAX_FRAME, flen)) {
            e->resent++;
            resent++;
        }
    }
    return resent;
}

void es_emitter_stats(const es_emitter_t *e, es_emitter_stats_t *out)
{
    if (!e || !out) return;
    out->packets = e->packets;
    out->sent = e->sent;
    out->resent = e->resent;
    out->dropped = e->dropped;
    out->held = e->held;
    out->have_status = e->have_status;
    out->tone_capable = e->tone_capable;
}
