/*
 * library - the immutable Library tree, built once from the Music directory.
 *
 * The scan is deliberately one directory handle at a time: an explicit queue of
 * node ids, one opendir/readdir/closedir per directory. A recursive walk would
 * hold one directory stream per level; the queue keeps the scan's memory flat and
 * has no depth limit.
 *
 * The final arena is the wire format: a fixed header, 32-byte nodes, a child-edge
 * array, a track index and one string table. It contains no pointers, so it can
 * be sent verbatim. The JSON view is a second, prebuilt rendering of the same
 * tree. Lookups walk the arena and never touch the card.
 */
#include "library.h"

#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

#define TAG "library"

#define LIB_MAGIC       0x424C5458u /* "XTLB" in little-endian */
#define LIB_BLOB_VERSION 1u
#define LIB_HEADER_SIZE  64u
#define LIB_NODE_SIZE    32u
#define LIB_FLAG_LE      0x1u
#define LIB_FLAG_TRUNC   0x2u
#define LIB_JSON_COST    3u /* arena + JSON stay under max_bytes with headroom */

typedef struct {
    uint64_t size;
    uint32_t name_off;
    uint32_t parent;
    uint32_t child_edge;
    uint16_t child_count;
    uint16_t name_len;
    uint8_t  kind;
    uint8_t  format;
    uint16_t flags;
    uint32_t track_index;
} lib_node_disk_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t flags;
    uint32_t node_count;
    uint32_t edge_count;
    uint32_t track_count;
    uint32_t string_bytes;
    uint32_t crc32;
    uint32_t generation;
    uint32_t root_name_off;
    uint64_t library_bytes;
    uint32_t reserved32[2];
    uint64_t reserved64;
} lib_header_disk_t;

_Static_assert(FILENAME_MAX >= 255, "the VFS path ceiling is at least the LFN limit");
_Static_assert(sizeof(lib_node_disk_t) == LIB_NODE_SIZE, "node record is 32 bytes");
_Static_assert(offsetof(lib_node_disk_t, size) == 0, "node size is first");
_Static_assert(offsetof(lib_node_disk_t, name_off) == 8, "node name offset");
_Static_assert(offsetof(lib_node_disk_t, parent) == 12, "node parent");
_Static_assert(offsetof(lib_node_disk_t, child_edge) == 16, "node child edge");
_Static_assert(offsetof(lib_node_disk_t, child_count) == 20, "node child count");
_Static_assert(offsetof(lib_node_disk_t, name_len) == 22, "node name length");
_Static_assert(offsetof(lib_node_disk_t, kind) == 24, "node kind");
_Static_assert(offsetof(lib_node_disk_t, format) == 25, "node format");
_Static_assert(offsetof(lib_node_disk_t, flags) == 26, "node flags");
_Static_assert(offsetof(lib_node_disk_t, track_index) == 28, "node track index");
_Static_assert(sizeof(lib_header_disk_t) == LIB_HEADER_SIZE, "header is 64 bytes");
_Static_assert(offsetof(lib_header_disk_t, library_bytes) == 40, "header library bytes");
_Static_assert(offsetof(lib_header_disk_t, root_name_off) == 36, "header root name");

typedef struct {
    uint32_t parent;
    uint32_t first_child;
    uint32_t last_child;
    uint32_t next_sibling;
    uint32_t name_off;
    uint16_t name_len;
    uint16_t child_count;
    uint8_t  kind;
    uint8_t  format;
    uint64_t size;
} lib_tmp_node_t;

typedef struct {
    uint32_t node;
    const char *name;
    uint16_t len;
} lib_child_ref_t;

typedef struct {
    lib_tmp_node_t *nodes;
    uint32_t count;
    uint32_t cap;

    char *names;
    size_t names_len;
    size_t names_cap;

    uint32_t *queue;
    uint32_t qcount;
    uint32_t qcap;

    uint32_t track_count;
    uint64_t total_bytes;
    uint32_t max_tracks;
    uint64_t max_bytes;
    size_t max_path;
    bool truncated;
    bool stop;
    bool root_failed;
    bool alloc_failed;
    unsigned unreadable_dirs;
} lib_scan_t;

struct library {
    void    *arena;
    size_t   arena_size;
    char    *json;
    size_t   json_len;
    char    *music_dir;
    char     error[160];
    lib_state_t state;
    uint32_t node_count;
    uint32_t track_count;
    uint64_t total_bytes;
    uint32_t crc32;
};

/* ---------------------------------------------------------------- helpers */

static uint32_t lib_crc32(const void *data, size_t n)
{
    const uint8_t *p = data;
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
    }
    return ~crc;
}

static int ascii_lower(int c) { return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c; }

static int name_cmp(const char *a, uint16_t alen, const char *b, uint16_t blen)
{
    uint16_t n = alen < blen ? alen : blen;
    for (uint16_t i = 0; i < n; i++) {
        int ca = ascii_lower((unsigned char)a[i]);
        int cb = ascii_lower((unsigned char)b[i]);
        if (ca != cb) return ca - cb;
    }
    if (alen != blen) return (int)alen - (int)blen;
    return 0;
}

