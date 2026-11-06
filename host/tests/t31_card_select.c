/*
 * T31: card_select, host tier.
 *
 * The record is built here byte by byte by a builder that shares no code with the
 * module: the test owns the layout and the CRC, so a change in the module cannot make
 * the test agree with itself. What is exercised is the contract the appliance relies
 * on: a round trip, the id-0 record, and a refusal for every kind of damaged file.
 */
#include "test_util.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "card_select.h"

/* ------------------------------------------------------------ independent bits */

static uint32_t crc32_ref(const uint8_t *p, size_t n)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static void wr32le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void wr64le(uint8_t *p, uint64_t v)
{
    wr32le(p, (uint32_t)v);
    wr32le(p + 4, (uint32_t)(v >> 32));
}

/* Build a complete, valid record: 32 bytes, CRC over the first 28. */
static void build_record(uint8_t rec[32], uint64_t id)
{
    memset(rec, 0, 32);
    memcpy(rec, "K7SEL\0\0", 8);
    rec[8] = 1;
    rec[9] = 0;
    wr64le(rec + 12, id);
    wr32le(rec + 28, crc32_ref(rec, 28));
}

/* Re-seal a record whose fields the test just changed. */
static void reseal(uint8_t rec[32])
{
    wr32le(rec + 28, crc32_ref(rec, 28));
}

/* ----------------------------------------------------------------- file helpers */

static bool read_file(const char *path, uint8_t *buf, size_t cap, size_t *got)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    size_t n = fread(buf, 1, cap, f);
    fclose(f);
    if (got) *got = n;
    return true;
}

static void write_file(const char *path, const uint8_t *buf, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        printf("FATAL: cannot write %s\n", path);
        exit(1);
    }
    if (fwrite(buf, 1, n, f) != n || fclose(f) != 0) {
        printf("FATAL: cannot write %s\n", path);
        exit(1);
    }
}

static long file_size(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return (long)st.st_size;
}

/* ------------------------------------------------------------------------ test */

