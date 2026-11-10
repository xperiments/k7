#pragma once
/*
 * http_json - a JSON body over HTTP, in the two shapes the device sends.
 *
 * Every route that answers JSON needs the same two lines, and each had written them:
 * send the headers, then the bytes. The one that mattered is the second entry point: a body
 * built by a bounded writer can have overflowed, and half a document is worse than an error
 * because it is JSON that parses and lies.
 */
#include <stddef.h>

#include "json.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Send `body` as application/json, with `headers` appended when it is not NULL. */
void http_json_send(int fd, int status, const char *body, const char *headers);

/* Send what `w` built. A writer that overflowed answers 500 instead. */
void http_json_send_writer(int fd, int status, const json_writer_t *w, const char *headers);

#ifdef __cplusplus
}
#endif
