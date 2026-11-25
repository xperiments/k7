#include "player_update.h"

#include <stdio.h>
#include <string.h>

/* Every integer on this wire is big-endian (player-update.md), the same convention the
 * handover and the audio stream share on one radio. */

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFF);
}

static uint16_t get16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v & 0xFF);
}

static uint32_t get32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

static void put_version(uint8_t *p, pu_version_t v)
{
    put16(p, v.major);
    put16(p + 2, v.minor);
    put16(p + 4, v.patch);
}

static pu_version_t get_version(const uint8_t *p)
{
    pu_version_t v = {get16(p), get16(p + 2), get16(p + 4)};
    return v;
}

/* ------------------------------------------------------------------- the header */

bool pu_header_encode(const pu_header_t *h, uint8_t out[PU_HEADER_SIZE])
{
    if (!h || !out) return false;
    if (h->board != PU_BOARD_ESP32) return false;
    if (h->sig_len == 0 || h->sig_len > PU_SIG_MAX) return false;
    if (h->image_len == 0 || h->image_len > PU_IMAGE_MAX) return false;

    memset(out, 0, PU_HEADER_SIZE);
    memcpy(out, PU_MAGIC, 4);
    out[4] = PU_VERSION;
    out[5] = h->board;
    out[6] = h->sig_len;
    /* out[7], 18..19 and 124..127 are the reserved runs, zero from the memset. */
    put32(out + 8, h->image_len);
    put_version(out + 12, h->version);
    memcpy(out + 20, h->sha256, 32);
    memcpy(out + 52, h->sig, h->sig_len);
    return true;
}

bool pu_header_parse(const uint8_t *in, size_t len, pu_header_t *out)
{
    if (!in || !out || len != PU_HEADER_SIZE) return false;
    if (memcmp(in, PU_MAGIC, 4) != 0) return false;
    if (in[4] != PU_VERSION) return false;
    if (in[5] != PU_BOARD_ESP32) return false;
    if (in[6] == 0 || in[6] > PU_SIG_MAX) return false;
    if (in[7] != 0 || in[18] != 0 || in[19] != 0) return false;
    for (size_t i = 124; i < PU_HEADER_SIZE; i++)
        if (in[i] != 0) return false;

    uint32_t image_len = get32(in + 8);
    if (image_len == 0 || image_len > PU_IMAGE_MAX) return false;

    memset(out, 0, sizeof *out);
    out->board = in[5];
    out->image_len = image_len;
    out->version = get_version(in + 12);
    memcpy(out->sha256, in + 20, 32);
    out->sig_len = in[6];
    memcpy(out->sig, in + 52, out->sig_len);
    return true;
}

bool pu_header_matches_digest(const pu_header_t *h, const uint8_t sha256[32])
{
    if (!h || !sha256) return false;
    return memcmp(h->sha256, sha256, 32) == 0;
}

/* -------------------------------------------------------------- probe, answer */

size_t pu_probe_encode(uint32_t nonce, uint8_t out[PU_PROBE_SIZE])
{
    if (!out) return 0;
    out[0] = PU_VERSION;
    out[1] = PU_TYPE_PROBE;
    put32(out + 2, nonce);
    return PU_PROBE_SIZE;
}

bool pu_probe_parse(const uint8_t *pkt, size_t len, uint32_t *nonce)
{
    if (!pkt || !nonce || len != PU_PROBE_SIZE) return false;
    if (pkt[0] != PU_VERSION || pkt[1] != PU_TYPE_PROBE) return false;
    *nonce = get32(pkt + 2);
    return true;
}

size_t pu_answer_encode(const pu_answer_t *a, uint8_t out[PU_ANSWER_SIZE])
{
    if (!a || !out) return 0;
    out[0] = PU_VERSION;
    out[1] = PU_TYPE_ANSWER;
    put32(out + 2, a->nonce);
    put_version(out + 6, a->version);
    out[12] = a->board;
    out[13] = a->flags;
    return PU_ANSWER_SIZE;
}

bool pu_answer_parse(const uint8_t *pkt, size_t len, pu_answer_t *out)
{
    if (!pkt || !out || len != PU_ANSWER_SIZE) return false;
    if (pkt[0] != PU_VERSION || pkt[1] != PU_TYPE_ANSWER) return false;
    out->nonce = get32(pkt + 2);
    out->version = get_version(pkt + 6);
    /* The board and the flags are the receiver's to judge, not this parser's: a player of
     * another board is still a player, and an unknown flag bit is an old reader's, not a
     * malformed frame. */
    out->board = pkt[12];
    out->flags = pkt[13];
    return true;
}

/* ------------------------------------------------------------------- the claim */

size_t pu_begin_encode(const pu_begin_t *b, uint8_t out[PU_BEGIN_SIZE])
{
    if (!b || !out) return 0;
    if (b->sig_len == 0 || b->sig_len > PU_SIG_MAX) return 0;
    if (b->image_len == 0 || b->image_len > PU_IMAGE_MAX) return 0;

    out[0] = PU_VERSION;
    out[1] = PU_TYPE_BEGIN;
    put_version(out + 2, b->version);
    out[8] = b->board;
    put32(out + 9, b->image_len);
    memcpy(out + 13, b->sha256, 32);
    out[45] = b->sig_len;
    memcpy(out + 46, b->sig, b->sig_len);
    return PU_BEGIN_SIZE;
}