int main(void)
{
    char tmpl[] = "/tmp/xtpcardXXXXXX";
    char *root = mkdtemp(tmpl);
    if (!root) {
        printf("FATAL: mkdtemp failed\n");
        return 1;
    }

    char path[512];
    snprintf(path, sizeof path, "%s/k7.select", root);

    const uint64_t id_a = 0x0123456789ABCDEFull;
    const uint64_t id_b = 0xDEADBEEFCAFEF00Dull;
    uint8_t raw[64];
    size_t got = 0;
    uint64_t id = 0;

    /* a missing file is "no selection" */
    CHECK(!card_select_load(path, &id), "a missing record is a refusal");
    CHECK_EQ(file_size(path), -1, "and load did not create it");

    /* ensure on an absent file creates one zero record */
    CHECK(card_select_ensure(path), "ensure creates an absent record");
    CHECK_EQ(file_size(path), 32, "the record is exactly 32 bytes");
    CHECK(read_file(path, raw, sizeof raw, &got) && got == 32, "the whole record is there");
    CHECK(memcmp(raw, "K7SEL\0\0", 8) == 0, "the magic is K7SEL, NUL-padded");
    CHECK_EQ(raw[8], 1, "version low byte");
    CHECK_EQ(raw[9], 0, "version high byte");
    CHECK_EQ(raw[10], 0, "the first reserved word is zero");
    CHECK_EQ(raw[11], 0, "the first reserved word's high byte");
    for (int i = 12; i < 20; i++) CHECK_EQ(raw[i], 0, "the ensured id is 0");
    for (int i = 20; i < 28; i++) CHECK_EQ(raw[i], 0, "the reserved bytes are zero");
    CHECK_EQ(raw[28], (uint8_t)crc32_ref(raw, 28), "the CRC low byte");
    CHECK_EQ(raw[29], (uint8_t)(crc32_ref(raw, 28) >> 8), "the CRC second byte");
    CHECK(card_select_load(path, &id), "the ensured record loads");
    CHECK_EQ(id, 0, "and says no selection");

    /* ensure on a present file leaves it alone */
    CHECK(card_select_save(path, id_a), "save a choice");
    CHECK(card_select_ensure(path), "ensure on a present record");
    CHECK(card_select_load(path, &id), "the present record still loads");
    CHECK_EQ(id, id_a, "and ensure did not overwrite it");
    CHECK_EQ(file_size(path), 32, "still exactly 32 bytes");

    /* save/load round trip, including the little-endian layout */
    CHECK(card_select_save(path, id_b), "save the second choice");
    CHECK(card_select_load(path, &id), "load the second choice");
    CHECK_EQ(id, id_b, "the id round-trips");
    CHECK(read_file(path, raw, sizeof raw, &got) && got == 32, "one record was written");
    CHECK_EQ(raw[12], (uint8_t)id_b, "id byte 0");
    CHECK_EQ(raw[13], (uint8_t)(id_b >> 8), "id byte 1");
    CHECK_EQ(raw[14], (uint8_t)(id_b >> 16), "id byte 2");
    CHECK_EQ(raw[15], (uint8_t)(id_b >> 24), "id byte 3");
    CHECK_EQ(raw[16], (uint8_t)(id_b >> 32), "id byte 4");
    CHECK_EQ(raw[17], (uint8_t)(id_b >> 40), "id byte 5");
    CHECK_EQ(raw[18], (uint8_t)(id_b >> 48), "id byte 6");
    CHECK_EQ(raw[19], (uint8_t)(id_b >> 56), "id byte 7");
    CHECK_EQ(raw[28], (uint8_t)crc32_ref(raw, 28), "the written CRC low byte");
    CHECK_EQ(raw[29], (uint8_t)(crc32_ref(raw, 28) >> 8), "the written CRC second byte");

    /* id 0 is a real record, not a deletion */
    CHECK(card_select_save(path, 0), "save no selection");
    CHECK(card_select_load(path, &id), "the id-0 record loads");
    CHECK_EQ(id, 0, "and reads as 0");
    CHECK_EQ(file_size(path), 32, "id 0 still occupies one record");

    /* save creates a missing file */
    CHECK_EQ(remove(path), 0, "remove the record");
    CHECK(card_select_save(path, id_a), "save onto a missing path creates it");
    CHECK_EQ(file_size(path), 32, "and writes the whole record");
    CHECK(card_select_load(path, &id), "which loads");
    CHECK_EQ(id, id_a, "with the saved id");

    /* short files */
    write_file(path, raw, 31);
    CHECK(!card_select_load(path, &id), "a 31-byte record is a refusal");
    write_file(path, raw, 0);
    CHECK(!card_select_load(path, &id), "an empty record is a refusal");

    /* bad magic */
    build_record(raw, id_a);
    raw[0] ^= 0x01;
    write_file(path, raw, 32);
    CHECK(!card_select_load(path, &id), "a bad magic is a refusal");

    /* bad version, sealed so the version is the only fault */
    build_record(raw, id_a);
    raw[8] = 2;
    raw[9] = 0;
    reseal(raw);
    write_file(path, raw, 32);
    CHECK(!card_select_load(path, &id), "a version other than 1 is a refusal");

    /* bad reserved word, sealed so the reserved field is the only fault */
    build_record(raw, id_a);
    raw[10] = 1;
    reseal(raw);
    write_file(path, raw, 32);
    CHECK(!card_select_load(path, &id), "a non-zero reserved word is a refusal");

    /* bad reserved padding, sealed so the padding is the only fault */
    build_record(raw, id_a);
    raw[20] = 1;
    reseal(raw);
    write_file(path, raw, 32);
    CHECK(!card_select_load(path, &id), "non-zero reserved padding is a refusal");

    /* bad CRC: the id is changed but the last four bytes are left stale */
    build_record(raw, id_a);
    raw[12] ^= 0xFF;
    write_file(path, raw, 32);
    CHECK(!card_select_load(path, &id), "a bad CRC is a refusal");

    /* a valid record still loads after all the damage, and save repairs the file */
    CHECK(card_select_save(path, id_b), "save repairs the record in place");
    CHECK(card_select_load(path, &id), "the repaired record loads");
    CHECK_EQ(id, id_b, "with the saved id");

    char cmd[600];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
    if (system(cmd) != 0) { /* the test's own /tmp tree */ }

    TEST_END();
}