static int name_cmp_exact(const char *a, uint16_t alen, const char *b, uint16_t blen)
{
    uint16_t n = alen < blen ? alen : blen;
    int r = memcmp(a, b, n);
    if (r) return r;
    return (int)alen - (int)blen;
}

static int child_cmp(const void *pa, const void *pb)
{
    const lib_child_ref_t *a = pa, *b = pb;
    int r = name_cmp(a->name, a->len, b->name, b->len);
    if (r) return r;
    return name_cmp_exact(a->name, a->len, b->name, b->len);
}

static bool utf8_cont(unsigned char c) { return (c & 0xC0u) == 0x80u; }

bool library_utf8_valid(const char *s, size_t n)
{
    size_t i = 0;
    while (i < n) {
        unsigned char c = (unsigned char)s[i++];
        if (c < 0x80) continue;
        int need;
        uint32_t cp;
        if ((c & 0xE0u) == 0xC0u) {
            need = 1;
            cp = c & 0x1Fu;
            if (cp < 2) return false; /* overlong */
        } else if ((c & 0xF0u) == 0xE0u) {
            need = 2;
            cp = c & 0x0Fu;
        } else if ((c & 0xF8u) == 0xF0u) {
            need = 3;
            cp = c & 0x07u;
        } else {
            return false;
        }
        if (i + (size_t)need > n) return false;
        for (int k = 0; k < need; k++) {
            unsigned char c2 = (unsigned char)s[i++];
            if (!utf8_cont(c2)) return false;
            cp = (cp << 6) | (c2 & 0x3Fu);
        }
        if (cp > 0x10FFFFu) return false;
        if (cp >= 0xD800u && cp <= 0xDFFFu) return false;
        if ((need == 1 && cp < 0x80u) || (need == 2 && cp < 0x800u) ||
            (need == 3 && cp < 0x10000u))
            return false;
    }
    return true;
}

static bool name_is_component(const char *name, size_t len)
{
    if (len == 0 || len > LIB_NAME_MAX) return false;
    if (name[0] == '.') return false; /* dot-files, "." and ".." */
    if (len == 2 && name[0] == '.' && name[1] == '.') return false;
    if (strchr(name, '/') || strchr(name, '\\')) return false;
    if (!library_utf8_valid(name, len)) return false;
    return true;
}

static const char *base_name(const char *path, char *buf, size_t cap)
{
    size_t end = strlen(path);
    while (end > 1 && path[end - 1] == '/') end--;
    size_t start = end;
    while (start > 0 && path[start - 1] != '/') start--;
    size_t len = end - start;
    if (len == 0 || len > LIB_NAME_MAX) {
        snprintf(buf, cap, "%s", "music");
        return buf;
    }
    snprintf(buf, cap, "%.*s", (int)len, path + start);
    return buf;
}

/* ------------------------------------------------------------- scan state */

static bool scan_grow_nodes(lib_scan_t *s)
{
    if (s->count < s->cap) return true;
    uint32_t cap = s->cap ? s->cap * 2 : 16;
    lib_tmp_node_t *p = realloc(s->nodes, (size_t)cap * sizeof *p);
    if (!p) return false;
    s->nodes = p;
    s->cap = cap;
    return true;
}

static bool scan_grow_names(lib_scan_t *s, size_t add)
{
    if (s->names_len + add <= s->names_cap) return true;
    size_t cap = s->names_cap ? s->names_cap * 2 : 256;
    while (cap < s->names_len + add) cap *= 2;
    char *p = realloc(s->names, cap);
    if (!p) return false;
    s->names = p;
    s->names_cap = cap;
    return true;
}

static bool scan_grow_queue(lib_scan_t *s)
{
    if (s->qcount < s->qcap) return true;
    uint32_t cap = s->qcap ? s->qcap * 2 : 16;
    uint32_t *p = realloc(s->queue, (size_t)cap * sizeof *p);
    if (!p) return false;
    s->queue = p;
    s->qcap = cap;
    return true;
}

static uint32_t scan_add_node(lib_scan_t *s, uint32_t parent, const char *name, size_t len,
                              lib_kind_t kind, mi_fmt_t format, uint64_t size)
{
    if (!scan_grow_nodes(s) || !scan_grow_names(s, len)) return LIB_NODE_NONE;
    uint32_t idx = s->count++;
    lib_tmp_node_t *n = &s->nodes[idx];
    memset(n, 0, sizeof *n);
    n->parent = parent;
    n->first_child = LIB_NODE_NONE;
    n->last_child = LIB_NODE_NONE;
    n->next_sibling = LIB_NODE_NONE;
    n->name_off = (uint32_t)s->names_len;
    n->name_len = (uint16_t)len;
    n->child_count = 0;
    n->kind = (uint8_t)kind;
    n->format = (uint8_t)format;
    n->size = size;
    memcpy(s->names + s->names_len, name, len);
    s->names_len += len;

    if (parent != LIB_NODE_NONE) {
        lib_tmp_node_t *p = &s->nodes[parent];
        if (p->first_child == LIB_NODE_NONE) p->first_child = idx;
        else s->nodes[p->last_child].next_sibling = idx;
        p->last_child = idx;
        p->child_count++;
    }
    if (kind == LIB_TRACK) {
        s->track_count++;
        s->total_bytes += size;
    }
    return idx;
}

