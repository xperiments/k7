/*
 * The Shell's image, as the front door reads it.
 *
 * The image is a wire format: `tools/pack_shell.py` writes it on the build machine, the
 * board maps it out of flash and this reader answers lookups in it. So the fixture here is
 * built by hand, byte by byte - NOT with the packer - because a test that assembles its
 * input with the same code that consumes it proves only that the two agree with each other.
 * Everything below is either the format's own arithmetic or a malformation the reader must
 * refuse rather than walk off the end of.
 *
 * The reason the reader is strict is the panel: the image is copied out of flash before
 * anything validates what a lookup will trust, so a bad offset is a wrong answer the page
 * would serve, not a crash. The header and the whole table are checked
 * once at boot (shell_image_open), and a lookup then trusts what it read.
 */
#include "test_util.h"

#include <string.h>

#include "shell_image.h"

/* A hand-written image:
 *
 *   index.html        plain   "<!doctype html>"   20 bytes
 *   assets/app.js     gzip    gzip's magic + 6   10 bytes
 *   icons/icon.png    plain   PNG's magic         8 bytes
 */
#define IMG_SIZE 220

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

/* The three names, laid out one after another: 12 + 14 + 15 bytes. */
static const char k_name0[] = "index.html";        /* 10 + NUL */
static const char k_name1[] = "assets/app.js";     /* 13 + NUL */
static const char k_name2[] = "icons/icon.png";    /* 14 + NUL */

#define NAME_BYTES (sizeof k_name0 + sizeof k_name1 + sizeof k_name2)

/* Where each part of the image sits, so the builder and the corruptors share one map. */
#define OFF_NAMES 24u + 3u * 16u
#define OFF_NAME0 OFF_NAMES
#define OFF_NAME1 (OFF_NAME0 + sizeof k_name0)
#define OFF_NAME2 (OFF_NAME1 + sizeof k_name1)
#define OFF_DATA  (OFF_NAMES + NAME_BYTES) /* already 4-byte aligned */
#define OFF_HTML  OFF_DATA
#define OFF_JS    (OFF_HTML + 20u)
#define OFF_PNG   (OFF_JS + 10u)

static void entry(uint8_t *img, int i, uint32_t name_off, uint16_t name_len, uint16_t flags,
                  uint32_t data_off, uint32_t data_len)
{
    uint8_t *p = img + 24 + i * 16;
    put32(p + 0, name_off);
    put16(p + 4, name_len);
    put16(p + 6, flags);
    put32(p + 8, data_off);
    put32(p + 12, data_len);
}

