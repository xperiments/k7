#include "mcp.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "http_server.h"
#include "mini_icecast.h"
#include "snapshot.h"

#define TAG "mcp"

#ifndef XTAPE_MCP_CONTROL
#define XTAPE_MCP_CONTROL 0
#endif
#if XTAPE_MCP_CONTROL
#define MCP_CONTROL 1
#else
#define MCP_CONTROL 0
#endif

#define MCP_VERSION       "2025-11-25"
#define MCP_VERSION_ALT1  "2025-06-18"
#define MCP_VERSION_ALT2  "2025-03-26"
#define MCP_TEXT_MAX      (128u * 1024u)
#define MCP_TOKENS        128

typedef enum {
    JT_OBJECT = 1,
    JT_ARRAY,
    JT_STRING,
    JT_PRIMITIVE,
} jtok_type_t;

typedef struct {
    uint8_t type;
    uint8_t pad[3];
    int start;
    int end;
    int parent;
} jtok_t;

typedef struct {
    const char *s;
    size_t      n;
    size_t      pos;
    jtok_t     *t;
    int         nt;
    int         cap;
} jparser_t;

typedef struct {
    char  *p;
    size_t cap;
    size_t n;
    bool   ok;
} w_t;

static const library_t *L;
static const snapshot_t *SN;      /* NULL when this device has no Snapshot */
static snapshot_result_t SN_state = SNAPSHOT_NO_PARTITION;
static const char *SN_reason = "";
static uint32_t L_scan_ms;
static mcp_local_ip_fn L_local_ip;
static mcp_rescan_fn L_rescan;
static uint16_t L_port = 8000;

/* --------------------------------------------------------------- JSON read */

static void jp_ws(jparser_t *j)
{
    while (j->pos < j->n) {
        char c = j->s[j->pos];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') j->pos++;
        else break;
    }
}

static int jp_add(jparser_t *j, jtok_type_t type, int start, int parent)
{
    if (j->nt >= j->cap) return -1;
    int i = j->nt++;
    j->t[i].type = (uint8_t)type;
    j->t[i].start = start;
    j->t[i].end = start;
    j->t[i].parent = parent;
    return i;
}

static bool jp_hex4(jparser_t *j, uint32_t *out)
{
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        if (j->pos >= j->n) return false;
        char c = j->s[j->pos++];
        int d = (c >= '0' && c <= '9') ? c - '0'
              : (c >= 'a' && c <= 'f') ? c - 'a' + 10
              : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
        if (d < 0) return false;
        v = v * 16 + (uint32_t)d;
    }
    *out = v;
    return true;
}

static bool jp_string(jparser_t *j, int parent)
{
    size_t start = j->pos + 1;
    int tok = jp_add(j, JT_STRING, (int)start, parent);
    if (tok < 0) return false;
    j->pos++;
    while (j->pos < j->n) {
        unsigned char c = (unsigned char)j->s[j->pos];
        if (c == '"') {
            j->t[tok].end = (int)j->pos;
            j->pos++;
            return true;
        }
        if (c < 0x20) return false;
        if (c != '\\') {
            j->pos++;
            continue;
        }
        j->pos++;
        if (j->pos >= j->n) return false;
        char e = j->s[j->pos++];
        if (e == 'u') {
            uint32_t cp;
            if (!jp_hex4(j, &cp)) return false;
            if (cp >= 0xD800 && cp <= 0xDBFF) {
                if (j->pos + 1 >= j->n || j->s[j->pos] != '\\' || j->s[j->pos + 1] != 'u')
                    return false;
                j->pos += 2;
                uint32_t low;
                if (!jp_hex4(j, &low) || low < 0xDC00 || low > 0xDFFF) return false;
            } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                return false;
            }
        } else if (!strchr("\"\\/bfnrt", e)) {
            return false;
        }
    }
    return false;
}

static bool jp_value(jparser_t *j, int parent, int depth);

static bool jp_object(jparser_t *j, int parent, int depth)
{
    int tok = jp_add(j, JT_OBJECT, (int)j->pos, parent);
    if (tok < 0) return false;
    j->pos++;
    jp_ws(j);
    if (j->pos < j->n && j->s[j->pos] == '}') {
        j->pos++;
        j->t[tok].end = (int)j->pos;
        return true;
    }
    for (;;) {
        jp_ws(j);
        if (j->pos >= j->n || j->s[j->pos] != '"') return false;
        if (!jp_string(j, tok)) return false;
        jp_ws(j);
        if (j->pos >= j->n || j->s[j->pos] != ':') return false;
        j->pos++;
        if (!jp_value(j, tok, depth + 1)) return false;
        jp_ws(j);
        if (j->pos >= j->n) return false;
        if (j->s[j->pos] == ',') {
            j->pos++;
            continue;
        }
        if (j->s[j->pos] == '}') {
            j->pos++;
            j->t[tok].end = (int)j->pos;
            return true;
        }
        return false;
    }
}

static bool jp_array(jparser_t *j, int parent, int depth)
{
    int tok = jp_add(j, JT_ARRAY, (int)j->pos, parent);
    if (tok < 0) return false;
    j->pos++;
    jp_ws(j);
    if (j->pos < j->n && j->s[j->pos] == ']') {
        j->pos++;
        j->t[tok].end = (int)j->pos;
        return true;
    }
    for (;;) {
        if (!jp_value(j, tok, depth + 1)) return false;
        jp_ws(j);
        if (j->pos >= j->n) return false;
        if (j->s[j->pos] == ',') {
            j->pos++;
            continue;
        }
        if (j->s[j->pos] == ']') {
            j->pos++;
            j->t[tok].end = (int)j->pos;
            return true;
        }
        return false;
    }
}

static bool number_syntax(const char *s, size_t n)
{
    size_t i = 0;
    if (i < n && s[i] == '-') i++;
    if (i >= n) return false;
    if (s[i] == '0') {
        i++;
    } else if (s[i] >= '1' && s[i] <= '9') {
        while (i < n && s[i] >= '0' && s[i] <= '9') i++;
    } else {
        return false;
    }
    if (i < n && s[i] == '.') {
        i++;
        if (i >= n || s[i] < '0' || s[i] > '9') return false;
        while (i < n && s[i] >= '0' && s[i] <= '9') i++;
    }
    if (i < n && (s[i] == 'e' || s[i] == 'E')) {
        i++;
        if (i < n && (s[i] == '+' || s[i] == '-')) i++;
        if (i >= n || s[i] < '0' || s[i] > '9') return false;
        while (i < n && s[i] >= '0' && s[i] <= '9') i++;
    }
    return i == n;
}

static bool jp_primitive(jparser_t *j, int parent)
{
    int start = (int)j->pos;
    while (j->pos < j->n) {
        char c = j->s[j->pos];
        if (c == ',' || c == '}' || c == ']' || c == ' ' || c == '\t' ||
            c == '\r' || c == '\n')
            break;
        j->pos++;
    }
    if (j->pos == (size_t)start) return false;
    const char *s = j->s + start;
    size_t n = j->pos - (size_t)start;
    bool literal = (n == 4 && memcmp(s, "true", 4) == 0) ||
                   (n == 5 && memcmp(s, "false", 5) == 0) ||
                   (n == 4 && memcmp(s, "null", 4) == 0);
    if (!literal && !number_syntax(s, n)) return false;
    int tok = jp_add(j, JT_PRIMITIVE, start, parent);
    if (tok < 0) return false;
    j->t[tok].end = (int)j->pos;
    return true;
}

static bool jp_value(jparser_t *j, int parent, int depth)
{
    if (depth > 16) return false;
    jp_ws(j);
    if (j->pos >= j->n) return false;
    char c = j->s[j->pos];
    if (c == '{') return jp_object(j, parent, depth);
    if (c == '[') return jp_array(j, parent, depth);
    if (c == '"') return jp_string(j, parent);
    return jp_primitive(j, parent);
}

