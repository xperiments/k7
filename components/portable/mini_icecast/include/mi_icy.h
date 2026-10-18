#pragma once
/*
 * mi_icy - interleave ICY metadata blocks into an encoded audio stream.
 *
 * The live stream is MP3 with no container, so a "now playing" title can only
 * reach a client as metadata interleaved into the byte stream: every `metaint`
 * encoded bytes, one block consisting of a length byte (in 16-byte units)
 * followed by that many bytes of `StreamTitle='...';`, zero-padded. The length
 * byte is always written, even when the title has not changed - the client counts
 * bytes, and a missing block would shift every later one.
 *
 * This module owns the byte counting, the block framing and the padding. It never
 * touches a socket: it hands audio spans and complete metadata blocks to a sink.
 * That is what lets the wire format be tested without a listener.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The longest title a block carries. Longer names are truncated; the metadata
 * interval is a byte count, so an unbounded title would push the block count past
 * what the one-byte length field can express. */
#define MI_ICY_MAX_TITLE 120

typedef struct mi_icy mi_icy_t;

typedef enum {
    MI_ICY_AUDIO = 0,    /* encoded stream bytes */
    MI_ICY_METADATA = 1, /* one complete block: length byte + padded payload */
} mi_icy_kind_t;

/* Called for each piece in stream order. Return false to stop; push() then
 * returns false. `ctx` is the caller's. */
typedef bool (*mi_icy_sink_t)(void *ctx, mi_icy_kind_t kind, const uint8_t *data, size_t n);

/* `metaint` is the number of encoded audio bytes between blocks. Zero is not a
 * stream with metadata: it returns NULL, and the caller sends audio directly. */
mi_icy_t *mi_icy_new(uint32_t metaint);
void      mi_icy_free(mi_icy_t *icy);

/* Number of audio bytes still to go before the next block. Exposed so a test can
 * pin the counter without reading the stream. */
uint32_t mi_icy_until_metadata(const mi_icy_t *icy);

/* Push audio. The sink sees it split at metadata boundaries, and sees one
 * metadata block after each complete interval. `title_version` decides whether the
 * block carries the title or the "unchanged" zero-length block: a version
 * different from the last one emitted (including the first push) carries it.
 * Returns false when the sink refused a piece. */
bool mi_icy_push(mi_icy_t *icy, const uint8_t *audio, size_t n,
                 const char *title, uint32_t title_version,
                 mi_icy_sink_t sink, void *ctx);

#ifdef __cplusplus
}
#endif