/* The image the good cases read; every byte accounted for. */
static void build(uint8_t *img)
{
    memset(img, 0, IMG_SIZE);

    memcpy(img, "XTSP", 4);
    put32(img + 4, SHELL_IMAGE_VERSION);
    put32(img + 8, 3);
    put32(img + 12, OFF_NAMES);
    put32(img + 16, OFF_DATA);
    /* The image's own length. The buffer here is the whole partition on the board, so the
     * reader must bound itself by this and not by what it was handed. */
    put32(img + 20, IMG_SIZE);

    entry(img, 0, OFF_NAME0, (uint16_t)(sizeof k_name0 - 1), 0, OFF_HTML, 20);
    entry(img, 1, OFF_NAME1, (uint16_t)(sizeof k_name1 - 1), 0x0001, OFF_JS, 10);
    entry(img, 2, OFF_NAME2, (uint16_t)(sizeof k_name2 - 1), 0, OFF_PNG, 8);

    memcpy(img + OFF_NAME0, k_name0, sizeof k_name0);
    memcpy(img + OFF_NAME1, k_name1, sizeof k_name1);
    memcpy(img + OFF_NAME2, k_name2, sizeof k_name2);

    memcpy(img + OFF_HTML, "<!doctype html>", 15); /* 15 + 5 spare bytes, length is 20 */
    const uint8_t gz[10] = { 0x1F, 0x8B, 8, 0, 0, 0, 0, 0, 2, 3 };
    memcpy(img + OFF_JS, gz, sizeof gz);
    const uint8_t png[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    memcpy(img + OFF_PNG, png, sizeof png);
}

static shell_image_t opened(const uint8_t *img)
{
    shell_image_t out;
    memset(&out, 0, sizeof out);
    bool ok = shell_image_open(&out, img, IMG_SIZE);
    CHECK(ok, "this fixture is a valid image");
    return out;
}

static void test_reads(void)
{
    uint8_t img[IMG_SIZE];
    build(img);

    shell_image_t s = opened(img);
    CHECK_EQ(s.count, 3, "the header's count");
    CHECK_EQ(s.size, IMG_SIZE, "the adopted size");

    shell_blob_t blob;
    memset(&blob, 0, sizeof blob);

    /* `/` is not in the image: the caller maps it to the index, and that is the front
     * door's rule, not this module's. */
    CHECK_EQ(shell_image_find(&s, "/", true, &blob), SHELL_IMAGE_MISSING, "a bare slash");

    CHECK_EQ(shell_image_find(&s, "/index.html", true, &blob), SHELL_IMAGE_FOUND, "the page");
    CHECK_EQ(blob.len, 20u, "its length");
    CHECK(!blob.gzip, "stored plain");
    CHECK(memcmp(blob.data, "<!doctype html>", 15) == 0, "its bytes");

    /* The leading '/' is optional: the packer strips it, the request has it. */
    CHECK_EQ(shell_image_find(&s, "index.html", true, &blob), SHELL_IMAGE_FOUND,
             "without the leading slash");
    CHECK_EQ(shell_image_find(&s, "/assets/app.js", true, &blob), SHELL_IMAGE_FOUND,
             "a gzip asset to a client that takes gzip");
    CHECK(blob.gzip, "and it is flagged gzip");
    CHECK_EQ(blob.len, 10u, "its stored length");
    CHECK_EQ(blob.data[0], 0x1F, "the first byte of the stored bytes");
    CHECK_EQ(blob.data[1], 0x8B, "the second, so it is the gzip magic");

    CHECK_EQ(shell_image_find(&s, "/assets/app.js", false, &blob), SHELL_IMAGE_PACKED_ONLY,
             "the same asset to a client without gzip");
    CHECK_EQ(shell_image_find(&s, "/icons/icon.png", false, &blob), SHELL_IMAGE_FOUND,
             "a plain file to a client without gzip");

    /* Exact matching: a prefix, a suffix, a longer path and a case change are all misses. */
    CHECK_EQ(shell_image_find(&s, "/index.htm", true, &blob), SHELL_IMAGE_MISSING,
             "a prefix of a name");
    CHECK_EQ(shell_image_find(&s, "/index.html/x", true, &blob), SHELL_IMAGE_MISSING,
             "a name with a tail");
    CHECK_EQ(shell_image_find(&s, "/Index.html", true, &blob), SHELL_IMAGE_MISSING,
             "a different case");
    CHECK_EQ(shell_image_find(&s, "", true, &blob), SHELL_IMAGE_MISSING, "an empty path");
    CHECK_EQ(shell_image_find(&s, "/nope", true, &blob), SHELL_IMAGE_MISSING, "an unknown path");

    /* A NULL out is allowed: the front door's 406 check does not need the bytes. */
    CHECK_EQ(shell_image_find(&s, "/assets/app.js", true, NULL), SHELL_IMAGE_FOUND,
             "a lookup with nowhere to put the blob");
}

/* Each corruption must be refused by open(); none of them may be walked past. */
static void test_refuses(void)
{
    uint8_t img[IMG_SIZE];
    shell_image_t s;

    build(img);
    img[0] = 'X';
    img[1] = 'T';
    img[2] = 'S';
    img[3] = 'Q';
    CHECK(!shell_image_open(&s, img, IMG_SIZE), "a wrong magic");

    build(img);
    put32(img + 4, SHELL_IMAGE_VERSION + 1);
    CHECK(!shell_image_open(&s, img, IMG_SIZE), "a version this firmware does not know");
    put32(img + 4, 0);
    CHECK(!shell_image_open(&s, img, IMG_SIZE), "version zero");

    build(img);
    put32(img + 8, 0);
    CHECK(!shell_image_open(&s, img, IMG_SIZE), "an empty table");
    put32(img + 8, 4097);
    CHECK(!shell_image_open(&s, img, IMG_SIZE), "a count past any real image");
    put32(img + 8, 0xFFFFFFFFu);
    CHECK(!shell_image_open(&s, img, IMG_SIZE), "a count that would overflow the walk");

    build(img);
    put32(img + 16, IMG_SIZE + 1);
    CHECK(!shell_image_open(&s, img, IMG_SIZE), "a first blob past the end");

    /* A name that runs off the end. */
    build(img);
    entry(img, 2, (uint32_t)IMG_SIZE - 4, 40, 0, OFF_PNG, 8);
    CHECK(!shell_image_open(&s, img, IMG_SIZE), "a name past the end");

    /* A name whose last byte is not the terminator. */
    build(img);
    img[OFF_NAME0 + sizeof k_name0 - 1] = 'x';
    CHECK(!shell_image_open(&s, img, IMG_SIZE), "an unterminated name");

    /* A blob that runs past the end, by one byte. */
    build(img);
    entry(img, 2, OFF_NAME2, (uint16_t)(sizeof k_name2 - 1), 0, OFF_PNG,
          (uint32_t)(IMG_SIZE - OFF_PNG + 1));
    CHECK(!shell_image_open(&s, img, IMG_SIZE), "a blob one byte past the end");

    /* A name that points back into the entries: the names start after the table. */
    build(img);
    entry(img, 0, 20, 10, 0, OFF_HTML, 20);
    img[20 + 10] = '\0';
    CHECK(!shell_image_open(&s, img, IMG_SIZE), "a name inside the table");

    /* The stated length: past the buffer, shorter than its own table, and short enough to
     * cut the last blob - a truncated image, which is the half-flashed partition. */
    build(img);
    put32(img + 20, IMG_SIZE + 1);
    CHECK(!shell_image_open(&s, img, IMG_SIZE), "a stated length past the buffer");
    put32(img + 20, 24 + 3 * 16 - 1);
    CHECK(!shell_image_open(&s, img, IMG_SIZE), "a stated length shorter than its table");
    put32(img + 20, OFF_PNG + 7);
    CHECK(!shell_image_open(&s, img, IMG_SIZE), "a stated length that cuts the last blob");

    /* A name table that starts before the entries end, and a blob table before the names. */
    build(img);
    put32(img + 12, OFF_NAMES - 1);
    CHECK(!shell_image_open(&s, img, IMG_SIZE), "names overlapping the table");
    build(img);
    put32(img + 16, OFF_NAMES - 1);
    CHECK(!shell_image_open(&s, img, IMG_SIZE), "a first blob before the names");

    /* Nothing at all, and a header with no room behind it. */
    CHECK(!shell_image_open(&s, NULL, IMG_SIZE), "a null image");
    build(img);
    CHECK(!shell_image_open(&s, img, 23), "fewer bytes than a header");
    CHECK(!shell_image_open(&s, img, 24 + 2 * 16), "a table cut off mid-entry");
    CHECK(!shell_image_open(&s, NULL, 0), "nothing");

    /* A refused open leaves nothing behind for a lookup to use. */
    build(img);
    img[0] = '?';
    memset(&s, 0xA5, sizeof s);
    shell_image_open(&s, img, IMG_SIZE);
    CHECK_EQ(s.count, 0, "a refused open zeroes the image");
    shell_blob_t blob;
    CHECK_EQ(shell_image_find(&s, "/index.html", true, &blob), SHELL_IMAGE_MISSING,
             "and a lookup against it finds nothing");
}

/* A lookup against an image that was never opened is a miss, not a crash. */
static void test_unopened(void)
{
    shell_image_t s;
    memset(&s, 0, sizeof s);
    shell_blob_t blob;
    CHECK_EQ(shell_image_find(&s, "/index.html", true, &blob), SHELL_IMAGE_MISSING, "no image");
    CHECK_EQ(shell_image_find(NULL, "/index.html", true, &blob), SHELL_IMAGE_MISSING, "no pointer");
}

int main(void)
{
    test_reads();
    test_refuses();
    test_unopened();
    TEST_END();
}