bool pu_begin_parse(const uint8_t *pkt, size_t len, pu_begin_t *out)
{
    if (!pkt || !out || len != PU_BEGIN_SIZE) return false;
    if (pkt[0] != PU_VERSION || pkt[1] != PU_TYPE_BEGIN) return false;
    if (pkt[45] == 0 || pkt[45] > PU_SIG_MAX) return false;

    uint32_t image_len = get32(pkt + 9);
    if (image_len == 0 || image_len > PU_IMAGE_MAX) return false;

    memset(out, 0, sizeof *out);
    out->version = get_version(pkt + 2);
    out->board = pkt[8];
    out->image_len = image_len;
    memcpy(out->sha256, pkt + 13, 32);
    out->sig_len = pkt[45];
    memcpy(out->sig, pkt + 46, out->sig_len);
    return true;
}

/* ------------------------------------------------------------------- the block */

size_t pu_block_fragment(uint16_t block, const uint8_t *data, size_t len, uint8_t *out,
                         size_t lens[PU_BLOCK_MAX_FRAGS])
{
    if (!data || !len || !out || !lens || len > PU_BLOCK_SIZE) return 0;
    size_t nfrags = (len + PU_FRAG_PAYLOAD - 1) / PU_FRAG_PAYLOAD;
    if (nfrags == 0 || nfrags > PU_BLOCK_MAX_FRAGS) return 0;

    size_t off = 0;
    for (size_t i = 0; i < nfrags; i++) {
        uint8_t *p = out + i * PU_MTU;
        size_t take = len - off;
        if (take > PU_FRAG_PAYLOAD) take = PU_FRAG_PAYLOAD;
        p[0] = PU_VERSION;
        p[1] = PU_TYPE_BLOCK;
        put16(p + 2, block);
        p[4] = (uint8_t)i;
        p[5] = (uint8_t)nfrags;
        memcpy(p + 6, data + off, take);
        lens[i] = 6 + take;
        off += take;
    }
    return nfrags;
}

bool pu_block_parse(const uint8_t *pkt, size_t len, pu_frag_t *out)
{
    if (!pkt || !out || len < 6) return false;
    if (pkt[0] != PU_VERSION || pkt[1] != PU_TYPE_BLOCK) return false;
    uint8_t frag = pkt[4], nfrags = pkt[5];
    if (nfrags == 0 || nfrags > PU_BLOCK_MAX_FRAGS || frag >= nfrags) return false;
    if (len - 6 > PU_FRAG_PAYLOAD) return false;
    /* Every fragment but the last carries a full payload: a short one is a damaged packet,
     * and accepting it would splice a hole into a block that claims to be whole. */
    if (frag + 1 < nfrags && len - 6 != PU_FRAG_PAYLOAD) return false;

    out->block = get16(pkt + 2);
    out->frag = frag;
    out->nfrags = nfrags;
    out->payload = pkt + 6;
    out->payload_len = len - 6;
    return true;
}

/* ------------------------------------------------------- the acknowledgement */

size_t pu_ack_encode(uint16_t block, uint8_t result, uint8_t out[PU_ACK_SIZE])
{
    if (!out || result > PU_ACK_REFUSED) return 0;
    out[0] = PU_VERSION;
    out[1] = PU_TYPE_ACK;
    put16(out + 2, block);
    out[4] = result;
    return PU_ACK_SIZE;
}

bool pu_ack_parse(const uint8_t *pkt, size_t len, uint16_t *block, uint8_t *result)
{
    if (!pkt || !block || !result || len != PU_ACK_SIZE) return false;
    if (pkt[0] != PU_VERSION || pkt[1] != PU_TYPE_ACK) return false;
    if (pkt[4] > PU_ACK_REFUSED) return false;
    *block = get16(pkt + 2);
    *result = pkt[4];
    return true;
}

/* ------------------------------------------------------------------ the result */

size_t pu_result_encode(uint8_t result, pu_version_t version, uint8_t out[PU_RESULT_SIZE])
{
    if (!out || result > PU_READY) return 0;
    out[0] = PU_VERSION;
    out[1] = PU_TYPE_RESULT;
    out[2] = result;
    put_version(out + 3, version);
    return PU_RESULT_SIZE;
}

bool pu_result_parse(const uint8_t *pkt, size_t len, uint8_t *result, pu_version_t *version)
{
    if (!pkt || !result || !version || len != PU_RESULT_SIZE) return false;
    if (pkt[0] != PU_VERSION || pkt[1] != PU_TYPE_RESULT) return false;
    if (pkt[2] > PU_READY) return false;
    *result = pkt[2];
    *version = get_version(pkt + 3);
    return true;
}

/* ------------------------------------------------------------------ the version */

int pu_version_cmp(pu_version_t a, pu_version_t b)
{
    if (a.major != b.major) return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
    return 0;
}

size_t pu_version_format(pu_version_t v, char out[PU_VERSION_TEXT_MAX])
{
    if (!out) return 0;
    int n = snprintf(out, PU_VERSION_TEXT_MAX, "%u.%u.%u", (unsigned)v.major,
                     (unsigned)v.minor, (unsigned)v.patch);
    if (n < 0 || n >= PU_VERSION_TEXT_MAX) {
        out[0] = '\0';
        return 0;
    }
    return (size_t)n;
}

/* --------------------------------------------------------------- the geometry */

unsigned pu_block_count(uint32_t image_len)
{
    if (image_len == 0) return 0;
    return (unsigned)((image_len + PU_BLOCK_SIZE - 1) / PU_BLOCK_SIZE);
}
