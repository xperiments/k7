/*
 * T33: the sheet the firmware ships, opened by the reader it ships with.
 *
 * Every other label test builds its own bytes. t32 is deliberately one of those - it owns
 * the layout and the CRC so a change in the reader cannot make the test agree with it - but
 * that leaves the seam itself unproven: the packer's output is embedded by CMake and nothing
 * on the host ever opens it. A packer change could break the panel with every test green.
 *
 * So this one constructs nothing. It reads `label_sheet.bin` and `label_manifest.txt` from
 * the directory the packer wrote them to (host/Makefile's `label` target), opens the sheet
 * with label.c and compares what the reader reports against the manifest - which the packer
 * derives from the designs, not from its own bytes. If the packer and the reader disagree
 * about a template, a prim count or a Slot's name, this is the test that says so.
 *
 * It also checks the one rule that only the packer enforces today: a text prints a Slot the
 * design declares. The reader tolerates an unknown Slot name on purpose (a sheet and a
 * firmware built apart is a bug, not a state), so the cross-check lives here, over the sheet
 * that actually ships.
 *
 *   usage: t33_label_sheet <dir holding label_sheet.bin and label_manifest.txt>
 */
#include "test_util.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "label.h"
#include "label_fonts.h" /* generated beside the sheet: the font table the app compiles */

/* A sheet is a few KB; anything past this is not one and is not worth allocating. */
#define SHEET_MAX (256u * 1024u)

static uint8_t *slurp(const char *path, size_t *len_out)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long n = ftell(f);
    if (n < 0 || (unsigned long)n > SHEET_MAX) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    uint8_t *p = malloc((size_t)n ? (size_t)n : 1u);
    if (!p) {
        fclose(f);
        return NULL;
    }
    if (fread(p, 1, (size_t)n, f) != (size_t)n) {
        free(p);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *len_out = (size_t)n;
    return p;
}

static void chomp(char *s)
{
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r')) s[--n] = '\0';
}

/* Splits `line` on its first five tabs, in place. The sixth field is the design's name
 * and runs to the end of the line, so a name containing a tab only makes the name longer. */
static bool split6(char *line, char **f)
{
    int n = 0;
    f[n++] = line;
    for (char *p = line; *p; p++) {
        if (*p == '\t' && n < 6) {
            *p = '\0';
            f[n++] = p + 1;
        }
    }
    return n == 6;
}

/* The template's Slot names, in the order the sheet lists them, against the manifest's. */
static void check_slots(const label_sheet_t *sheet, unsigned i, const char *wanted)
{
    unsigned declared = label_slot_count(sheet, i);
    unsigned seen = 0;
    const char *p = wanted;
    while (*p) {
        const char *comma = strchr(p, ',');
        size_t len = comma ? (size_t)(comma - p) : strlen(p);
        char name[64];
        CHECK(len < sizeof name, "template %u: a Slot name in the manifest is %zu chars", i, len);
        if (len < sizeof name) {
            memcpy(name, p, len);
            name[len] = '\0';
            label_slot_t slot;
            bool got = seen < declared && label_slot_at(sheet, i, (uint16_t)seen, &slot);
            CHECK(got, "template %u: the sheet has fewer Slots than the manifest", i);
            if (got)
                CHECK(strcmp(slot.name, name) == 0,
                      "template %u slot %u: the sheet says \"%s\", the manifest \"%s\"", i, seen,
                      slot.name, name);
        }
        seen++;
        if (!comma) break;
        p = comma + 1;
    }
    /* An empty list is an empty string, and a design with no Slots is legal. */
    if (*wanted == '\0') seen = 0;
    CHECK_EQ(seen, declared, "a template's Slot count");
}

/* A text that names a Slot the design does not declare prints nothing on the panel: the
 * packer refuses it, and the reader lets it through. Say so here, over the shipping sheet. */
