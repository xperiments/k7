#pragma once
/*
 * json - the one JSON reader and writer in the firmware.
 *
 * The device answers JSON on its API and speaks it to an MCP client, and it did both with a
 * copy per module: four bounded writers and two parsers, each with its own idea of escaping
 * and its own idea of what "the body is too long" means. A reply that carried a quote went out
 * unescaped from the one place that built its string with `snprintf`, and nothing failed,
 * because nothing that reads those replies has ever sent one.
 *
 * So there is one writer and one reader here, and the modules call them.
 *
 * The writer is a bounded buffer: every call is a no-op once it is full, `json_ok` says
 * whether anything was refused, and a writer that overflowed never sends half a document -
 * the caller answers 413 instead. The reader is one pass over a body into a fixed token
 * table, because every caller has a body already in memory and needs a few fields out of it,
 * not a tree to walk. A body with more tokens than the table holds is refused, not truncated.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------- the reader */

/* What a token is. A string's `start`/`end` still carry its quotes. */
typedef enum {
    JSON_TOK_OBJECT = 1,
    JSON_TOK_ARRAY,
    JSON_TOK_STRING,
    JSON_TOK_PRIMITIVE,
} json_tok_type_t;

typedef struct {
    uint8_t type;
    uint8_t pad[3];
    int     start;   /* offset of the first byte, quotes included for a string */
    int     end;     /* offset past the last byte */
    int     parent;  /* index of the enclosing object or array, -1 at the top */
} json_tok_t;

/* Parse `body` into `tokens`. False when the body is not JSON or needs more than `cap`
 * tokens; `*out_nt` is the count on success. */
bool json_parse(const char *body, json_tok_t *tokens, int cap, int *out_nt);

/* Whether token `t` is exactly the string `want` (quotes excluded). */
bool json_tok_eq(const char *body, const json_tok_t *t, const char *want);

/* The index of member `key` in object `obj`, or -1. */
int json_tok_member(const char *body, const json_tok_t *t, int nt, int obj, const char *key);

/* Copy a string token into `out`, unescaping it and never splitting a UTF-8 sequence.
 * False when it does not fit `cap` or is not a string. */
bool json_tok_unescape(const char *body, const json_tok_t *t, char *out, size_t cap);

/* A primitive token as an int. False when it is not one, or is past `100000`. */
bool json_tok_int(const char *body, const json_tok_t *t, int *out);

/* Whether `s` is a JSON number. */
bool json_number_syntax(const char *s, size_t n);

/* The index of item `k` in array `arr`, or -1. */
int json_tok_array_item(const json_tok_t *t, int nt, int arr, int k);

/* --------------------------------------------------------------- the writer */

typedef struct {
    char  *p;
    size_t cap;
    size_t n;
    bool   ok;
} json_writer_t;

/* A writer over `buf`. `json_ok` is false once anything did not fit. */
json_writer_t json_writer_make(char *buf, size_t cap);

void json_raw(json_writer_t *w, const char *s, size_t n);
void json_puts(json_writer_t *w, const char *s);
void json_printf(json_writer_t *w, const char *fmt, ...);
/* `s` as a JSON string's contents: the quotes are the caller's. */
void json_escaped(json_writer_t *w, const char *s);
/* The first `n` bytes of `s`, stopping at a NUL. */
void json_escaped_n(json_writer_t *w, const char *s, size_t n);

bool json_ok(const json_writer_t *w);

#ifdef __cplusplus
}
#endif
