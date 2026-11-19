#include "player_recv.h"

#include <string.h>

void player_recv_init(player_recv_t *r)
{
    if (!r) return;
    memset(r, 0, sizeof *r);
}

/* A sequence is "newer" when the 16-bit difference is positive, so the stream survives the
 * wrap at 65 536 that a station playing for two and a half hours reaches. */
static bool newer(uint16_t a, uint16_t b) { return (int16_t)(a - b) > 0; }

static player_recv_part_t *find_part(player_recv_t *r, uint16_t seq)
{
    for (unsigned i = 0; i < PLAYER_RECV_PARTS; i++)
        if (r->part[i].used && r->part[i].seq == seq) return &r->part[i];
    return NULL;
}

static player_recv_part_t *free_part(player_recv_t *r)
{
    for (unsigned i = 0; i < PLAYER_RECV_PARTS; i++)
        if (!r->part[i].used) return &r->part[i];
    return NULL;
}

static int find_held(const player_recv_t *r, uint16_t seq)
{
    for (unsigned i = 0; i < r->held_n; i++)
        if (r->held[i].seq == seq) return (int)i;
    return -1;
}

static void drop_part(player_recv_t *r, player_recv_part_t *p)
{
    (void)r;
    memset(p, 0, sizeof *p);
}

/* Hand out every frame that is now next in line, then say whether a gap is still open. */
static void drain(player_recv_t *r, player_recv_frame_fn on_frame, void *ctx)
{
    for (;;) {
        int k = find_held(r, r->next);
        if (!r->have_next || k < 0) break;
        const player_recv_held_t *h = &r->held[k];
        r->frames++;
        if (on_frame) on_frame(ctx, h->data, h->len);
        r->held[k] = r->held[r->held_n - 1];
        r->held_n--;
        r->next++;
    }
    r->gap = r->held_n > 0;
}

/* A complete frame: delivered when it is the next one, held when something before it is missing. */
static void complete(player_recv_t *r, player_recv_part_t *p, player_recv_frame_fn on_frame,
                     void *ctx)
{
    if (!r->have_next) {
        r->have_next = true;
        r->next = p->seq;
    }

    if (p->seq == r->next) {
        r->frames++;
        if (on_frame) on_frame(ctx, p->data, p->len);
        r->next++;
        drop_part(r, p);
        drain(r, on_frame, ctx);
        return;
    }

    /* Ahead of a gap: it waits, and it is what the status packet will ask about. */
    if (r->held_n == PLAYER_RECV_HELD) {
        r->parts_dropped++;
        drop_part(r, p);
        return;
    }
    player_recv_held_t *h = &r->held[r->held_n++];
    h->seq = p->seq;
    h->len = p->len;
    memcpy(h->data, p->data, p->len);
    drop_part(r, p);
    if (!r->gap) {
        r->gap = true;
        r->gap_at = 0; /* stamped by the next packet, which knows the clock */
    }
}

void player_recv_packet(player_recv_t *r, const uint8_t *pkt, size_t len, uint32_t now_ms,
                        player_recv_frame_fn on_frame, void *ctx)
{
    if (!r || !pkt) return;
    es_frag_t f;
    if (!es_parse_frag(pkt, len, &f)) return;

    if (!r->have_next) {
        r->have_next = true;
        r->next = f.seq;
    }

    /* Behind the stream: a duplicate, or a frame whose gap was already abandoned. */
    if (!newer(f.seq, (uint16_t)(r->next - 1))) {
        r->dups++;
        return;
    }

    player_recv_part_t *p = find_part(r, f.seq);
    if (!p) {
        p = free_part(r);
        if (!p) {
            r->parts_dropped++;
            return;
        }
        memset(p, 0, sizeof *p);
        p->used = true;
        p->seq = f.seq;
        p->nfrags = f.nfrags;
    }
    if (p->nfrags != f.nfrags) { /* two senders' ideas of one frame: drop it */
        drop_part(r, p);
        return;
    }
    if (f.frag >= 32 || (p->mask & (1u << f.frag))) return;
    size_t off = (size_t)f.frag * ES_MAX_PAYLOAD;
    if (off + f.payload_len > ES_MAX_FRAME) {
        drop_part(r, p);
        return;
    }
    memcpy(p->data + off, f.payload, f.payload_len);
    p->mask |= (uint8_t)(1u << f.frag);
    if (f.frag + 1u == f.nfrags) p->len = (uint16_t)(off + f.payload_len);

    uint8_t all = (uint8_t)((1u << p->nfrags) - 1u);
    if ((p->mask & all) == all && p->len) complete(r, p, on_frame, ctx);
    if (r->gap && r->gap_at == 0) r->gap_at = now_ms;
}

void player_recv_tick(player_recv_t *r, uint32_t now_ms,
                      player_recv_frame_fn on_frame, void *ctx)
{
    if (!r || !r->gap) return;
    if (r->gap_at == 0) {
        r->gap_at = now_ms;
        return;
    }
    if (now_ms - r->gap_at < PLAYER_RECV_GAP_MS) return;

    /* The second is up. Anything still incomplete and older than the gap goes, and the stream
     * moves to the oldest frame that did arrive. */
    uint16_t oldest = 0;
    bool have = false;
    for (unsigned i = 0; i < r->held_n; i++)
        if (!have || newer(oldest, r->held[i].seq)) {
            oldest = r->held[i].seq;
            have = true;
        }
    if (have && newer(oldest, r->next)) {
        r->abandoned++;
        r->lost += (uint32_t)(oldest - r->next);
        r->next = oldest;
    } else {
        /* Nothing to move to: the gap is all there is, so it is abandoned frame by frame as the
         * stream moves on with the next packet. */
        r->abandoned++;
    }

    for (unsigned i = 0; i < PLAYER_RECV_PARTS; i++) {
        if (r->part[i].used && !newer(r->part[i].seq, (uint16_t)(r->next - 1)))
            drop_part(r, &r->part[i]);
    }
    r->gap = false;
    r->gap_at = 0;
    drain(r, on_frame, ctx);
}

void player_recv_status(player_recv_t *r, uint32_t free_bytes, es_status_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof *out);
    out->free_bytes = free_bytes;
    if (!r || !r->have_next) return;

    out->highest = (uint16_t)(r->next - 1);

    /* The gaps: sequences from `next` that are neither held nor being assembled, up to the
     * highest thing the receiver knows about. */
    uint16_t top = (uint16_t)(r->next - 1);
    for (unsigned i = 0; i < r->held_n; i++)
        if (newer(r->held[i].seq, top)) top = r->held[i].seq;
    for (unsigned i = 0; i < PLAYER_RECV_PARTS; i++)
        if (r->part[i].used && newer(r->part[i].seq, top)) top = r->part[i].seq;

    for (uint16_t seq = r->next; !newer(seq, top) && out->nack_count < ES_MAX_NACK; seq++) {
        if (find_held(r, seq) >= 0) continue;
        if (find_part(r, seq)) continue;
        out->nack[out->nack_count++] = seq;
    }
}

void player_recv_stats(const player_recv_t *r, player_recv_stats_t *out)
{
    if (!r || !out) return;
    out->frames = r->frames;
    out->lost = r->lost;
    out->abandoned = r->abandoned;
    out->dups = r->dups;
    out->parts_dropped = r->parts_dropped;
}