static bool scan_budget_stop(const lib_scan_t *s, size_t add_name, bool track)
{
    if (track && s->max_tracks && s->track_count >= s->max_tracks) return true;
    uint64_t nodes = (uint64_t)s->count + 1;
    uint64_t tracks = (uint64_t)s->track_count + (track ? 1u : 0u);
    uint64_t arena = LIB_HEADER_SIZE + nodes * LIB_NODE_SIZE +
                     (nodes > 0 ? nodes - 1 : 0) * 4u + tracks * 4u +
                     (uint64_t)s->names_len + add_name;
    return arena * LIB_JSON_COST > s->max_bytes;
}

static size_t tmp_relpath(const lib_scan_t *s, uint32_t node, char *buf, size_t cap)
{
    size_t len = 0;
    for (uint32_t n = node; n != 0; n = s->nodes[n].parent) {
        len += s->nodes[n].name_len;
        if (n != node) len++;
    }
    if (len + 1 > cap) return (size_t)-1;
    buf[len] = 0;
    size_t pos = len;
    for (uint32_t n = node; n != 0; n = s->nodes[n].parent) {
        const lib_tmp_node_t *tn = &s->nodes[n];
        pos -= tn->name_len;
        memcpy(buf + pos, s->names + tn->name_off, tn->name_len);
        if (tn->parent != 0) {
            pos--;
            buf[pos] = '/';
        }
    }
    return len;
}

static int tmp_abs_path(const lib_scan_t *s, const char *root, uint32_t node,
                        char *buf, size_t cap)
{
    if (node == 0) {
        int n = snprintf(buf, cap, "%s", root);
        return (n < 0 || (size_t)n >= cap || (size_t)n > s->max_path) ? -1 : n;
    }
    char rel[LIB_PATH_MAX];
    if (tmp_relpath(s, node, rel, sizeof rel) == (size_t)-1) return -1;
    int n = snprintf(buf, cap, "%s/%s", root, rel);
    return (n < 0 || (size_t)n >= cap || (size_t)n > s->max_path) ? -1 : n;
}

static void scan_dir(lib_scan_t *s, const char *root, uint32_t dir)
{
    char dir_path[LIB_PATH_MAX];
    if (tmp_abs_path(s, root, dir, dir_path, sizeof dir_path) < 0) {
        ESP_LOGW(TAG, "skipping a directory whose path exceeds %zu bytes", s->max_path);
        s->unreadable_dirs++;
        return;
    }
    DIR *d = opendir(dir_path);
    if (!d) {
        if (dir == 0) s->root_failed = true;
        else s->unreadable_dirs++;
        ESP_LOGW(TAG, "cannot read %s: %s", dir_path, strerror(errno));
        return;
    }

    struct dirent *e;
    while (!s->stop && (e = readdir(d)) != NULL) {
        const char *name = e->d_name;
        size_t len = strlen(name);
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        if (!name_is_component(name, len)) {
            if (name[0] == '.' || len > LIB_NAME_MAX) continue; /* normal skip */
            ESP_LOGW(TAG, "skipping a name that is not a valid component: %.*s",
                     (int)(len > 64 ? 64 : len), name);
            continue;
        }
        /* Skip links unconditionally: `stat` would follow one, and the target
         * libc has no lstat. A filesystem that reports DT_UNKNOWN is skipped
         * rather than stat-followed, which is the conservative choice. */
        if (e->d_type == DT_LNK || e->d_type == DT_UNKNOWN) continue;

        char abs[LIB_PATH_MAX];
        int n = snprintf(abs, sizeof abs, "%s/%s", dir_path, name);
        if (n < 0 || (size_t)n >= sizeof abs || (size_t)n > s->max_path) {
            ESP_LOGW(TAG, "skipping %s/%s: path exceeds %zu bytes", dir_path,
                     name, s->max_path);
            continue;
        }
        struct stat st;
        if (stat(abs, &st) != 0) {
            ESP_LOGW(TAG, "skipping %s: %s", abs, strerror(errno));
            continue;
        }
        if (!S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode)) continue;

        bool track = S_ISREG(st.st_mode);
        mi_fmt_t format = track ? mi_dec_format_of(name) : MI_FMT_NONE;
        if (track && format == MI_FMT_NONE) continue; /* not a Track */

        if (scan_budget_stop(s, len, track)) {
            s->truncated = true;
            s->stop = true;
            break;
        }
        uint32_t child = scan_add_node(s, dir, name, len, track ? LIB_TRACK : LIB_DIR,
                                       format, track ? (uint64_t)st.st_size : 0);
        if (child == LIB_NODE_NONE) {
            s->alloc_failed = true;
            s->stop = true;
            break;
        }
        if (!track) {
            if (!scan_grow_queue(s)) {
                s->alloc_failed = true;
                s->stop = true;
                break;
            }
            s->queue[s->qcount++] = child;
        }
    }
    closedir(d);
}