static bool jp_parse(const char *body, jtok_t *tokens, int cap, int *out_nt)
{
    jparser_t j = {.s = body, .n = strlen(body), .pos = 0, .t = tokens, .nt = 0, .cap = cap};
    jp_ws(&j);
    if (!jp_value(&j, -1, 0)) return false;
    jp_ws(&j);
    if (j.pos != j.n) return false;
    *out_nt = j.nt;
    return true;
}

static bool jtok_eq(const char *body, const jtok_t *t, const char *want)
{
    size_t n = (size_t)(t->end - t->start);
    return t->type == JT_STRING && n == strlen(want) &&
           memcmp(body + t->start, want, n) == 0;
}

static int jtok_member(const char *body, const jtok_t *t, int nt, int obj, const char *key)
{
    if (obj < 0 || obj >= nt) return -1;
    for (int i = obj + 1; i < nt; i++) {
        if (t[i].parent != obj || t[i].type != JT_STRING) continue;
        if (!jtok_eq(body, &t[i], key)) continue;
        for (int j = i + 1; j < nt; j++)
            if (t[j].parent == obj) return j;
        return -1;
    }
    return -1;
}

static bool jtok_unescape(const char *body, const jtok_t *t, char *out, size_t cap)
{
    size_t n = 0;
    for (int i = t->start; i < t->end; i++) {
        unsigned char c = (unsigned char)body[i];
        if (c != '\\') {
            if (n + 1 >= cap) return false;
            out[n++] = (char)c;
            continue;
        }
        if (++i >= t->end) return false;
        c = (unsigned char)body[i];
        switch (c) {
        case '"': case '\\': case '/': out[n++] = (char)c; break;
        case 'b': out[n++] = '\b'; break;
        case 'f': out[n++] = '\f'; break;
        case 'n': out[n++] = '\n'; break;
        case 'r': out[n++] = '\r'; break;
        case 't': out[n++] = '\t'; break;
        case 'u': {
            if (i + 4 >= t->end) return false;
            uint32_t cp = 0;
            for (int k = 0; k < 4; k++) {
                char h = body[++i];
                int d = (h >= '0' && h <= '9') ? h - '0'
                      : (h >= 'a' && h <= 'f') ? h - 'a' + 10
                      : (h >= 'A' && h <= 'F') ? h - 'A' + 10 : -1;
                if (d < 0) return false;
                cp = cp * 16 + (uint32_t)d;
            }
            if (cp >= 0xD800 && cp <= 0xDBFF) {
                if (i + 6 >= t->end || body[i + 1] != '\\' || body[i + 2] != 'u')
                    return false;
                i += 2;
                uint32_t low = 0;
                for (int k = 0; k < 4; k++) {
                    char h = body[++i];
                    int d = (h >= '0' && h <= '9') ? h - '0'
                          : (h >= 'a' && h <= 'f') ? h - 'a' + 10
                          : (h >= 'A' && h <= 'F') ? h - 'A' + 10 : -1;
                    if (d < 0) return false;
                    low = low * 16 + (uint32_t)d;
                }
                if (low < 0xDC00 || low > 0xDFFF) return false;
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
            } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                return false;
            }
            if (cp < 0x20) return false;
            if (cp < 0x80) {
                if (n + 1 >= cap) return false;
                out[n++] = (char)cp;
            } else if (cp < 0x800) {
                if (n + 2 >= cap) return false;
                out[n++] = (char)(0xC0 | (cp >> 6));
                out[n++] = (char)(0x80 | (cp & 0x3F));
            } else if (cp < 0x10000) {
                if (n + 3 >= cap) return false;
                out[n++] = (char)(0xE0 | (cp >> 12));
                out[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                out[n++] = (char)(0x80 | (cp & 0x3F));
            } else {
                if (n + 4 >= cap) return false;
                out[n++] = (char)(0xF0 | (cp >> 18));
                out[n++] = (char)(0x80 | ((cp >> 12) & 0x3F));
                out[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                out[n++] = (char)(0x80 | (cp & 0x3F));
            }
            break;
        }
        default: return false;
        }
        if (n + 1 >= cap) return false;
    }
    out[n] = 0;
    return true;
}

static bool jtok_int(const char *body, const jtok_t *t, int *out)
{
    if (t->type != JT_PRIMITIVE) return false;
    int v = 0;
    bool neg = false;
    int i = t->start;
    if (i < t->end && body[i] == '-') { neg = true; i++; }
    if (i >= t->end) return false;
    for (; i < t->end; i++) {
        char c = body[i];
        if (c < '0' || c > '9') return false;
        v = v * 10 + (c - '0');
        if (v > 100000) return false;
    }
    *out = neg ? -v : v;
    return true;
}

/* --------------------------------------------------------------- JSON write */

static void w_put(w_t *w, const char *s)
{
    size_t n = strlen(s);
    if (!w->ok) return;
    if (w->n + n >= w->cap) { w->ok = false; return; }
    memcpy(w->p + w->n, s, n);
    w->n += n;
    w->p[w->n] = 0;
}

static void w_puts(w_t *w, const char *s) { w_put(w, s); }

static void w_raw(w_t *w, const char *s, size_t n)
{
    if (!w->ok) return;
    if (w->n + n >= w->cap) { w->ok = false; return; }
    memcpy(w->p + w->n, s, n);
    w->n += n;
    w->p[w->n] = 0;
}

static void w_printf(w_t *w, const char *fmt, ...)
{
    char tmp[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= sizeof tmp) { w->ok = false; return; }
    w_raw(w, tmp, (size_t)n);
}

static void w_escaped(w_t *w, const char *s)
{
    static const char hex[] = "0123456789abcdef";
    for (; *s && w->ok; s++) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
        case '"': w_raw(w, "\\\"", 2); break;
        case '\\': w_raw(w, "\\\\", 2); break;
        default:
            if (c < 0x20) {
                char esc[6] = {'\\', 'u', '0', '0', hex[c >> 4], hex[c & 15]};
                w_raw(w, esc, sizeof esc);
            } else {
                w_raw(w, (const char *)&c, 1);
            }
            break;
        }
    }
}

static void w_escaped_n(w_t *w, const char *s, size_t n)
{
    char tmp[LIB_NAME_MAX + 1];
    if (n > LIB_NAME_MAX) n = LIB_NAME_MAX;
    memcpy(tmp, s, n);
    tmp[n] = 0;
    w_escaped(w, tmp);
}

static w_t w_make(char *buf, size_t cap) { return (w_t){buf, cap, 0, true}; }

/* ---------------------------------------------------------------- tools */

static const char *fmt_name(mi_fmt_t f)
{
    switch (f) {
    case MI_FMT_MP3: return "mp3";
    case MI_FMT_FLAC: return "flac";
    case MI_FMT_WAV: return "wav";
    default: return "none";
    }
}

