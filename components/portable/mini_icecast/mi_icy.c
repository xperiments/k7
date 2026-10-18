/*
 * mi_icy - the metadata interval, as a byte counter and a block builder.
 *
 * The two rules worth stating: the block length byte counts 16-byte units, and a
 * block is emitted even when the title has not changed, as a single zero byte.
 * Clients count audio bytes, so skipping either would misalign everything after.
 */
#include "mi_icy.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct mi_icy {
    uint32_t metaint;
    uint32_t since_meta;      /* audio bytes since the last block */
    uint32_t last_version;    /* version carried by the last block; UINT32_MAX = none */
};

mi_icy_t *mi_icy_new(uint32_t metaint)
{
    if (metaint == 0) return NULL;
    mi_icy_t *icy = calloc(1, sizeof *icy);
    if (!icy) return NULL;
    icy->metaint = metaint;
    /* No block emitted yet, so the first one must carry the current title even
     * when the broadcast's version is still zero. */
    icy->last_version = UINT32_MAX;
    return icy;
}

void mi_icy_free(mi_icy_t *icy) { free(icy); }

uint32_t mi_icy_until_metadata(const mi_icy_t *icy)
{
    return icy ? icy->metaint - icy->since_meta : 0;
}

/* One complete block: length byte, StreamTitle payload, zero padding to the next
 * 16-byte boundary. A quote in the title would end the payload early for every
 * parser, so it is replaced with a space rather than escaped. */
static bool emit_block(mi_icy_t *icy, const char *title, uint32_t version,
                       mi_icy_sink_t sink, void *ctx)
{
    if (version == icy->last_version) {
        uint8_t zero = 0;
        if (!sink(ctx, MI_ICY_METADATA, &zero, 1)) return false;
        return true;
    }

    char escaped[MI_ICY_MAX_TITLE + 1];
    size_t n = 0;
    for (const char *p = title ? title : ""; *p && n < MI_ICY_MAX_TITLE; p++)
        escaped[n++] = (*p == '\'') ? ' ' : *p;
    escaped[n] = 0;

    char text[MI_ICY_MAX_TITLE + 24];
    int len = snprintf(text, sizeof text, "StreamTitle='%s';", escaped);
    if (len < 0) return false;
    if ((size_t)len >= sizeof text) len = (int)sizeof text - 1;

    unsigned blocks = ((unsigned)len + 15) / 16;
    if (blocks > 255) blocks = 255; /* the length field is one byte */
    if ((unsigned)len > blocks * 16) len = (int)(blocks * 16);
    /* The payload is "StreamTitle='...';": 15 bytes of framing plus the title,
     * so the block count is bounded by MI_ICY_MAX_TITLE. The buffer is that size
     * rather than 4 KB: this frame is on the broadcast task's stack, and that
     * stack is also the decoder's. */
    _Static_assert((MI_ICY_MAX_TITLE + 30) / 16 <= 10, "MI_ICY_MAX_TITLE outgrew the block buffer");
    uint8_t buf[1 + 10 * 16];
    memset(buf, 0, 1 + blocks * 16);
    buf[0] = (uint8_t)blocks;
    memcpy(buf + 1, text, (size_t)len);

    if (!sink(ctx, MI_ICY_METADATA, buf, 1 + (size_t)blocks * 16)) return false;
    icy->last_version = version;
    return true;
}

bool mi_icy_push(mi_icy_t *icy, const uint8_t *audio, size_t n,
                 const char *title, uint32_t title_version,
                 mi_icy_sink_t sink, void *ctx)
{
    if (!icy || !sink) return false;
    while (n) {
        size_t k = icy->metaint - icy->since_meta;
        if (k > n) k = n;
        if (!sink(ctx, MI_ICY_AUDIO, audio, k)) return false;
        audio += k;
        n -= k;
        icy->since_meta += (uint32_t)k;
        if (icy->since_meta == icy->metaint) {
            icy->since_meta = 0;
            if (!emit_block(icy, title, title_version, sink, ctx)) return false;
        }
    }
    return true;
}