static bool sort_children(lib_scan_t *s)
{
    for (uint32_t i = 0; i < s->count; i++) {
        lib_tmp_node_t *dir = &s->nodes[i];
        if (dir->first_child == LIB_NODE_NONE || dir->child_count < 2) continue;
        lib_child_ref_t *refs = malloc((size_t)dir->child_count * sizeof *refs);
        if (!refs) return false;
        uint32_t k = 0;
        for (uint32_t c = dir->first_child; c != LIB_NODE_NONE; c = s->nodes[c].next_sibling) {
            refs[k].node = c;
            refs[k].name = s->names + s->nodes[c].name_off;
            refs[k].len = s->nodes[c].name_len;
            k++;
        }
        qsort(refs, k, sizeof *refs, child_cmp);
        dir->first_child = refs[0].node;
        dir->last_child = refs[k - 1].node;
        for (uint32_t r = 0; r < k; r++) {
            s->nodes[refs[r].node].next_sibling =
                (r + 1 < k) ? refs[r + 1].node : LIB_NODE_NONE;
        }
        free(refs);
    }
    return true;
}

static const char *format_name(mi_fmt_t f)
{
    switch (f) {
    case MI_FMT_MP3: return "mp3";
    case MI_FMT_FLAC: return "flac";
    case MI_FMT_WAV: return "wav";
    default: return "unknown";
    }
}

/* ---------------------------------------------------------------- JSON view */

typedef struct {
    char  *buf;
    size_t cap;
    size_t len;
    bool   ok;
} json_w_t;

static void jw_raw(json_w_t *w, const char *s, size_t n)
{
    if (!w->ok) return;
    if (w->buf) {
        if (w->len + n + 1 > w->cap) {
            w->ok = false;
            return;
        }
        memcpy(w->buf + w->len, s, n);
    }
    w->len += n;
}

static void jw_puts(json_w_t *w, const char *s) { jw_raw(w, s, strlen(s)); }

static void jw_printf(json_w_t *w, const char *fmt, ...)
{
    char tmp[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= sizeof tmp) {
        w->ok = false;
        return;
    }
    jw_raw(w, tmp, (size_t)n);
}

static void jw_escaped(json_w_t *w, const char *s, size_t n)
{
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < n && w->ok; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
        case '"': jw_raw(w, "\\\"", 2); break;
        case '\\': jw_raw(w, "\\\\", 2); break;
        default:
            if (c < 0x20) {
                char esc[6] = {'\\', 'u', '0', '0', hex[c >> 4], hex[c & 15]};
                jw_raw(w, esc, sizeof esc);
            } else {
                jw_raw(w, (const char *)&c, 1);
            }
            break;
        }
    }
}

typedef struct {
    lib_node_t node;
    uint32_t   next_child;
    size_t     path_len; /* the directory's own relative path length */
} json_frame_t;

static void json_node_open(const library_t *lib, json_w_t *w, lib_node_t n,
                           const char *path, size_t path_len)
{
    const lib_node_disk_t *nodes =
        (const lib_node_disk_t *)((const char *)lib->arena + LIB_HEADER_SIZE);
    const uint32_t *edges = (const uint32_t *)(nodes + lib->node_count);
    const uint32_t *tracks = edges + (lib->node_count ? lib->node_count - 1 : 0);
    const char *strings = (const char *)(tracks + lib->track_count);
    const lib_node_disk_t *node = &nodes[n];
    bool dir = node->kind == LIB_DIR;

    jw_printf(w, "{\"id\":%u,\"type\":\"%s\",\"name\":\"", (unsigned)n,
              dir ? "dir" : "track");
    jw_escaped(w, strings + node->name_off, node->name_len);
    jw_puts(w, "\",\"path\":\"");
    jw_escaped(w, path, path_len);
    jw_puts(w, "\"");
    if (dir) {
        jw_puts(w, ",\"children\":[");
    } else {
        jw_printf(w, ",\"size\":%llu,\"format\":\"%s\"}",
                  (unsigned long long)node->size, format_name((mi_fmt_t)node->format));
    }
}

static const char *state_name(lib_state_t s)
{
    switch (s) {
    case LIB_STATE_EMPTY: return "empty";
    case LIB_STATE_TRUNCATED: return "truncated";
    case LIB_STATE_ERROR: return "error";
    default: return "ok";
    }
}