static bool ascii_contains(const char *hay, const char *needle)
{
    size_t hn = strlen(hay), nn = strlen(needle);
    if (!nn || nn > hn) return false;
    for (size_t i = 0; i + nn <= hn; i++) {
        size_t k = 0;
        for (; k < nn; k++) {
            unsigned char a = (unsigned char)hay[i + k];
            unsigned char b = (unsigned char)needle[k];
            if (a >= 'A' && a <= 'Z') a = (unsigned char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (unsigned char)(b + 32);
            if (a != b) break;
        }
        if (k == nn) return true;
    }
    return false;
}

/* One track's measurements as JSON, or `null` when the Snapshot has no row for it.
 * An unknown value is `null`, not an absent key: "not measured" is an answer the
 * model can act on, and a missing key cannot be told from a typo. */
static void write_opt_string(w_t *w, const snapshot_t *sn, const snapshot_row_t *row,
                             const char *name, snapshot_string_t which)
{
    const char *v = snapshot_row_string(sn, row, which);
    if (!v[0]) return;
    w_printf(w, ",\"%s\":\"", name);
    w_escaped(w, v);
    w_put(w, "\"");
}

static void write_analysis(w_t *w, const snapshot_t *sn, const snapshot_row_t *row, bool have)
{
    if (!have || !sn) {
        w_puts(w, "null");
        return;
    }
    w_puts(w, "{\"bpm\":");
    if (row->bpm_x10)
        w_printf(w, "%u.%u", (unsigned)(row->bpm_x10 / 10u), (unsigned)(row->bpm_x10 % 10u));
    else
        w_puts(w, "null");

    w_puts(w, ",\"key\":");
    if (row->key >= 1 && row->key <= 24)
        w_printf(w, "\"%u%c\"", (unsigned)(((row->key - 1u) % 12u) + 1u),
                 row->key > 12 ? 'B' : 'A');
    else
        w_puts(w, "null");

    static const char *const pct_name[4] = {"energy", "danceability", "valence", "arousal"};
    uint8_t pct[4] = {row->energy, row->danceability, row->valence, row->arousal};
    for (int i = 0; i < 4; i++) {
        w_printf(w, ",\"%s\":", pct_name[i]);
        if (pct[i] == SNAPSHOT_UNKNOWN_PCT)
            w_puts(w, "null");
        else
            w_printf(w, "%u", (unsigned)pct[i]);
    }

    w_puts(w, ",\"mood\":[");
    bool first = true;
    for (unsigned i = 0; i < 5; i++) {
        if (!(row->mood_bits & (1u << i))) continue;
        if (!first) w_put(w, ",");
        first = false;
        w_printf(w, "\"%s\"", snapshot_mood_name(i));
    }
    w_puts(w, "]");

    write_opt_string(w, sn, row, "title", SNAPSHOT_STRING_TITLE);
    write_opt_string(w, sn, row, "artist", SNAPSHOT_STRING_ARTIST);
    write_opt_string(w, sn, row, "album", SNAPSHOT_STRING_ALBUM);
    write_opt_string(w, sn, row, "genre", SNAPSHOT_STRING_GENRE);

    static const char *const src_name[4] = {"bpm", "key", "mood", "energy"};
    uint8_t src_bit[4] = {SNAPSHOT_SRC_BPM_TAGGED, SNAPSHOT_SRC_KEY_TAGGED,
                          SNAPSHOT_SRC_MOOD_TAGGED, SNAPSHOT_SRC_ENERGY_TAGGED};
    w_puts(w, ",\"tagged\":[");
    first = true;
    for (int i = 0; i < 4; i++) {
        if (!(row->source_flags & src_bit[i])) continue;
        if (!first) w_put(w, ",");
        first = false;
        w_printf(w, "\"%s\"", src_name[i]);
    }
    w_puts(w, "]");

    w_puts(w, ",\"confidence\":");
    if (row->confidence == SNAPSHOT_UNKNOWN_PCT)
        w_puts(w, "null");
    else
        w_printf(w, "%u", (unsigned)row->confidence);
    w_puts(w, "}");
}

/* The row for a Library track, when this device has a Snapshot and the file still
 * matches it. A file over 4 GiB cannot be represented in a row's size, so it is
 * unanalysed rather than matched on a truncated size. */
static bool track_row(const library_t *lib, lib_node_t n, snapshot_row_t *row)
{
    if (!SN) return false;
    char rel[LIB_PATH_MAX];
    if (library_relpath(lib, n, rel, sizeof rel) == (size_t)-1) return false;
    uint64_t size = library_size(lib, n);
    if (size > UINT32_MAX) return false;
    return snapshot_find(SN, snapshot_path_hash(rel), (uint32_t)size, row);
}

static void write_entry(w_t *w, const library_t *lib, lib_node_t n)
{
    uint16_t nlen = 0;
    const char *name = library_name(lib, n, &nlen);
    char rel[LIB_PATH_MAX];
    if (library_relpath(lib, n, rel, sizeof rel) == (size_t)-1) rel[0] = 0;
    w_puts(w, "{\"name\":\"");
    w_escaped_n(w, name, nlen);
    w_puts(w, "\",\"path\":\"");
    w_escaped(w, rel);
    w_puts(w, "\",\"type\":\"");
    w_puts(w, library_kind(lib, n) == LIB_DIR ? "dir" : "track");
    w_puts(w, "\"");
    if (library_kind(lib, n) == LIB_TRACK) {
        w_printf(w, ",\"size\":%llu,\"format\":\"%s\"",
                 (unsigned long long)library_size(lib, n), fmt_name(library_format(lib, n)));
    }
    w_puts(w, "}");
}

static void write_track(w_t *w, const library_t *lib, lib_node_t n)
{
    uint16_t nlen = 0;
    const char *name = library_name(lib, n, &nlen);
    char rel[LIB_PATH_MAX];
    if (library_relpath(lib, n, rel, sizeof rel) == (size_t)-1) rel[0] = 0;
    w_puts(w, "{\"name\":\"");
    w_escaped_n(w, name, nlen);
    w_puts(w, "\",\"path\":\"");
    w_escaped(w, rel);
    w_puts(w, "\",\"size\":");
    w_printf(w, "%llu", (unsigned long long)library_size(lib, n));
    w_printf(w, ",\"format\":\"%s\",\"track_index\":%u,\"analysis\":",
             fmt_name(library_format(lib, n)), (unsigned)library_track_index(lib, n));

    snapshot_row_t row;
    bool have = track_row(lib, n, &row);
    write_analysis(w, SN, &row, have);
    w_puts(w, "}");
}

static void write_stats(w_t *w, const library_t *lib)
{
    if (!lib) {
        w_puts(w, "{\"available\":false}");
        return;
    }
    /* Coverage is the number that bounds every answer: it is counted against the
     * current Library, so a row for a track that no longer exists is not coverage. */
    unsigned tracks = library_track_count(lib);
    unsigned analysed = 0, tagged_bpm = 0, tagged_key = 0, tagged_mood = 0, tagged_energy = 0;
    if (SN) {
        for (uint32_t i = 0; i < tracks; i++) {
            snapshot_row_t row;
            if (!track_row(lib, library_track_at(lib, i), &row)) continue;
            analysed++;
            if (row.source_flags & SNAPSHOT_SRC_BPM_TAGGED) tagged_bpm++;
            if (row.source_flags & SNAPSHOT_SRC_KEY_TAGGED) tagged_key++;
            if (row.source_flags & SNAPSHOT_SRC_MOOD_TAGGED) tagged_mood++;
            if (row.source_flags & SNAPSHOT_SRC_ENERGY_TAGGED) tagged_energy++;
        }
    }
    const char *state = SN ? "ok"
                      : SN_state == SNAPSHOT_NO_PARTITION ? "no_partition"
                      : SN_state == SNAPSHOT_NO_UPDATE ? "no_update"
                      : "invalid";

    w_printf(w, "{\"available\":true,\"state\":\"%s\",\"tracks\":%u,\"directories\":%u,"
                "\"bytes\":%llu,\"generation\":%u,\"truncated\":%s,\"scan_ms\":%u",
             library_state_name(library_state(lib)), tracks,
             (unsigned)library_dir_count(lib), (unsigned long long)library_total_bytes(lib),
             (unsigned)library_generation(lib), library_truncated(lib) ? "true" : "false",
             (unsigned)L_scan_ms);
    w_printf(w, ",\"snapshot\":{\"state\":\"%s\",\"format_version\":%u",
             state, (unsigned)SNAPSHOT_VERSION);
    if (SN) {
        w_printf(w, ",\"generation\":%u,\"rows\":%u,\"analyser\":%u,\"model_set\":\"%llu\"",
                 (unsigned)snapshot_generation(SN), (unsigned)snapshot_rows(SN),
                 (unsigned)snapshot_analyser(SN), (unsigned long long)snapshot_model_set(SN));
    } else if (SN_reason[0]) {
        w_puts(w, ",\"reason\":\"");
        w_escaped(w, SN_reason);
        w_puts(w, "\"");
    }
    w_printf(w, ",\"coverage\":{\"tracks\":%u,\"analysed\":%u,\"percent\":%u},"
                "\"tagged\":{\"bpm\":%u,\"key\":%u,\"mood\":%u,\"energy\":%u}}}",
             tracks, analysed, tracks ? (analysed * 100u) / tracks : 0u, tagged_bpm, tagged_key,
             tagged_mood, tagged_energy);
}

typedef struct {
    w_t     *w;
    const library_t *lib;
    unsigned limit;
    unsigned count;
    bool     truncated;
    bool     first;
} list_ctx_t;

static void list_children(list_ctx_t *c, lib_node_t dir, unsigned depth)
{
    if (depth == 0 || c->truncated) return;
    uint32_t n = library_child_count(c->lib, dir);
    for (uint32_t i = 0; i < n; i++) {
        if (c->count >= c->limit) { c->truncated = true; return; }
        lib_node_t child = library_child(c->lib, dir, i);
        if (!c->first) w_put(c->w, ",");
        c->first = false;
        write_entry(c->w, c->lib, child);
        c->count++;
        if (library_kind(c->lib, child) == LIB_DIR && depth > 1)
            list_children(c, child, depth - 1);
    }
}

static bool arg_string(const char *body, const jtok_t *t, int nt, int args,
                       const char *key, char *out, size_t cap, bool required)
{
    int v = jtok_member(body, t, nt, args, key);
    if (v < 0) return !required;
    return t[v].type == JT_STRING && jtok_unescape(body, &t[v], out, cap);
}

static bool arg_int(const char *body, const jtok_t *t, int nt, int args,
                    const char *key, int *out, int def)
{
    int v = jtok_member(body, t, nt, args, key);
    if (v < 0) { *out = def; return true; }
    return jtok_int(body, &t[v], out);
}

#if MCP_CONTROL
static bool control_play(w_t *w, const library_t *lib, const char *body, const jtok_t *t,
                         int nt, int args)
{
    char path[LIB_PATH_MAX] = {0};
    if (!arg_string(body, t, nt, args, "path", path, sizeof path, false)) {
        w_puts(w, "path is invalid or too long");
        return false;
    }
    if (path[0]) {
        if (!mini_icecast_play(path)) {
            w_puts(w, "no such track");
            return false;
        }
        w_puts(w, "{\"accepted\":true}");
        return true;
    }

    int id = -1, generation = -1;
    if (!arg_int(body, t, nt, args, "id", &id, -1) ||
        !arg_int(body, t, nt, args, "generation", &generation, -1) ||
        id < 0 || generation < 0) {
        w_puts(w, "path, or id with generation, is required");
        return false;
    }
    if (!lib || generation != (int)library_generation(lib) ||
        (uint32_t)id >= library_node_count(lib)) {
        w_puts(w, "the generation or id is not current");
        return false;
    }
    lib_node_t n = (lib_node_t)id;
    if (library_kind(lib, n) != LIB_TRACK) {
        w_puts(w, "the id is not a track");
        return false;
    }
    char rel[LIB_PATH_MAX];
    if (library_relpath(lib, n, rel, sizeof rel) == (size_t)-1 ||
        !mini_icecast_play(rel)) {
        w_puts(w, "the track could not be played");
        return false;
    }
    w_puts(w, "{\"accepted\":true}");
    return true;
}

static bool control_skip(w_t *w)
{
    mini_icecast_skip();
    w_puts(w, "{\"accepted\":true}");
    return true;
}

static bool control_prev(w_t *w)
{
    mini_icecast_prev();
    w_puts(w, "{\"accepted\":true}");
    return true;
}

static bool control_rescan(w_t *w)
{
    if (!L_rescan) {
        w_puts(w, "a rescan is not available in this build");
        return false;
    }
    esp_err_t e = L_rescan();
    if (e == ESP_OK) {
        w_puts(w, "{\"started\":true}");
        return true;
    }
    if (e == ESP_ERR_INVALID_STATE) {
        w_puts(w, "a scan is already running");
        return false;
    }
    w_puts(w, "the scan could not be started");
    return false;
}
#endif

static bool tool_library_list(w_t *w, const library_t *lib, const char *body, const jtok_t *t, int nt, int args)
{
    char path[LIB_PATH_MAX] = {0};
    if (!arg_string(body, t, nt, args, "path", path, sizeof path, false)) {
        w_puts(w, "bad path argument");
        return false;
    }
    int depth = 1, limit = 200;
    if (!arg_int(body, t, nt, args, "depth", &depth, 1) || depth < 1 || depth > 8 ||
        !arg_int(body, t, nt, args, "limit", &limit, 200) || limit < 1 || limit > 200) {
        w_puts(w, "depth must be 1..8 and limit 1..200");
        return false;
    }
    if (!lib) { w_puts(w, "no library generation"); return false; }
    lib_node_t n = path[0] ? library_find(lib, path) : library_root(lib);
    if (n == LIB_NODE_NONE) { w_puts(w, "no such directory"); return false; }
    if (library_kind(lib, n) != LIB_DIR) { w_puts(w, "not a directory"); return false; }

    w_puts(w, "{\"path\":\"");
    w_escaped(w, path);
    w_puts(w, "\",\"entries\":[");
    list_ctx_t c = {w, lib, (unsigned)limit, 0, false, true};
    list_children(&c, n, (unsigned)depth);
    w_printf(w, "],\"truncated\":%s}", c.truncated ? "true" : "false");
    return true;
}

/* ------------------------------------------------------- search arguments */

static int jtok_array_item(const jtok_t *t, int nt, int arr, int k)
{
    if (arr < 0 || arr >= nt || t[arr].type != JT_ARRAY) return -1;
    int seen = 0;
    for (int i = arr + 1; i < nt; i++) {
        if (t[i].parent != arr) continue;
        if (seen++ == k) return i;
    }
    return -1;
}

/* BPM arrives as a decimal and the filter compares tenths, so "120.5" is 1205. */
static bool arg_bpm(const char *body, const jtok_t *t, int nt, int args, const char *key,
                    uint16_t *out)
{
    int v = jtok_member(body, t, nt, args, key);
    if (v < 0) return true;
    if (t[v].type != JT_PRIMITIVE) return false;
    size_t n = (size_t)(t[v].end - t[v].start);
    char buf[24];
    if (n == 0 || n >= sizeof buf) return false;
    memcpy(buf, body + t[v].start, n);
    buf[n] = 0;
    char *endp = NULL;
    double d = strtod(buf, &endp);
    if (!endp || *endp || d < 0 || d > 400) return false;
    *out = (uint16_t)(d * 10.0 + 0.5);
    return true;
}

/* A string array of keys, each Camelot or a note name. */
static bool arg_keys(const char *body, const jtok_t *t, int nt, int args, uint16_t *mask)
{
    int arr = jtok_member(body, t, nt, args, "key");
    if (arr < 0) return true;
    if (t[arr].type != JT_ARRAY) return false;
    for (int k = 0; k < 24; k++) {
        int item = jtok_array_item(t, nt, arr, k);
        if (item < 0) break;
        if (t[item].type != JT_STRING) return false;
        char one[24];
        if (!jtok_unescape(body, &t[item], one, sizeof one)) return false;
        uint8_t code = snapshot_key_parse(one);
        if (!code) return false;
        *mask |= (uint16_t)(1u << (code - 1));
    }
    return true;
}

static bool arg_moods(const char *body, const jtok_t *t, int nt, int args, uint8_t *mask)
{
    int arr = jtok_member(body, t, nt, args, "mood");
    if (arr < 0) return true;
    if (t[arr].type != JT_ARRAY) return false;
    for (int k = 0; k < 8; k++) {
        int item = jtok_array_item(t, nt, arr, k);
        if (item < 0) break;
        if (t[item].type != JT_STRING) return false;
        char one[24];
        if (!jtok_unescape(body, &t[item], one, sizeof one)) return false;
        int hit = snapshot_mood_from_name(one);
        if (hit < 0) return false;
        *mask |= (uint8_t)(1u << hit);
    }
    return true;
}

/* Percentages are 0xFF for "unset"; an absent key must not write a bound, because 0 is
 * a legal one. */
static bool percent_filter(const char *body, const jtok_t *t, int nt, int args,
                           const char *min_key, const char *max_key, uint8_t *lo, uint8_t *hi)
{
    int tok = jtok_member(body, t, nt, args, min_key);
    if (tok >= 0) {
        int v = 0;
        if (!jtok_int(body, &t[tok], &v) || v < 0 || v > 100) return false;
        *lo = (uint8_t)v;
    }
    tok = jtok_member(body, t, nt, args, max_key);
    if (tok >= 0) {
        int v = 0;
        if (!jtok_int(body, &t[tok], &v) || v < 0 || v > 100) return false;
        *hi = (uint8_t)v;
    }
    return true;
}

/* ------------------------------------------------------------- search tool */

static bool tool_library_search(w_t *w, const library_t *lib, const char *body, const jtok_t *t,
                                int nt, int args)
{
    snapshot_filter_t f;
    snapshot_filter_defaults(&f);
    char query[256] = {0};
    char genre[64] = {0};
    int limit = 50, offset = 0;

    if (!arg_string(body, t, nt, args, "query", query, sizeof query, false)) {
        w_puts(w, "query must fit 255 bytes");
        return false;
    }
    if (!arg_string(body, t, nt, args, "genre", genre, sizeof genre, false)) {
        w_puts(w, "genre must fit 63 bytes");
        return false;
    }
    if (!arg_int(body, t, nt, args, "limit", &limit, 50) || limit < 1 || limit > 100 ||
        !arg_int(body, t, nt, args, "offset", &offset, 0) || offset < 0 || offset > 199 ||
        offset + limit > 200) {
        w_puts(w, "limit must be 1..100, offset 0..199, and offset+limit at most 200");
        return false;
    }
    if (!arg_bpm(body, t, nt, args, "bpm_min", &f.bpm_min_x10) ||
        !arg_bpm(body, t, nt, args, "bpm_max", &f.bpm_max_x10)) {
        w_puts(w, "bpm_min and bpm_max must be numbers");
        return false;
    }
    if (!percent_filter(body, t, nt, args, "energy_min", "energy_max", &f.energy_min,
                        &f.energy_max) ||
        !percent_filter(body, t, nt, args, "danceability_min", "danceability_max",
                        &f.danceability_min, &f.danceability_max) ||
        !percent_filter(body, t, nt, args, "valence_min", "valence_max", &f.valence_min,
                        &f.valence_max) ||
        !percent_filter(body, t, nt, args, "arousal_min", "arousal_max", &f.arousal_min,
                        &f.arousal_max)) {
        w_puts(w, "percentage bounds are 0..100");
        return false;
    }
    if (!arg_keys(body, t, nt, args, &f.key_mask) || !arg_moods(body, t, nt, args, &f.mood_mask)) {
        w_puts(w, "key must be a list of Camelot codes or note names; "
                  "mood a list of happy, sad, aggressive, relaxed, party");
        return false;
    }
    char sort[16] = {0};
    if (!arg_string(body, t, nt, args, "sort", sort, sizeof sort, false)) {
        w_puts(w, "sort is invalid");
        return false;
    }
    if (sort[0] && !snapshot_sort_parse(sort, &f.sort)) {
        w_puts(w, "sort must be path, title, bpm, energy or danceability");
        return false;
    }
    if (query[0]) f.query = query;
    if (genre[0]) f.genre = genre;

    bool wants_analysis = f.bpm_min_x10 || f.bpm_max_x10 || f.key_mask || f.mood_mask ||
                          f.energy_min != SNAPSHOT_UNKNOWN_PCT ||
                          f.energy_max != SNAPSHOT_UNKNOWN_PCT ||
                          f.danceability_min != SNAPSHOT_UNKNOWN_PCT ||
                          f.danceability_max != SNAPSHOT_UNKNOWN_PCT ||
                          f.valence_min != SNAPSHOT_UNKNOWN_PCT ||
                          f.valence_max != SNAPSHOT_UNKNOWN_PCT ||
                          f.arousal_min != SNAPSHOT_UNKNOWN_PCT ||
                          f.arousal_max != SNAPSHOT_UNKNOWN_PCT || f.genre;
    if (wants_analysis && !SN) {
        w_puts(w, "this device has no snapshot: name search works, measurements do not");
        return false;
    }

    if (!lib) { w_puts(w, "no library generation"); return false; }

    snapshot_hits_t *hits = snapshot_hits_new((uint32_t)(offset + limit));
    if (!hits) { w_puts(w, "out of memory"); return false; }

    unsigned matched = 0;
    uint32_t total = library_track_count(lib);
    for (uint32_t i = 0; i < total; i++) {
        lib_node_t n = library_track_at(lib, i);
        char rel[LIB_PATH_MAX];
        if (library_relpath(lib, n, rel, sizeof rel) == (size_t)-1) continue;
        int64_t key = 0;
        const char *str = NULL;
        bool ok;
        if (SN) {
            ok = snapshot_matches(SN, &f, rel, (uint32_t)library_size(lib, n), &key, &str);
        } else {
            ok = query[0] && ascii_contains(rel, query);
        }
        if (!ok) continue;
        matched++;
        snapshot_hits_add(hits, i, key, str);
    }

    w_puts(w, "{\"query\":\"");
    w_escaped(w, query);
    w_printf(w, "\",\"matched\":%u,\"tracks\":[", matched);
    bool first = true;
    uint32_t count = snapshot_hits_count(hits);
    for (uint32_t k = (uint32_t)offset; k < count; k++) {
        if (!first) w_put(w, ",");
        first = false;
        write_track(w, lib, library_track_at(lib, snapshot_hits_index(hits, k)));
    }
    w_printf(w, "],\"truncated\":%s}", matched > (unsigned)(offset + limit) ? "true" : "false");
    snapshot_hits_free(hits);
    return true;
}

static bool tool_library_track(w_t *w, const library_t *lib, const char *body, const jtok_t *t,
                               int nt, int args)
{
    if (!lib) { w_puts(w, "no library generation"); return false; }

    char path[LIB_PATH_MAX] = {0};
    if (!arg_string(body, t, nt, args, "path", path, sizeof path, false)) {
        w_puts(w, "path must fit the path ceiling");
        return false;
    }
    lib_node_t n = LIB_NODE_NONE;
    if (path[0]) {
        n = library_find(lib, path);
    } else {
        /* The pair a previous result carried: a node id is only meaningful inside the
         * generation it came from, so both are required. */
        int id = -1, generation = -1;
        if (!arg_int(body, t, nt, args, "id", &id, -1) ||
            !arg_int(body, t, nt, args, "generation", &generation, -1) || id < 0 ||
            generation < 0) {
            w_puts(w, "path, or id with generation, is required");
            return false;
        }
        if (generation != (int)library_generation(lib) || (uint32_t)id >= library_node_count(lib)) {
            w_puts(w, "the generation or id is not current");
            return false;
        }
        n = (lib_node_t)id;
    }
    if (n == LIB_NODE_NONE || library_kind(lib, n) != LIB_TRACK) {
        w_puts(w, "no such track");
        return false;
    }
    write_track(w, lib, n);
    return true;
}

static void tool_player_status(w_t *w)
{
    mini_icecast_status_t st;
    mini_icecast_status(&st);
    w_puts(w, "{\"title\":\"");
    w_escaped(w, st.title);
    w_puts(w, "\",\"file\":\"");
    w_escaped(w, st.file);
    w_puts(w, "\",\"path\":\"");
    w_escaped(w, st.path);
    w_printf(w, "\",\"elapsed_ms\":%u,\"duration_ms\":%u,\"level_l\":%.3f,\"level_r\":%.3f,"
                "\"library_count\":%u,\"generation\":%u,\"track_index\":%u}",
             (unsigned)st.elapsed_ms, (unsigned)st.duration_ms, (double)st.level_l,
             (double)st.level_r, (unsigned)st.library_count, (unsigned)st.generation,
             (unsigned)st.track_index);
}

#if !MCP_CONTROL
static const char TOOLS_READONLY[] =
    "{\"tools\":["
    "{\"name\":\"library_stats\",\"description\":\"Counts, bytes, generation and truncation of the boot-built library.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{}},\"annotations\":{\"readOnlyHint\":true}},"
    "{\"name\":\"library_list\",\"description\":\"List a directory in playlist order.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},"
    "\"depth\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":8},"
    "\"limit\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":200}}},"
    "\"annotations\":{\"readOnlyHint\":true}},"
    "{\"name\":\"library_search\",\"description\":\"Find tracks by name and/or musical properties. "
    "Every filter is optional and they are ANDed. A track with no measurements matches name "
    "searches only, and never matches a range.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
    "\"query\":{\"type\":\"string\",\"description\":\"case-insensitive substring of the path, title, artist, album or genre\"},"
    "\"genre\":{\"type\":\"string\"},"
    "\"bpm_min\":{\"type\":\"number\"},\"bpm_max\":{\"type\":\"number\"},"
    "\"energy_min\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":100},"
    "\"energy_max\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":100},"
    "\"danceability_min\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":100},"
    "\"danceability_max\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":100},"
    "\"valence_min\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":100},"
    "\"valence_max\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":100},"
    "\"arousal_min\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":100},"
    "\"arousal_max\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":100},"
    "\"key\":{\"type\":\"array\",\"items\":{\"type\":\"string\"},\"description\":\"Camelot codes (8A) or note names (Am, A minor, F#m)\"},"
    "\"mood\":{\"type\":\"array\",\"items\":{\"type\":\"string\",\"enum\":[\"happy\",\"sad\",\"aggressive\",\"relaxed\",\"party\"]}},"
    "\"sort\":{\"type\":\"string\",\"enum\":[\"path\",\"title\",\"bpm\",\"energy\",\"danceability\"]},"
    "\"offset\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":199},"
    "\"limit\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":100}}},"
    "\"annotations\":{\"readOnlyHint\":true}},"
    "{\"name\":\"library_track\",\"description\":\"One track's metadata and its measurements, "
    "by relative path or by id with generation.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},"
    "\"id\":{\"type\":\"integer\",\"minimum\":0},"
    "\"generation\":{\"type\":\"integer\",\"minimum\":1}}},"
    "\"annotations\":{\"readOnlyHint\":true}},"
    "{\"name\":\"player_status\",\"description\":\"What the live broadcast is playing.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{}},\"annotations\":{\"readOnlyHint\":true}}"
    "]}";
