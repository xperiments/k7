/*
 * json - the one JSON reader and writer in the firmware. See include/json.h.
 */
#include "json.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* The reader's cursor. Private: a caller only ever sees the tokens it produced. */
typedef struct {
    const char *s;
    size_t      n;
    size_t      pos;
    json_tok_t *t;
    int         nt;
    int         cap;
} json_parser_t;

/* --------------------------------------------------------------- JSON read */

static void jp_ws(json_parser_t *j)
{
    while (j->pos < j->n) {
        char c = j->s[j->pos];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') j->pos++;
        else break;
    }
}

static int jp_add(json_parser_t *j, json_tok_type_t type, int start, int parent)
{
    if (j->nt >= j->cap) return -1;
    int i = j->nt++;
    j->t[i].type = (uint8_t)type;
    j->t[i].start = start;
    j->t[i].end = start;
    j->t[i].parent = parent;
    return i;
}

static bool jp_hex4(json_parser_t *j, uint32_t *out)
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

static bool jp_string(json_parser_t *j, int parent)
{
    size_t start = j->pos + 1;
    int tok = jp_add(j, JSON_TOK_STRING, (int)start, parent);
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

static bool jp_value(json_parser_t *j, int parent, int depth);

static bool jp_object(json_parser_t *j, int parent, int depth)
{
    int tok = jp_add(j, JSON_TOK_OBJECT, (int)j->pos, parent);
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

static bool jp_array(json_parser_t *j, int parent, int depth)
{
    int tok = jp_add(j, JSON_TOK_ARRAY, (int)j->pos, parent);
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

bool json_number_syntax(const char *s, size_t n)
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

static bool jp_primitive(json_parser_t *j, int parent)
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
    if (!literal && !json_number_syntax(s, n)) return false;
    int tok = jp_add(j, JSON_TOK_PRIMITIVE, start, parent);
    if (tok < 0) return false;
    j->t[tok].end = (int)j->pos;
    return true;
}

static bool jp_value(json_parser_t *j, int parent, int depth)
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

bool json_parse(const char *body, json_tok_t *tokens, int cap, int *out_nt)
{
    json_parser_t j = {.s = body, .n = strlen(body), .pos = 0, .t = tokens, .nt = 0, .cap = cap};
    jp_ws(&j);
    if (!jp_value(&j, -1, 0)) return false;
    jp_ws(&j);
    if (j.pos != j.n) return false;
    *out_nt = j.nt;
    return true;
}

bool json_tok_eq(const char *body, const json_tok_t *t, const char *want)
{
    size_t n = (size_t)(t->end - t->start);
    return t->type == JSON_TOK_STRING && n == strlen(want) &&
           memcmp(body + t->start, want, n) == 0;
}

int json_tok_member(const char *body, const json_tok_t *t, int nt, int obj, const char *key)
{
    if (obj < 0 || obj >= nt) return -1;
    for (int i = obj + 1; i < nt; i++) {
        if (t[i].parent != obj || t[i].type != JSON_TOK_STRING) continue;
        if (!json_tok_eq(body, &t[i], key)) continue;
        for (int j = i + 1; j < nt; j++)
            if (t[j].parent == obj) return j;
        return -1;
    }
    return -1;
}

bool json_tok_unescape(const char *body, const json_tok_t *t, char *out, size_t cap)
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

int json_tok_array_item(const json_tok_t *t, int nt, int arr, int k)
{
    if (arr < 0 || arr >= nt || t[arr].type != JSON_TOK_ARRAY) return -1;
    int seen = 0;
    for (int i = arr + 1; i < nt; i++) {
        if (t[i].parent != arr) continue;
        if (seen++ == k) return i;
    }
    return -1;
}

bool json_tok_int(const char *body, const json_tok_t *t, int *out)
{
    if (t->type != JSON_TOK_PRIMITIVE) return false;
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

void json_puts(json_writer_t *w, const char *s)
{
    size_t n = strlen(s);
    if (!w->ok) return;
    if (w->n + n >= w->cap) { w->ok = false; return; }
    memcpy(w->p + w->n, s, n);
    w->n += n;
    w->p[w->n] = 0;
}


void json_raw(json_writer_t *w, const char *s, size_t n)
{
    if (!w->ok) return;
    if (w->n + n >= w->cap) { w->ok = false; return; }
    memcpy(w->p + w->n, s, n);
    w->n += n;
    w->p[w->n] = 0;
}

void json_printf(json_writer_t *w, const char *fmt, ...)
{
    char tmp[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= sizeof tmp) { w->ok = false; return; }
    json_raw(w, tmp, (size_t)n);
}

void json_escaped(json_writer_t *w, const char *s)
{
    static const char hex[] = "0123456789abcdef";
    for (; *s && w->ok; s++) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
        case '"': json_raw(w, "\\\"", 2); break;
        case '\\': json_raw(w, "\\\\", 2); break;
        default:
            if (c < 0x20) {
                char esc[6] = {'\\', 'u', '0', '0', hex[c >> 4], hex[c & 15]};
                json_raw(w, esc, sizeof esc);
            } else {
                json_raw(w, (const char *)&c, 1);
            }
            break;
        }
    }
}

void json_escaped_n(json_writer_t *w, const char *s, size_t n)
{
    for (size_t i = 0; i < n && s[i]; i++) {
        char one[2] = {s[i], 0};
        json_escaped(w, one);
    }
}

json_writer_t json_writer_make(char *buf, size_t cap)
{
    return (json_writer_t){buf, cap, 0, true};
}

bool json_ok(const json_writer_t *w)
{
    return w->ok;
}