static void json_document(const library_t *lib, json_w_t *w)
{
    jw_puts(w, "{\"root\":\"");
    jw_escaped(w, lib->music_dir, strlen(lib->music_dir));
    jw_printf(w, "\",\"generation\":1,\"count\":%u,\"bytes\":%llu,\"state\":\"%s\","
                 "\"truncated\":%s,\"tree\":",
              (unsigned)lib->track_count, (unsigned long long)lib->total_bytes,
              state_name(lib->state), lib->state == LIB_STATE_TRUNCATED ? "true" : "false");

    const lib_node_disk_t *nodes =
        (const lib_node_disk_t *)((const char *)lib->arena + LIB_HEADER_SIZE);
    const uint32_t *edges = (const uint32_t *)(nodes + lib->node_count);
    const uint32_t *tracks = edges + (lib->node_count ? lib->node_count - 1 : 0);
    const char *strings = (const char *)(tracks + lib->track_count);
    json_frame_t *stack = malloc((size_t)lib->node_count * sizeof *stack);
    if (!stack) {
        w->ok = false;
        return;
    }
    char path[LIB_PATH_MAX];
    path[0] = 0;
    size_t depth = 0;
    json_node_open(lib, w, 0, path, 0);
    stack[depth++] = (json_frame_t){0, 0, 0};
    while (depth && w->ok) {
        json_frame_t *frame = &stack[depth - 1];
        const lib_node_disk_t *node = &nodes[frame->node];
        if (frame->next_child < node->child_count) {
            uint32_t idx = frame->next_child++;
            lib_node_t child = edges[node->child_edge + idx];
            if (idx) jw_puts(w, ",");
            size_t plen = frame->path_len;
            if (plen) {
                if (plen + 1 >= sizeof path) {
                    w->ok = false;
                    break;
                }
                path[plen++] = '/';
            }
            const lib_node_disk_t *cn = &nodes[child];
            if (plen + cn->name_len >= sizeof path) {
                w->ok = false;
                break;
            }
            memcpy(path + plen, strings + cn->name_off, cn->name_len);
            plen += cn->name_len;
            path[plen] = 0;
            json_node_open(lib, w, child, path, plen);
            if (cn->kind == LIB_DIR) {
                if (depth >= lib->node_count) {
                    w->ok = false;
                    break;
                }
                stack[depth++] = (json_frame_t){child, 0, plen};
            }
        } else {
            jw_puts(w, "]}");
            depth--;
        }
    }
    free(stack);
    jw_puts(w, "}");
}

/* ------------------------------------------------------------- final build */

static bool build_arena(lib_scan_t *s, const char *music_dir, bool truncated,
                        void **out_arena, size_t *out_size, uint32_t *out_nodes,
                        uint32_t *out_tracks, uint64_t *out_bytes, uint32_t *out_crc)
{
    uint32_t n = s->count;
    if (n == 0) return false;
    uint32_t *order = malloc((size_t)n * sizeof *order);
    uint32_t *final_id = malloc((size_t)n * sizeof *final_id);
    uint32_t *stack = malloc((size_t)n * sizeof *stack);
    uint32_t *kids = malloc((size_t)n * sizeof *kids);
    if (!order || !final_id || !stack || !kids) {
        free(order); free(final_id); free(stack); free(kids);
        return false;
    }

    if (!sort_children(s)) {
        free(order); free(final_id); free(stack); free(kids);
        return false;
    }

    uint32_t top = 0, next = 0;
    stack[top++] = 0;
    while (top) {
        uint32_t id = stack[--top];
        final_id[id] = next;
        order[next++] = id;
        uint32_t count = s->nodes[id].child_count;
        uint32_t k = 0;
        for (uint32_t c = s->nodes[id].first_child; c != LIB_NODE_NONE && k < count;
             c = s->nodes[c].next_sibling)
            kids[k++] = c;
        while (k) stack[top++] = kids[--k];
    }

    uint32_t edge_count = n - 1;
    uint32_t track_count = s->track_count;
    size_t strings_len = 0;
    for (uint32_t i = 0; i < n; i++) strings_len += s->nodes[order[i]].name_len;

    size_t arena_size = LIB_HEADER_SIZE + (size_t)n * LIB_NODE_SIZE +
                        (size_t)edge_count * 4 + (size_t)track_count * 4 + strings_len;
    void *arena = heap_caps_malloc(arena_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!arena) {
        free(order); free(final_id); free(stack); free(kids);
        return false;
    }
    memset(arena, 0, arena_size);

    lib_header_disk_t *h = arena;
    lib_node_disk_t *nodes = (lib_node_disk_t *)((char *)arena + LIB_HEADER_SIZE);
    uint32_t *edges = (uint32_t *)(nodes + n);
    uint32_t *tracks = edges + edge_count;
    char *strings = (char *)(tracks + track_count);

    size_t str_pos = 0, edge_pos = 0;
    uint32_t track_pos = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t tmp_id = order[i];
        lib_tmp_node_t *tn = &s->nodes[tmp_id];
        lib_node_disk_t *dn = &nodes[i];
        dn->size = tn->size;
        dn->name_off = (uint32_t)str_pos;
        memcpy(strings + str_pos, s->names + tn->name_off, tn->name_len);
        str_pos += tn->name_len;
        dn->parent = tn->parent == LIB_NODE_NONE ? LIB_NODE_NONE : final_id[tn->parent];
        dn->child_edge = LIB_NODE_NONE;
        dn->child_count = tn->child_count;
        dn->name_len = tn->name_len;
        dn->kind = tn->kind;
        dn->format = tn->format;
        dn->flags = 0;
        dn->track_index = LIB_NODE_NONE;
        if (tn->kind == LIB_DIR) {
            dn->child_edge = tn->child_count ? (uint32_t)edge_pos : LIB_NODE_NONE;
            for (uint32_t c = tn->first_child; c != LIB_NODE_NONE;
                 c = s->nodes[c].next_sibling)
                edges[edge_pos++] = final_id[c];
        } else {
            dn->track_index = track_pos;
            tracks[track_pos++] = i;
        }
    }

    h->magic = LIB_MAGIC;
    h->version = LIB_BLOB_VERSION;
    h->header_size = LIB_HEADER_SIZE;
    h->flags = LIB_FLAG_LE | (truncated ? LIB_FLAG_TRUNC : 0u);
    h->node_count = n;
    h->edge_count = edge_count;
    h->track_count = track_count;
    h->string_bytes = (uint32_t)strings_len;
    h->generation = 1;
    h->root_name_off = nodes[0].name_off;
    h->library_bytes = s->total_bytes;
    h->crc32 = lib_crc32((char *)arena + LIB_HEADER_SIZE, arena_size - LIB_HEADER_SIZE);

    free(order); free(final_id); free(stack); free(kids);
    *out_arena = arena;
    *out_size = arena_size;
    *out_nodes = n;
    *out_tracks = track_count;
    *out_bytes = s->total_bytes;
    *out_crc = h->crc32;
    (void)music_dir;
    return true;
}