#endif

#if MCP_CONTROL
static const char TOOLS_CONTROL[] =
    "{\"tools\":["
    "{\"name\":\"library_stats\",\"description\":\"Counts, bytes, generation and truncation of the boot-built library.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{}},\"annotations\":{\"readOnlyHint\":true}},"
    "{\"name\":\"library_list\",\"description\":\"List a directory in playlist order.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},"
    "\"depth\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":8},"
    "\"limit\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":200}}},"
    "\"annotations\":{\"readOnlyHint\":true}},"
    "{\"name\":\"library_search\",\"description\":\"Find tracks by name and/or musical properties. "
    "Every filter is optional and they are ANDed. A track with no measurements matches name "
    "searches only, and never matches a range.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
    "\"query\":{\"type\":\"string\",\"description\":\"case-insensitive substring of the path, title, artist, album or genre\"},"
    "\"genre\":{\"type\":\"string\"},"
    "\"bpm_min\":{\"type\":\"number\"},\"bpm_max\":{\"type\":\"number\"},"
    "\"energy_min\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":100},"
    "\"energy_max\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":100},"
    "\"danceability_min\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":100},"
    "\"danceability_max\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":100},"
    "\"valence_min\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":100},"
    "\"valence_max\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":100},"
    "\"arousal_min\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":100},"
    "\"arousal_max\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":100},"
    "\"key\":{\"type\":\"array\",\"items\":{\"type\":\"string\"},\"description\":\"Camelot codes (8A) or note names (Am, A minor, F#m)\"},"
    "\"mood\":{\"type\":\"array\",\"items\":{\"type\":\"string\",\"enum\":[\"happy\",\"sad\",\"aggressive\",\"relaxed\",\"party\"]}},"
    "\"sort\":{\"type\":\"string\",\"enum\":[\"path\",\"title\",\"bpm\",\"energy\",\"danceability\"]},"
    "\"offset\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":199},"
    "\"limit\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":100}}},"
    "\"annotations\":{\"readOnlyHint\":true}},"
    "{\"name\":\"library_track\",\"description\":\"One track's metadata and its measurements, "
    "by relative path or by id with generation.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},"
    "\"id\":{\"type\":\"integer\",\"minimum\":0},"
    "\"generation\":{\"type\":\"integer\",\"minimum\":1}}},"
    "\"annotations\":{\"readOnlyHint\":true}},"
    "{\"name\":\"player_status\",\"description\":\"What the live broadcast is playing.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{}},\"annotations\":{\"readOnlyHint\":true}},"
    "{\"name\":\"player_play\",\"description\":\"Play a Track now, by relative path or by id and generation.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},"
    "\"id\":{\"type\":\"integer\",\"minimum\":0},"
    "\"generation\":{\"type\":\"integer\",\"minimum\":1}}},"
    "\"annotations\":{\"readOnlyHint\":false,\"destructiveHint\":false}},"
    "{\"name\":\"player_skip\",\"description\":\"Advance to the next Track.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{}},"
    "\"annotations\":{\"readOnlyHint\":false,\"destructiveHint\":false}},"
    "{\"name\":\"player_prev\",\"description\":\"Go back to the previous Track.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{}},"
    "\"annotations\":{\"readOnlyHint\":false,\"destructiveHint\":false}},"
    "{\"name\":\"library_rescan\",\"description\":\"Start a background Library rescan.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{}},"
    "\"annotations\":{\"readOnlyHint\":false,\"destructiveHint\":false}}"
    "]}";
