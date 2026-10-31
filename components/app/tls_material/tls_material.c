/*
 * tls_material - the shipped pair, as bytes.
 *
 * The bytes are in the image because CMake's EMBED_TXTFILES put them there: for a
 * file called xtape.local.pem it defines
 *
 *     _binary_xtape_local_pem_start, _binary_xtape_local_pem_end
 *
 * in the component's own library. The symbols are named after the file's base
 * name, which is why the pair may live in tls/ at the repository root instead of
 * being copied into the component.
 *
 * The length includes the NUL that EMBED_TXTFILES appends, because mbedTLS wants a
 * NUL-terminated buffer and a length that counts it: passing the PEM's own length
 * makes `mbedtls_x509_crt_parse` return MBEDTLS_ERR_X509_INVALID_FORMAT and every
 * handshake fails with a connection reset. Nothing here logs or copies the key; it goes
 * straight into the TLS listener's configuration and stays in flash.
 */
#include "tls_material.h"

#ifdef TLS_MATERIAL_EMBEDDED

extern const uint8_t xtape_local_pem_start[] asm("_binary_xtape_local_pem_start");
extern const uint8_t xtape_local_pem_end[] asm("_binary_xtape_local_pem_end");
extern const uint8_t xtape_local_key_start[] asm("_binary_xtape_local_key_start");
extern const uint8_t xtape_local_key_end[] asm("_binary_xtape_local_key_end");

/* The embedded span, NUL included: mbedTLS's parsers expect the terminator and count it. */
static size_t embedded_len(const uint8_t *start, const uint8_t *end)
{
    return (size_t)(end - start);
}

const uint8_t *tls_material_cert(size_t *len)
{
    if (len) *len = embedded_len(xtape_local_pem_start, xtape_local_pem_end);
    return xtape_local_pem_start;
}

const uint8_t *tls_material_key(size_t *len)
{
    if (len) *len = embedded_len(xtape_local_key_start, xtape_local_key_end);
    return xtape_local_key_start;
}

bool tls_material_available(void)
{
    return embedded_len(xtape_local_pem_start, xtape_local_pem_end) > 1 &&
           embedded_len(xtape_local_key_start, xtape_local_key_end) > 1;
}

#else /* no pair in this image: the device serves the stream and says why */

const uint8_t *tls_material_cert(size_t *len)
{
    if (len) *len = 0;
    return NULL;
}

const uint8_t *tls_material_key(size_t *len)
{
    if (len) *len = 0;
    return NULL;
}

bool tls_material_available(void) { return false; }

#endif