static void check_declared_slots(const label_sheet_t *sheet, unsigned i)
{
    unsigned prims = label_prim_count(sheet, i);
    for (unsigned j = 0; j < prims; j++) {
        label_prim_t prim;
        if (!label_prim_at(sheet, i, (uint16_t)j, &prim)) continue;
        if (prim.kind != LABEL_TEXT_SLOT) continue;
        const char *name = label_prim_text(sheet, i, (uint16_t)j);
        bool declared = false;
        for (unsigned k = 0; k < label_slot_count(sheet, i); k++) {
            label_slot_t slot;
            if (label_slot_at(sheet, i, (uint16_t)k, &slot) && name && strcmp(slot.name, name) == 0)
                declared = true;
        }
        CHECK(declared, "template %u: a text prints slot \"%s\", which the design does not declare",
              i, name ? name : "");
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        printf("usage: %s <dir holding label_sheet.bin and label_manifest.txt>\n", argv[0]);
        return 2;
    }

    char path[1024];
    snprintf(path, sizeof path, "%s/label_sheet.bin", argv[1]);
    size_t len = 0;
    uint8_t *bytes = slurp(path, &len);
    CHECK(bytes != NULL, "could not read %s", path);
    if (!bytes) return 1;

    /* LABEL_FONT_COUNT is the generated table's length, which is what the app passes: the
     * sheet's font indices are checked against the app's k_label_font[] and nothing else. */
    label_sheet_t *sheet = NULL;
    char why[192] = "";
    label_result_t r = label_open_why(bytes, len, LABEL_FONT_COUNT, &sheet, why, sizeof why);
    CHECK(r == LABEL_OK, "the packed sheet did not open: %s", why);
    if (r != LABEL_OK) {
        free(bytes);
        return 1;
    }

    snprintf(path, sizeof path, "%s/label_manifest.txt", argv[1]);
    FILE *m = fopen(path, "r");
    CHECK(m != NULL, "could not read %s", path);
    if (!m) {
        label_close(sheet);
        free(bytes);
        return 1;
    }

    char line[512];
    bool ok = fgets(line, sizeof line, m) != NULL;
    chomp(line);
    CHECK(ok && strcmp(line, "label_manifest\t2") == 0,
          "the manifest's first line is not \"label_manifest<TAB>2\": %s", ok ? line : "(empty)");

    unsigned wanted = 0;
    ok = fgets(line, sizeof line, m) != NULL;
    chomp(line);
    CHECK(ok && strncmp(line, "count\t", 6) == 0, "the manifest's second line is not \"count\": %s",
          ok ? line : "(empty)");
    if (ok && strncmp(line, "count\t", 6) == 0) wanted = (unsigned)strtoul(line + 6, NULL, 10);

    CHECK_EQ(label_templates(sheet), wanted, "the sheet's template count");
    CHECK(wanted > 0, "the manifest describes no designs");

    for (unsigned i = 0; i < wanted; i++) {
        ok = fgets(line, sizeof line, m) != NULL;
        CHECK(ok, "the manifest ends at template %u", i);
        if (!ok) break;
        chomp(line);

        char *f[6];
        bool split = split6(line, f);
        CHECK(split, "template %u: the manifest line has fewer than six fields", i);
        if (!split) continue;

        CHECK_EQ(strtoul(f[0], NULL, 10), i, "a manifest line's index");
        CHECK_EQ(label_prim_count(sheet, i), strtoul(f[1], NULL, 10),
                 "a template's prim count");
        CHECK_EQ(label_slot_count(sheet, i), strtoul(f[2], NULL, 10),
                 "a template's Slot count");

        uint32_t rgb = 0;
        const bool has_canvas = label_canvas(sheet, i, &rgb);
        if (strcmp(f[3], "-") == 0) {
            CHECK(!has_canvas, "template %u: the manifest has no canvas, the sheet one", i);
        } else {
            CHECK(f[3][0] == '#' && strlen(f[3]) == 7,
                  "template %u: the manifest canvas \"%s\" is not #RRGGBB", i, f[3]);
            CHECK(has_canvas, "template %u: the manifest has a canvas, the sheet none", i);
            CHECK_EQ(rgb, (uint32_t)strtoul(f[3] + 1, NULL, 16),
                     "a template's canvas");
        }

        const char *name = label_template_name(sheet, i);
        CHECK(name != NULL && strcmp(name, f[5]) == 0,
              "template %u: the sheet says \"%s\", the manifest \"%s\"", i, name ? name : "(null)",
              f[5]);

        check_slots(sheet, i, f[4]);
        check_declared_slots(sheet, i);
    }

    CHECK(fgets(line, sizeof line, m) == NULL, "the manifest has more lines than it counts");

    fclose(m);
    label_close(sheet);
    free(bytes);
    (void)why;
    TEST_END();
}