#endif

/* --------------------------------------------------------------- responses */

#define MCP_HEADER "MCP-Protocol-Version: " MCP_VERSION "\r\n"

static void send_body(int fd, int status, const char *body, size_t len)
{
    if (http_send_headers(fd, status, "application/json", len, MCP_HEADER) == ESP_OK)
        http_send_all(fd, body, len);
}

static void send_static_error(int fd, int status, int code, const char *message)
{
    char body[256];
    int n = snprintf(body, sizeof body,
                     "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":%d,\"message\":\"%s\"}}",
                     code, message);
    if (n < 0) n = 0;
    if ((size_t)n >= sizeof body) n = (int)sizeof body - 1;
    send_body(fd, status, body, (size_t)n);
}

static void write_id(w_t *w, const char *body, const jtok_t *id)
{
    if (!id) { w_put(w, "null"); return; }
    if (id->type == JT_STRING) {
        w_put(w, "\"");
        w_raw(w, body + id->start, (size_t)(id->end - id->start));
        w_put(w, "\"");
    } else {
        w_raw(w, body + id->start, (size_t)(id->end - id->start));
    }
}

static void rpc_error(int fd, int status, const char *body, const jtok_t *id,
                      int code, const char *message)
{
    size_t id_len = id ? (size_t)(id->end - id->start) : 4;
    size_t cap = 1024 + id_len + strlen(message) * 6;
    char *out = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!out) out = malloc(cap);
    if (!out) {
        send_static_error(fd, 500, -32603, "out of memory");
        return;
    }
    w_t w = w_make(out, cap);
    w_puts(&w, "{\"jsonrpc\":\"2.0\",\"id\":");
    write_id(&w, body, id);
    w_printf(&w, ",\"error\":{\"code\":%d,\"message\":\"", code);
    w_escaped(&w, message);
    w_puts(&w, "\"}}");
    if (w.ok) send_body(fd, status, out, w.n);
    else send_static_error(fd, 500, -32603, "response too large");
    free(out);
}

