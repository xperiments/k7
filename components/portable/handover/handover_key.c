/*
 * handover_key - FIPS 180-4 SHA-256, RFC 2104 HMAC and RFC 5869 HKDF, and the handover's
 * LMK on top of them.
 *
 * SHA-256 is implemented here rather than borrowed, and the header says why: the host build
 * links no crypto library, and the KDF has to be testable where it is cheap to change. The
 * curve is *not* here - both boards call mbedTLS for X25519, and a second curve
 * implementation in this repository would be a second thing to be wrong.
 *
 * All three primitives are checked against their published vectors in the host test: FIPS
 * 180-4's examples, RFC 4231's HMAC cases and RFC 5869's HKDF cases.
 */
#include "handover.h"

#include <string.h>

/* ---------------------------------------------------------------- SHA-256 */

static const uint32_t k_sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
    0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
    0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
    0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
    0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
    0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
    0xc67178f2};

typedef struct {
    uint32_t h[8];
    uint64_t bits;
    uint8_t buf[64];
    size_t used;
} sha256_t;

static uint32_t rotr32(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }

static void sha256_block(sha256_t *s, const uint8_t *blk)
{
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)blk[i * 4] << 24) | ((uint32_t)blk[i * 4 + 1] << 16) |
               ((uint32_t)blk[i * 4 + 2] << 8) | (uint32_t)blk[i * 4 + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3];
    uint32_t e = s->h[4], f = s->h[5], g = s->h[6], h = s->h[7];

    for (int i = 0; i < 64; i++) {
        uint32_t s1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + s1 + ch + k_sha256_k[i] + w[i];
        uint32_t s0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    s->h[0] += a;
    s->h[1] += b;
    s->h[2] += c;
    s->h[3] += d;
    s->h[4] += e;
    s->h[5] += f;
    s->h[6] += g;
    s->h[7] += h;
}

static void sha256_init(sha256_t *s)
{
    s->h[0] = 0x6a09e667;
    s->h[1] = 0xbb67ae85;
    s->h[2] = 0x3c6ef372;
    s->h[3] = 0xa54ff53a;
    s->h[4] = 0x510e527f;
    s->h[5] = 0x9b05688c;
    s->h[6] = 0x1f83d9ab;
    s->h[7] = 0x5be0cd19;
    s->bits = 0;
    s->used = 0;
}

static void sha256_update(sha256_t *s, const void *data, size_t len)
{
    const uint8_t *p = data;
    s->bits += (uint64_t)len * 8u;
    while (len) {
        size_t take = 64 - s->used;
        if (take > len) take = len;
        memcpy(s->buf + s->used, p, take);
        s->used += take;
        p += take;
        len -= take;
        if (s->used == 64) {
            sha256_block(s, s->buf);
            s->used = 0;
        }
    }
}

