/* s5-ogg-page-writer.c -- minimal Ogg page writer for ONE live logical bitstream.
 *
 * Reference implementation for docs/research/server/s5-ogg-container-and-muxing.md.
 * No seeking, no demuxer, no allocation, no libc beyond memcpy/memset.
 *
 * VERIFIED: the encoder path below reproduces all 27 page headers that libogg
 * hard-codes in its own test suite (src/framing.c), byte for byte, when driven
 * with libogg's page-close heuristic.  See s5-ogg-verify.c.
 *
 * Build/verify:  cc -O2 -I. -o s5-ogg-verify s5-ogg-verify.c && ./s5-ogg-verify
 *
 * Usage in the radio server (Opus):
 *   oggmux_init(&m, random_serial);
 *   oggmux_packet(&m, opushead, 19, 0, out, 0);          // BOS page, alone
 *   oggmux_flush(&m, out, 1);
 *   oggmux_packet(&m, opustags, len, 0, out, 0);         // must END its page
 *   oggmux_flush(&m, out, 1);
 *   for each encoded frame:
 *       granule += 960;                                   // 48 kHz samples
 *       oggmux_packet(&m, frame, flen, granule, out, 0);
 *       while ((w = oggmux_flush(&m, out, 0)) > 0) send(w);   // page every ~0.5 s
 *   // to change track: emit an EOS page (oggmux_packet(...,eos=1); flush force=1),
 *   // then oggmux_init() with a NEW serial and repeat from OpusHead.
 */

#include <stdint.h>
#include <string.h>

#define OGG_MAX_SEGS   255
#define OGG_MAX_BODY   (255 * 255)                        /* 65025 payload bytes */
#define OGG_MAX_PAGE   (OGG_MAX_BODY + 27 + OGG_MAX_SEGS) /* 65307 bytes, RFC 3533 §6 */
#define PKT_START      0x100   /* lacing-array bit: a packet begins on this segment */

typedef struct {
    uint32_t serial, pageno;
    int      bos_done;      /* BOS page already emitted?                  */
    int      eos;           /* caller signalled end of stream            */
    int      more_pkts;     /* a packet will still complete on this page  */
    uint8_t  body[OGG_MAX_BODY];
    uint16_t lacing[OGG_MAX_SEGS];   /* low byte = size, bit 8 = starts packet */
    int      nseg, body_len, pkts_done;
    uint64_t granule;       /* granule of last packet submitted          */
    uint64_t page_gp;       /* granule of last packet COMPLETED on page  */
    int      page_gp_valid;
} oggmux_t;

/* CRC: poly 0x04c11db7, MSB-first, init 0, no final XOR.
 * libogg src/framing.c: "unreflected alg and an init/final of 0, not 0xffffffff".
 * RFC 3533 §6: "The generator polynomial is 0x04c11db7". */
static uint32_t ogg_crc(uint32_t crc, const uint8_t *p, size_t n)
{
    while (n--) {
        crc ^= (uint32_t)(*p++) << 24;
        for (int i = 0; i < 8; i++)
            crc = (crc & 0x80000000u) ? (crc << 1) ^ 0x04c11db7u : (crc << 1);
    }
    return crc;
}

static void put32(uint8_t *p, uint32_t v)
{ p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24); }
static void put64(uint8_t *p, uint64_t v)
{ for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8*i)); }

static void oggmux_init(oggmux_t *m, uint32_t serial)
{ memset(m, 0, sizeof(*m)); m->serial = serial; m->granule = ~(uint64_t)0; }

/* Emit the open page if warranted.  Returns bytes written (0 = nothing). */
static int oggmux_flush(oggmux_t *m, uint8_t *out, int force)
{
    if (m->nseg == 0) return 0;
    if (!m->bos_done) force = 1;              /* ID header alone on page 0 */
    if (!force && m->nseg < OGG_MAX_SEGS && !(m->body_len > 4096 && m->pkts_done >= 4))
        return 0;                             /* page-close policy, see note */

    uint8_t hdr[27 + OGG_MAX_SEGS];
    memcpy(hdr, "OggS", 4);
    hdr[4] = 0;                               /* stream_structure_version = 0 */
    uint8_t flags = 0;
    if (!(m->lacing[0] & PKT_START)) flags |= 0x01;   /* continued packet    */
    if (!m->bos_done)                flags |= 0x02;   /* beginning of stream */
    if (m->eos && !m->more_pkts)     flags |= 0x04;   /* end of stream       */
    hdr[5] = flags;
    /* granulepos = granule of the last packet that COMPLETED on this page, or
       -1 if none did (page wholly spanned by one packet).  RFC 3533 §6. */
    uint64_t gp = m->page_gp_valid ? m->page_gp : ~(uint64_t)0;
    if (!m->bos_done) gp = 0;
    put64(hdr + 6, gp);
    put32(hdr + 14, m->serial);
    put32(hdr + 18, m->pageno++);
    put32(hdr + 22, 0);                       /* zeroed for the CRC computation */
    hdr[26] = (uint8_t)m->nseg;
    int n = 0;
    for (int i = 0; i < m->nseg; i++) { hdr[27+i] = (uint8_t)m->lacing[i]; n += m->lacing[i] & 0xff; }

    int hlen = 27 + m->nseg;                  /* RFC 3533 §6: 27 + nseg */
    memcpy(out, hdr, hlen);
    memcpy(out + hlen, m->body, n);
    put32(out + 22, ogg_crc(ogg_crc(0, out, hlen), out + hlen, n));

    m->bos_done = 1;
    m->nseg = 0; m->body_len = 0; m->granule = ~(uint64_t)0;
    m->page_gp_valid = 0; m->pkts_done = 0;
    return hlen + n;
}

/* Submit one packet.  Splits across pages exactly as RFC 3533 §5 requires:
   a 255-byte multiple is terminated by a lacing value of 0. */
static int oggmux_packet(oggmux_t *m, const uint8_t *pkt, int len, uint64_t gp,
                         uint8_t *out, int eos)
{
    int total = 0;
    const uint8_t *p = pkt;
    m->more_pkts = 1;
    int nseg_total = len / 255 + 1;
    for (int i = 0; i < nseg_total; i++) {
        if (m->nseg == OGG_MAX_SEGS) total += oggmux_flush(m, out + total, 1);
        int chunk = (i < nseg_total - 1) ? 255 : len - 255 * (nseg_total - 1);
        uint16_t lv = (uint16_t)chunk;
        if (i == 0) lv |= PKT_START;   /* only the FIRST segment starts a packet */
        m->lacing[m->nseg++] = lv;
        memcpy(m->body + m->body_len, p, chunk);
        m->body_len += chunk;
        p += chunk;
        if (i == nseg_total - 1) { m->page_gp = gp; m->page_gp_valid = 1; m->pkts_done++; }
    }
    m->granule = gp;
    m->more_pkts = 0;
    if (eos) m->eos = 1;
    if (!m->bos_done) total += oggmux_flush(m, out + total, 1);   /* ID header page */
    return total;
}