static void rpc_result(int fd, int status, const char *body, const jtok_t *id, const char *result)
{
    size_t id_len = id ? (size_t)(id->end - id->start) : 4;
    size_t cap = strlen(result) + id_len + 1024;
    char *out = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!out) out = malloc(cap);
    if (!out) {
        send_static_error(fd, 500, -32603, "out of memory");
        return;
    }
    w_t w = w_make(out, cap);
    w_puts(&w, "{\"jsonrpc\":\"2.0\",\"id\":");
    write_id(&w, body, id);
    w_puts(&w, ",\"result\":");
    w_puts(&w, result);
    w_put(&w, "}");
    if (w.ok) send_body(fd, status, out, w.n);
    else send_static_error(fd, 500, -32603, "response too large");
    free(out);
}

static void rpc_notification(int fd)
{
    http_send_headers(fd, 202, NULL, 0, MCP_HEADER);
}

static void rpc_tool(int fd, const char *body, const jtok_t *id, const char *text, bool is_error)
{
    size_t id_len = id ? (size_t)(id->end - id->start) : 4;
    size_t cap = strlen(text) * 2 + id_len + 2048;
    char *out = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!out) out = malloc(cap);
    if (!out) {
        send_static_error(fd, 500, -32603, "out of memory");
        return;
    }
    w_t w = w_make(out, cap);
    w_puts(&w, "{\"jsonrpc\":\"2.0\",\"id\":");
    write_id(&w, body, id);
    w_puts(&w, ",\"result\":{\"content\":[{\"type\":\"text\",\"text\":\"");
    w_escaped(&w, text);
    w_printf(&w, "\"}],\"isError\":%s}}", is_error ? "true" : "false");
    if (w.ok) send_body(fd, 200, out, w.n);
    else send_static_error(fd, 500, -32603, "response too large");
    free(out);
}

