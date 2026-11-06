/*
 * card_select - see include/card_select.h.
 *
 * Deliberately small and stdio-only: the record is 32 bytes, so there is no reason to
 * cache a FILE, grow a table or reach past the C library. The CRC is the same
 * CRC-32/IEEE the Snapshot uses; it is computed here rather than borrowed so this
 * component depends on nothing but libc.
 */
#include "card_select.h"

#include <stdio.h>
#include <string.h>

#define RECORD_SIZE      32u
#define RECORD_CRC_OFFSET 28u
#define RECORD_MAGIC     "K7SEL\0\0"
#define RECORD_MAGIC_SIZE 8u
#define RECORD_VERSION   1u

static uint32_t crc32_ieee(const uint8_t *p, size_t n)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint64_t rd64(const uint8_t *p)
{
    return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32);
}

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8);
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static void wr64(uint8_t *p, uint64_t v)
{
    wr32(p, (uint32_t)v);
    wr32(p + 4, (uint32_t)(v >> 32));
}

/* Build a complete record, CRC included. Reserved bytes are zero by construction. */
static void encode(uint8_t rec[RECORD_SIZE], uint64_t id)
{
    memset(rec, 0, RECORD_SIZE);
    memcpy(rec, RECORD_MAGIC, RECORD_MAGIC_SIZE);
    wr16(rec + 8, RECORD_VERSION);
    wr16(rec + 10, 0);
    wr64(rec + 12, id);
    wr32(rec + RECORD_CRC_OFFSET, crc32_ieee(rec, RECORD_CRC_OFFSET));
}

/* Write the whole record to an already-open file, then flush it. */
static bool write_record(FILE *f, const uint8_t rec[RECORD_SIZE])
{
    bool ok = fwrite(rec, 1, RECORD_SIZE, f) == RECORD_SIZE;
    if (ok) ok = fflush(f) == 0;
    return ok;
}

bool card_select_load(const char *path, uint64_t *id)
{
    if (!path || !id) return false;

    FILE *f = fopen(path, "rb");
    if (!f) return false;

    uint8_t rec[RECORD_SIZE];
    size_t n = fread(rec, 1, RECORD_SIZE, f);
    fclose(f);
    if (n != RECORD_SIZE) return false;

    if (memcmp(rec, RECORD_MAGIC, RECORD_MAGIC_SIZE) != 0) return false;
    if (rd16(rec + 8) != RECORD_VERSION) return false;
    if (rd16(rec + 10) != 0) return false;
    /* Bytes 20..27: reserved, zero. Bytes 28..31 are the CRC. */
    for (size_t i = 20; i < RECORD_CRC_OFFSET; i++)
        if (rec[i] != 0) return false;
    if (rd32(rec + RECORD_CRC_OFFSET) != crc32_ieee(rec, RECORD_CRC_OFFSET)) return false;

    *id = rd64(rec + 12);
    return true;
}

bool card_select_save(const char *path, uint64_t id)
{
    if (!path) return false;

    uint8_t rec[RECORD_SIZE];
    encode(rec, id);

    FILE *f = fopen(path, "r+b");
    if (!f) f = fopen(path, "wb");
    if (!f) return false;

    bool ok = write_record(f, rec);
    if (fclose(f) != 0) ok = false;
    return ok;
}

bool card_select_ensure(const char *path)
{
    if (!path) return false;

    /* Existence is the whole test: a file that is there is not touched, even when its
     * contents are not a valid record. Repairing it is a decision for save, not here. */
    FILE *f = fopen(path, "rb");
    if (f) {
        fclose(f);
        return true;
    }

    uint8_t rec[RECORD_SIZE];
    encode(rec, 0);
    f = fopen(path, "wb");
    if (!f) return false;

    bool ok = write_record(f, rec);
    if (fclose(f) != 0) ok = false;
    return ok;
}