typedef enum {
    JSON_OK = 0,
    JSON_TOO_LARGE,
    JSON_NO_MEM,
} json_result_t;

static json_result_t build_json(library_t *lib, uint64_t max_total)
{
    json_w_t measure = {NULL, 0, 0, true};
    json_document(lib, &measure);
    if (!measure.ok) return JSON_NO_MEM;
    if ((uint64_t)lib->arena_size + measure.len + 1 > max_total) return JSON_TOO_LARGE;
    char *json = heap_caps_malloc(measure.len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!json) return JSON_NO_MEM;
    json_w_t fill = {json, measure.len + 1, 0, true};
    json_document(lib, &fill);
    if (!fill.ok || fill.len != measure.len) {
        heap_caps_free(json);
        return JSON_NO_MEM;
    }
    json[fill.len] = 0;
    lib->json = json;
    lib->json_len = fill.len;
    return JSON_OK;
}

/* ------------------------------------------------------------------- API */

lib_result_t library_open(const library_cfg_t *cfg, library_t **out)
{
    if (out) *out = NULL;
    if (!cfg || !cfg->music_dir || !cfg->music_dir[0]) return LIB_INVALID_ARG;

    char root[LIB_PATH_MAX];
    size_t root_len = strlen(cfg->music_dir);
    while (root_len > 1 && cfg->music_dir[root_len - 1] == '/') root_len--;
    if (root_len >= sizeof root) return LIB_INVALID_ARG;
    memcpy(root, cfg->music_dir, root_len);
    root[root_len] = 0;
    size_t max_path = cfg->max_path ? cfg->max_path : LIB_PATH_MAX - 1;
    if (max_path > LIB_PATH_MAX - 1) max_path = LIB_PATH_MAX - 1;
    if (root_len > max_path) return LIB_INVALID_ARG;

    lib_scan_t s;
    memset(&s, 0, sizeof s);
    s.max_bytes = cfg->max_bytes ? cfg->max_bytes : LIB_DEFAULT_MAX_BYTES;
    s.max_tracks = cfg->max_tracks ? cfg->max_tracks : LIB_DEFAULT_MAX_TRACKS;
    s.max_path = max_path;

    char root_name[LIB_NAME_MAX + 1];
    base_name(root, root_name, sizeof root_name);
    uint32_t root_node = scan_add_node(&s, LIB_NODE_NONE, root_name, strlen(root_name),
                                       LIB_DIR, MI_FMT_NONE, 0);
    if (root_node == LIB_NODE_NONE) {
        free(s.nodes); free(s.names); free(s.queue);
        return LIB_NO_MEM;
    }
    if (scan_budget_stop(&s, 0, false)) {
        s.truncated = true;
        s.stop = true;
    }
    if (!s.stop && !scan_grow_queue(&s)) {
        free(s.nodes); free(s.names); free(s.queue);
        return LIB_NO_MEM;
    }
    if (!s.stop) {
        s.queue[s.qcount++] = root_node;
        for (uint32_t head = 0; head < s.qcount && !s.stop; head++)
            scan_dir(&s, root, s.queue[head]);
    }
    if (s.root_failed) {
        free(s.nodes); free(s.names); free(s.queue);
        return LIB_NO_DIR;
    }
    if (s.alloc_failed) {
        free(s.nodes); free(s.names); free(s.queue);
        return LIB_NO_MEM;
    }

    bool truncated = s.truncated;
    void *arena = NULL;
    size_t arena_size = 0;
    uint32_t nodes_n = 0, tracks_n = 0, crc = 0;
    uint64_t total = 0;
    if (!build_arena(&s, root, truncated, &arena, &arena_size, &nodes_n, &tracks_n, &total, &crc)) {
        free(s.nodes); free(s.names); free(s.queue);
        return LIB_NO_MEM;
    }

    library_t *lib = calloc(1, sizeof *lib);
    if (!lib) {
        heap_caps_free(arena);
        free(s.nodes); free(s.names); free(s.queue);
        return LIB_NO_MEM;
    }
    lib->arena = arena;
    lib->arena_size = arena_size;
    lib->node_count = nodes_n;
    lib->track_count = tracks_n;
    lib->total_bytes = total;
    lib->crc32 = crc;
    if (arena_size > s.max_bytes) truncated = true;
    lib->state = truncated ? LIB_STATE_TRUNCATED
                           : (tracks_n ? LIB_STATE_OK : LIB_STATE_EMPTY);
    if (truncated) {
        ((lib_header_disk_t *)lib->arena)->flags |= LIB_FLAG_TRUNC;
        ESP_LOGW(TAG, "library truncated at %u tracks, %u nodes (cap %llu bytes)",
                 (unsigned)tracks_n, (unsigned)nodes_n, (unsigned long long)s.max_bytes);
    }
    if (s.unreadable_dirs)
        snprintf(lib->error, sizeof lib->error, "%u director%s could not be read",
                 s.unreadable_dirs, s.unreadable_dirs == 1 ? "y" : "ies");
    lib->music_dir = malloc(strlen(root) + 1);
    if (!lib->music_dir) {
        library_close(lib);
        free(s.nodes); free(s.names); free(s.queue);
        return LIB_NO_MEM;
    }
    strcpy(lib->music_dir, root);

    if (cfg->build_json) {
        json_result_t jr = build_json(lib, s.max_bytes);
        if (jr == JSON_NO_MEM) {
            library_close(lib);
            free(s.nodes); free(s.names); free(s.queue);
            return LIB_NO_MEM;
        }
        if (jr == JSON_TOO_LARGE) {
            lib->state = LIB_STATE_TRUNCATED;
            ((lib_header_disk_t *)lib->arena)->flags |= LIB_FLAG_TRUNC;
            snprintf(lib->error, sizeof lib->error,
                     "JSON view would exceed the %llu-byte budget; serving the binary view only",
                     (unsigned long long)s.max_bytes);
            ESP_LOGW(TAG, "%s", lib->error);
        }
    }

    free(s.nodes);
    free(s.names);
    free(s.queue);
    if (out) *out = lib;
    else library_close(lib);
    return LIB_OK;
}