/* --------------------------------------------------------------- lifecycle */

static const char INSTRUCTIONS[] =
    "The xtape music library on the card. Use library_list and library_search to browse. "
    "Player tools are available only when control is enabled.";

static void initialize_result(w_t *w, const char *version)
{
    w_printf(w, "{\"protocolVersion\":\"%s\",\"capabilities\":{\"tools\":{\"listChanged\":false}},"
                "\"serverInfo\":{\"name\":\"xtape\",\"version\":\"1.0.0\"},"
                "\"instructions\":\"", version);
    w_escaped(w, INSTRUCTIONS);
    w_puts(w, "\"}");
}

static bool supported_version(const char *v)
{
    return strcmp(v, MCP_VERSION) == 0 || strcmp(v, MCP_VERSION_ALT1) == 0 ||
           strcmp(v, MCP_VERSION_ALT2) == 0;
}

/* Same-origin, and this origin: the browser's page is the device itself, and the device
 * answers to three names. A browser omits the port on 443 and 80, so a bare name is the
 * shape a same-origin request actually has; the explicit ports are accepted for clients
 * and tests that write them. `L_port` keeps the host build's tests, which dial a random
 * port on 127.0.0.1. */
static bool origin_ok(const http_request_t *req)
{
    if (!req->origin[0]) return true; /* native clients do not send Origin */
    if (!req->host[0]) return false;

    const char *auth;
    if (strncasecmp(req->origin, "https://", 8) == 0)
        auth = req->origin + 8;
    else if (strncasecmp(req->origin, "http://", 7) == 0)
        auth = req->origin + 7;
    else
        return false;
    if (!*auth || strpbrk(auth, "/?#@")) return false;
    if (strcasecmp(auth, req->host) != 0) return false;

    static const char *const names[2] = {"xtape.local", "localhost"};
    char local[32] = {0};
    if (L_local_ip) L_local_ip(local, sizeof local);

    for (int i = 0; i < 3; i++) {
        const char *name = i < 2 ? names[i] : local;
        if (!name || !name[0]) continue;
        if (strcasecmp(auth, name) == 0) return true;
        char pinned[80];
        snprintf(pinned, sizeof pinned, "%s:443", name);
        if (strcasecmp(auth, pinned) == 0) return true;
        snprintf(pinned, sizeof pinned, "%s:80", name);
        if (strcasecmp(auth, pinned) == 0) return true;
        snprintf(pinned, sizeof pinned, "%s:%u", name, (unsigned)L_port);
        if (strcasecmp(auth, pinned) == 0) return true;
    }
    return false;
}

