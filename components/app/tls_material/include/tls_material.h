#pragma once
/*
 * tls_material - the certificate and key this firmware serves, from tls/.
 *
 * One pair, shipped in every image, signed once by a CA whose key never leaves the
 * machine that signed it. The device mints nothing and needs no clock: the client
 * is the one that checks the dates (front door T1).
 *
 * Both calls return NULL and leave the length at 0 in a build that carries no
 * pair. That is a configuration a device can boot in - the stream on port 80 does
 * not care - so the caller must check, log, and carry on without TLS.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The leaf certificate for xtape.local (SANs: xtape.local, localhost,
 * 127.0.0.1), PEM, NUL-terminated by the embedding step. */
const uint8_t *tls_material_cert(size_t *len);

/* Its private key, PEM. Never logged, never written anywhere at runtime. */
const uint8_t *tls_material_key(size_t *len);

/* True when this build carries both halves. */
bool tls_material_available(void);

#ifdef __cplusplus
}
#endif
