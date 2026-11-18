#include "espnow_stream.h"

#include <string.h>

/* Status header: version, type, free (3), highest (2), nack count (1). */
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
    size_t need = STATUS_HEADER + 2u * s->nack_count;
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
    return true;
}