static http_result_t post_route(int fd, const http_request_t *req, void *ctx)
{
    (void)ctx;
    if (!origin_ok(req)) {
        rpc_error(fd, 403, NULL, NULL, -32600, "origin rejected");
        return HTTP_HANDLED;
    }
    if (strcmp(req->path, "/mcp") != 0) {
        rpc_error(fd, 404, NULL, NULL, -32600, "no such path");
        return HTTP_HANDLED;
    }
    const char *body = (const char *)req->body;
    if (!body) {
        rpc_error(fd, 400, NULL, NULL, -32700, "empty body");
        return HTTP_HANDLED;
    }

    jtok_t tokens[MCP_TOKENS];
    int nt = 0;
    if (!jp_parse(body, tokens, MCP_TOKENS, &nt) || nt == 0) {
        rpc_error(fd, 400, NULL, NULL, -32700, "parse error");
        return HTTP_HANDLED;
    }

    if (req->mcp_protocol_version[0] && !supported_version(req->mcp_protocol_version)) {
        rpc_error(fd, 400, body, NULL, -32602, "unsupported MCP-Protocol-Version");
        return HTTP_HANDLED;
    }

    if (tokens[0].type != JT_OBJECT) {
        rpc_error(fd, 400, body, NULL, -32600, "a single JSON-RPC object is required");
        return HTTP_HANDLED;
    }
    int top = 0;
    int version_tok = jtok_member(body, tokens, nt, top, "jsonrpc");
    int method_tok = jtok_member(body, tokens, nt, top, "method");
    int id_tok = jtok_member(body, tokens, nt, top, "id");
    int params_tok = jtok_member(body, tokens, nt, top, "params");

    bool id_ok = id_tok < 0 || tokens[id_tok].type == JT_STRING;
    if (!id_ok && id_tok >= 0 && tokens[id_tok].type == JT_PRIMITIVE) {
        const char *id_s = body + tokens[id_tok].start;
        size_t id_n = (size_t)(tokens[id_tok].end - tokens[id_tok].start);
        id_ok = (id_n == 4 && memcmp(id_s, "null", 4) == 0) || number_syntax(id_s, id_n);
    }
    if (version_tok < 0 || !jtok_eq(body, &tokens[version_tok], "2.0") ||
        method_tok < 0 || tokens[method_tok].type != JT_STRING || !id_ok ||
        (params_tok >= 0 && tokens[params_tok].type != JT_OBJECT)) {
        rpc_error(fd, 400, body, id_tok >= 0 ? &tokens[id_tok] : NULL, -32600,
                  "invalid JSON-RPC envelope");
        return HTTP_HANDLED;
    }

    if (id_tok < 0) {
        /* A valid notification is never executed and has no response body. */
        rpc_notification(fd);
        return HTTP_HANDLED;
    }
    const jtok_t *id = &tokens[id_tok];

    char method[64];
    if (!jtok_unescape(body, &tokens[method_tok], method, sizeof method)) {
        rpc_error(fd, 400, body, id, -32600, "invalid method");
        return HTTP_HANDLED;
    }

    if (strcmp(method, "notifications/initialized") == 0) {
        rpc_error(fd, 200, body, id, -32600, "must be sent as a notification");
        return HTTP_HANDLED;
    }

    if (strcmp(method, "initialize") == 0) {
        if (params_tok < 0) {
            rpc_error(fd, 200, body, id, -32602, "missing params");
            return HTTP_HANDLED;
        }
        int pv = jtok_member(body, tokens, nt, params_tok, "protocolVersion");
        int caps = jtok_member(body, tokens, nt, params_tok, "capabilities");
        int ci = jtok_member(body, tokens, nt, params_tok, "clientInfo");
        if (pv < 0 || tokens[pv].type != JT_STRING) {
            rpc_error(fd, 200, body, id, -32602, "missing protocolVersion");
            return HTTP_HANDLED;
        }
        if (caps < 0 || tokens[caps].type != JT_OBJECT) {
            rpc_error(fd, 200, body, id, -32602, "missing capabilities");
            return HTTP_HANDLED;
        }
        if (ci < 0 || tokens[ci].type != JT_OBJECT) {
            rpc_error(fd, 200, body, id, -32602, "missing clientInfo");
            return HTTP_HANDLED;
        }
        int cname = jtok_member(body, tokens, nt, ci, "name");
        int cver = jtok_member(body, tokens, nt, ci, "version");
        if (cname < 0 || tokens[cname].type != JT_STRING) {
            rpc_error(fd, 200, body, id, -32602, "missing clientInfo.name");
            return HTTP_HANDLED;
        }
        if (cver < 0 || tokens[cver].type != JT_STRING) {
            rpc_error(fd, 200, body, id, -32602, "missing clientInfo.version");
            return HTTP_HANDLED;
        }
        char pvv[32];
        if (!jtok_unescape(body, &tokens[pv], pvv, sizeof pvv)) {
            rpc_error(fd, 200, body, id, -32602, "invalid protocolVersion");
            return HTTP_HANDLED;
        }
        const char *echo = supported_version(pvv) ? pvv : MCP_VERSION;
        char result[512];
        w_t rw = w_make(result, sizeof result);
        initialize_result(&rw, echo);
        if (!rw.ok) { rpc_error(fd, 500, body, id, -32603, "out of memory"); return HTTP_HANDLED; }
        rpc_result(fd, 200, body, id, result);
        return HTTP_HANDLED;
    }

    if (strcmp(method, "ping") == 0) {
        rpc_result(fd, 200, body, id, "{}");
        return HTTP_HANDLED;
    }

    if (strcmp(method, "tools/list") == 0) {
#if MCP_CONTROL
        rpc_result(fd, 200, body, id, TOOLS_CONTROL);
#else
        rpc_result(fd, 200, body, id, TOOLS_READONLY);
#endif
        return HTTP_HANDLED;
    }

    if (strcmp(method, "tools/call") == 0) {
        if (params_tok < 0) {
            rpc_error(fd, 200, body, id, -32602, "missing params");
            return HTTP_HANDLED;
        }
        int name_tok = jtok_member(body, tokens, nt, params_tok, "name");
        if (name_tok < 0 || tokens[name_tok].type != JT_STRING) {
            rpc_error(fd, 200, body, id, -32602, "missing tool name");
            return HTTP_HANDLED;
        }
        char name[64];
        if (!jtok_unescape(body, &tokens[name_tok], name, sizeof name)) {
            rpc_error(fd, 200, body, id, -32602, "invalid tool name");
            return HTTP_HANDLED;
        }
        int args = jtok_member(body, tokens, nt, params_tok, "arguments");
        if (args >= 0 && tokens[args].type != JT_OBJECT) {
            rpc_error(fd, 200, body, id, -32602, "arguments must be an object");
            return HTTP_HANDLED;
        }

        bool owned = false;
        const library_t *lib = L;
        if (!lib) {
            lib = library_acquire();
            owned = true;
        }
        char *text = heap_caps_malloc(MCP_TEXT_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!text) text = malloc(MCP_TEXT_MAX);
        if (!text) {
            if (owned) library_release(lib);
            rpc_error(fd, 500, body, id, -32603, "out of memory");
            return HTTP_HANDLED;
        }
        w_t tw = w_make(text, MCP_TEXT_MAX);
        bool ok = true;
        if (strcmp(name, "library_stats") == 0) {
            write_stats(&tw, lib);
        } else if (strcmp(name, "library_list") == 0) {
            ok = tool_library_list(&tw, lib, body, tokens, nt, args);
        } else if (strcmp(name, "library_search") == 0) {
            ok = tool_library_search(&tw, lib, body, tokens, nt, args);
        } else if (strcmp(name, "library_track") == 0) {
            ok = tool_library_track(&tw, lib, body, tokens, nt, args);
        } else if (strcmp(name, "player_status") == 0) {
            tool_player_status(&tw);
#if MCP_CONTROL
        } else if (strcmp(name, "player_play") == 0) {
            ok = control_play(&tw, lib, body, tokens, nt, args);
        } else if (strcmp(name, "player_skip") == 0) {
            ok = control_skip(&tw);
        } else if (strcmp(name, "player_prev") == 0) {
            ok = control_prev(&tw);
        } else if (strcmp(name, "library_rescan") == 0) {
            ok = control_rescan(&tw);
#endif
        } else {
            free(text);
            if (owned) library_release(lib);
            rpc_error(fd, 200, body, id, -32602, "unknown tool");
            return HTTP_HANDLED;
        }
        if (!tw.ok) {
            w_t ew = w_make(text, MCP_TEXT_MAX);
            w_puts(&ew, "response too large");
            ok = false;
        }
        rpc_tool(fd, body, id, text, !ok);
        free(text);
        if (owned) library_release(lib);
        return HTTP_HANDLED;
    }

    rpc_error(fd, 200, body, id, -32601, "method not found");
    return HTTP_HANDLED;
}

static http_result_t get_route(int fd, const http_request_t *req, void *ctx)
{
    (void)ctx;
    if (strcmp(req->path, "/mcp") != 0) {
        http_send_error(fd, 404, "no such path");
        return HTTP_HANDLED;
    }
    http_send_timeout_ms(fd, 5000);
    http_send_headers(fd, 405, "text/plain", 0, "Allow: POST\r\n");
    return HTTP_HANDLED;
}

static void mcp_body_error(int fd, int status, const char *detail, void *ctx)
{
    (void)ctx;
    int code = status == 500 ? -32603 : -32600;
    char body[256];
    int n = snprintf(body, sizeof body,
                     "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":%d,\"message\":\"%s\"}}",
                     code, detail);
    if (n < 0) n = 0;
    if ((size_t)n >= sizeof body) n = (int)sizeof body - 1;
    send_body(fd, status, body, (size_t)n);
}

esp_err_t mcp_start(const mcp_cfg_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;
    L = cfg->library;
    L_scan_ms = cfg->scan_ms;
    L_local_ip = cfg->local_ip;
    L_rescan = cfg->rescan;
    L_port = cfg->port ? cfg->port : 8000;
    SN = cfg->snapshot;
    SN_state = cfg->snapshot_state;
    SN_reason = cfg->snapshot_reason ? cfg->snapshot_reason : "";
    /* MCP is the app's endpoint: TLS listener (front door R1). */
    esp_err_t err = http_server_register_route_method_ex_on(HTTP_LISTENER_TLS, "/mcp", "POST",
                                                            post_route, NULL, mcp_body_error);
    if (err == ESP_OK)
        err = http_server_register_route_on(HTTP_LISTENER_TLS, "/mcp", get_route, NULL);
    if (err == ESP_OK)
        err = http_server_register_route_method_on(HTTP_LISTENER_TLS, "/mcp", "DELETE", get_route,
                                                   NULL);
    if (err != ESP_OK) ESP_LOGE(TAG, "cannot register /mcp: %s", esp_err_to_name(err));
    return err;
}