void library_close(library_t *lib)
{
    if (!lib) return;
    if (lib->json) heap_caps_free(lib->json);
    if (lib->arena) heap_caps_free(lib->arena);
    free(lib->music_dir);
    free(lib);
}

static const lib_node_disk_t *nodes(const library_t *lib)
{
    return (const lib_node_disk_t *)((const char *)lib->arena + LIB_HEADER_SIZE);
}

static const uint32_t *edges(const library_t *lib)
{
    return (const uint32_t *)(nodes(lib) + lib->node_count);
}

static const uint32_t *tracks(const library_t *lib)
{
    return edges(lib) + (lib->node_count ? lib->node_count - 1 : 0);
}

static const char *strings(const library_t *lib)
{
    return (const char *)(tracks(lib) + lib->track_count);
}

lib_state_t library_state(const library_t *lib) { return lib ? lib->state : LIB_STATE_ERROR; }
const char *library_error(const library_t *lib) { return lib ? lib->error : "no library"; }
bool library_truncated(const library_t *lib) { return lib && lib->state == LIB_STATE_TRUNCATED; }
uint32_t library_node_count(const library_t *lib) { return lib ? lib->node_count : 0; }
uint32_t library_track_count(const library_t *lib) { return lib ? lib->track_count : 0; }
uint64_t library_total_bytes(const library_t *lib) { return lib ? lib->total_bytes : 0; }
uint32_t library_crc32(const library_t *lib) { return lib ? lib->crc32 : 0; }
lib_node_t library_root(const library_t *lib) { return lib ? 0 : LIB_NODE_NONE; }

lib_node_t library_parent(const library_t *lib, lib_node_t n)
{
    if (!lib || n >= lib->node_count) return LIB_NODE_NONE;
    return nodes(lib)[n].parent;
}

uint32_t library_child_count(const library_t *lib, lib_node_t n)
{
    if (!lib || n >= lib->node_count) return 0;
    return nodes(lib)[n].child_count;
}

lib_node_t library_child(const library_t *lib, lib_node_t n, uint32_t i)
{
    if (!lib || n >= lib->node_count) return LIB_NODE_NONE;
    const lib_node_disk_t *node = &nodes(lib)[n];
    if (i >= node->child_count || node->child_edge == LIB_NODE_NONE) return LIB_NODE_NONE;
    return edges(lib)[node->child_edge + i];
}