static void sha256_final(sha256_t *s, uint8_t out[32])
{
    uint64_t bits = s->bits;
    uint8_t pad = 0x80;
    sha256_update(s, &pad, 1);
    uint8_t zero = 0x00;
    while (s->used != 56) sha256_update(s, &zero, 1);
    uint8_t len_be[8];
    for (int i = 0; i < 8; i++) len_be[i] = (uint8_t)(bits >> (56 - 8 * i));
    sha256_update(s, len_be, 8);
    for (int i = 0; i < 8; i++) {
        out[i * 4] = (uint8_t)(s->h[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(s->h[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(s->h[i] >> 8);
        out[i * 4 + 3] = (uint8_t)s->h[i];
    }
}

void ho_sha256(const uint8_t *in, size_t len, uint8_t out[32])
{
    sha256_t s;
    sha256_init(&s);
    sha256_update(&s, in, len);
    sha256_final(&s, out);
}

/* ---------------------------------------------------------------- HMAC */

/* Streaming HMAC, so HKDF can feed it three pieces without building one buffer for a
 * message whose length is the caller's to choose. */
static void hmac_begin(sha256_t *inner, sha256_t *outer, const uint8_t *key, size_t key_len)
{
    uint8_t k[64];
    memset(k, 0, sizeof(k));
    if (key_len > sizeof(k)) {
        ho_sha256(key, key_len, k);
    } else if (key_len) {
        memcpy(k, key, key_len);
    }

    uint8_t pad[64];
    for (int i = 0; i < 64; i++) pad[i] = (uint8_t)(k[i] ^ 0x36);
    sha256_init(inner);
    sha256_update(inner, pad, sizeof(pad));

    for (int i = 0; i < 64; i++) pad[i] = (uint8_t)(k[i] ^ 0x5c);
    sha256_init(outer);
    sha256_update(outer, pad, sizeof(pad));
}

static void hmac_end(sha256_t *inner, sha256_t *outer, uint8_t out[32])
{
    uint8_t digest[32];
    sha256_final(inner, digest);
    sha256_update(outer, digest, sizeof(digest));
    sha256_final(outer, out);
}

void ho_hmac_sha256(const uint8_t *key, size_t key_len, const uint8_t *msg, size_t msg_len,
                    uint8_t out[32])
{
    sha256_t inner, outer;
    hmac_begin(&inner, &outer, key, key_len);
    sha256_update(&inner, msg, msg_len);
    hmac_end(&inner, &outer, out);
}

/* ---------------------------------------------------------------- HKDF */

bool ho_hkdf_sha256(const uint8_t *ikm, size_t ikm_len, const uint8_t *salt, size_t salt_len,
                    const uint8_t *info, size_t info_len, uint8_t *out, size_t out_len)
{
    if (!out || !out_len) return false;
    if (out_len > 255 * 32) return false;
    if (!ikm && ikm_len) return false;
    if (!salt && salt_len) return false;
    if (!info && info_len) return false;

    /* RFC 5869 §2.2: a salt that is not provided is HashLen zero bytes. */
    static const uint8_t zero_salt[32] = {0};
    const uint8_t *salt_p = salt ? salt : zero_salt;
    size_t salt_n = salt ? salt_len : sizeof(zero_salt);

    uint8_t prk[32];
    ho_hmac_sha256(salt_p, salt_n, ikm, ikm_len, prk);

    uint8_t t[32];
    size_t t_len = 0;
    size_t done = 0;
    for (uint8_t counter = 1; done < out_len; counter++) {
        /* T(n) = HMAC(PRK, T(n-1) || info || counter) */
        sha256_t inner, outer;
        hmac_begin(&inner, &outer, prk, sizeof(prk));
        sha256_update(&inner, t, t_len);
        sha256_update(&inner, info, info_len);
        sha256_update(&inner, &counter, 1);
        hmac_end(&inner, &outer, t);
        t_len = sizeof(t);

        size_t take = out_len - done;
        if (take > sizeof(t)) take = sizeof(t);
        memcpy(out + done, t, take);
        done += take;
    }
    return true;
}

/* ---------------------------------------------------------------- the LMK */

bool ho_lmk(const uint8_t shared[HO_KEY_LEN], const uint8_t k7_pub[HO_KEY_LEN],
            const uint8_t player_pub[HO_KEY_LEN], uint8_t out[HO_LMK_LEN])
{
    if (!shared || !k7_pub || !player_pub || !out) return false;

    /* The low-order-point answer is all zeroes, and it is not a network record (H28). */
    uint8_t any = 0;
    for (int i = 0; i < HO_KEY_LEN; i++) any |= shared[i];
    if (!any) return false;

    static const char label[] = "k7-handover-v1";
    uint8_t info[sizeof(label) - 1 + 2 * HO_KEY_LEN];
    size_t n = 0;
    memcpy(info, label, sizeof(label) - 1);
    n += sizeof(label) - 1;
    memcpy(info + n, k7_pub, HO_KEY_LEN);
    n += HO_KEY_LEN;
    memcpy(info + n, player_pub, HO_KEY_LEN);
    n += HO_KEY_LEN;

    return ho_hkdf_sha256(shared, HO_KEY_LEN, NULL, 0, info, n, out, HO_LMK_LEN);
}