const char *library_name(const library_t *lib, lib_node_t n, uint16_t *len)
{
    if (!lib || n >= lib->node_count) {
        if (len) *len = 0;
        return NULL;
    }
    const lib_node_disk_t *node = &nodes(lib)[n];
    if (len) *len = node->name_len;
    return strings(lib) + node->name_off;
}

lib_kind_t library_kind(const library_t *lib, lib_node_t n)
{
    return (lib && n < lib->node_count) ? (lib_kind_t)nodes(lib)[n].kind : LIB_KIND_NONE;
}

uint64_t library_size(const library_t *lib, lib_node_t n)
{
    return (lib && n < lib->node_count) ? nodes(lib)[n].size : 0;
}

mi_fmt_t library_format(const library_t *lib, lib_node_t n)
{
    if (!lib || n >= lib->node_count) return MI_FMT_NONE;
    const lib_node_disk_t *node = &nodes(lib)[n];
    return node->kind == LIB_TRACK ? (mi_fmt_t)node->format : MI_FMT_NONE;
}

size_t library_relpath(const library_t *lib, lib_node_t n, char *buf, size_t buf_size)
{
    if (!lib || n >= lib->node_count || !buf || !buf_size) return (size_t)-1;
    const lib_node_disk_t *ns = nodes(lib);
    if (n == 0) {
        buf[0] = 0;
        return 0;
    }
    size_t len = 0;
    for (lib_node_t cur = n; cur != 0; cur = ns[cur].parent) {
        len += ns[cur].name_len;
        if (cur != n) len++;
    }
    if (len + 1 > buf_size) return (size_t)-1;
    buf[len] = 0;
    size_t pos = len;
    for (lib_node_t cur = n; cur != 0; cur = ns[cur].parent) {
        const lib_node_disk_t *node = &ns[cur];
        pos -= node->name_len;
        memcpy(buf + pos, strings(lib) + node->name_off, node->name_len);
        if (node->parent != 0) {
            pos--;
            buf[pos] = '/';
        }
    }
    return len;
}

static bool ascii_eq_fold(const char *a, size_t alen, const char *b, size_t blen)
{
    if (alen != blen) return false;
    for (size_t i = 0; i < alen; i++) {
        unsigned char ca = (unsigned char)a[i], cb = (unsigned char)b[i];
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca + ('a' - 'A'));
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb + ('a' - 'A'));
        if (ca != cb) return false;
    }
    return true;
}

lib_node_t library_find(const library_t *lib, const char *relpath)
{
    if (!lib || !relpath) return LIB_NODE_NONE;
    if (!relpath[0]) return 0;
    if (relpath[0] == '/') return LIB_NODE_NONE;

    lib_node_t cur = 0;
    const char *p = relpath;
    for (;;) {
        const char *slash = strchr(p, '/');
        size_t len = slash ? (size_t)(slash - p) : strlen(p);
        if (len == 0 || len > LIB_NAME_MAX) return LIB_NODE_NONE;
        if ((len == 1 && p[0] == '.') || (len == 2 && p[0] == '.' && p[1] == '.'))
            return LIB_NODE_NONE;
        if (strchr(p, '\\') && (!slash || strchr(p, '\\') < slash)) return LIB_NODE_NONE;
        if (library_kind(lib, cur) != LIB_DIR) return LIB_NODE_NONE;

        lib_node_t exact = LIB_NODE_NONE, folded = LIB_NODE_NONE;
        uint32_t folded_count = 0;
        uint32_t count = library_child_count(lib, cur);
        for (uint32_t i = 0; i < count; i++) {
            lib_node_t child = library_child(lib, cur, i);
            uint16_t clen = 0;
            const char *name = library_name(lib, child, &clen);
            if (clen == len && memcmp(name, p, len) == 0) {
                exact = child;
                break;
            }
            if (ascii_eq_fold(name, clen, p, len)) {
                folded = child;
                folded_count++;
            }
        }
        lib_node_t found = exact;
        if (found == LIB_NODE_NONE) {
            if (folded_count == 1) found = folded;
            else if (folded_count > 1) return LIB_NODE_NONE;
        }
        if (found == LIB_NODE_NONE) return LIB_NODE_NONE;
        if (!slash) return found;
        if (slash[1] == 0) return LIB_NODE_NONE;
        cur = found;
        p = slash + 1;
    }
}

lib_node_t library_track_at(const library_t *lib, uint32_t playlist_index)
{
    if (!lib || playlist_index >= lib->track_count) return LIB_NODE_NONE;
    return tracks(lib)[playlist_index];
}

uint32_t library_track_index(const library_t *lib, lib_node_t n)
{
    if (!lib || n >= lib->node_count || nodes(lib)[n].kind != LIB_TRACK) return LIB_NODE_NONE;
    return nodes(lib)[n].track_index;
}

const char *library_json(const library_t *lib, size_t *len)
{
    if (len) *len = lib ? lib->json_len : 0;
    return lib ? lib->json : NULL;
}

const void *library_blob(const library_t *lib, size_t *len)
{
    if (len) *len = lib ? lib->arena_size : 0;
    return lib ? lib->arena : NULL;
}
