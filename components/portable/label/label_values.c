/*
 * label_values - see include/label_values.h and docs/specs/label-templates.md §5.
 *
 * One row per Track change, resolved in RAM: path -> the Library's node -> its size -> the
 * Snapshot's row. No tags are read and the card is not touched, which is why a design can be
 * re-texted on the screen task without the broadcast noticing.
 *
 * Formatting is the point of this module, not an afterthought: the panel prints "124", "8A"
 * and "relaxed", and each of those is a decision the spec records rather than a printf that
 * happened to fit.
 */
#include "label_values.h"

#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------------- the table */

static const char *const k_slot_names[LABEL_SLOT_COUNT] = {
    "song_title", "song_artist", "song_album", "genre", "bpm", "musical_key",
    "energy", "danceability", "valence", "arousal", "mood",
};

const char *label_values_name(label_slot_id_t id)
{
    return (id >= 0 && id < LABEL_SLOT_COUNT) ? k_slot_names[id] : "";
}

static int slot_index(const char *name)
{
    if (!name) return -1;
    for (int i = 0; i < LABEL_SLOT_COUNT; i++)
        if (strcmp(name, k_slot_names[i]) == 0) return i;
    return -1;
}

/* ---------------------------------------------------------------- helpers */

static void put(label_values_t *v, label_slot_id_t id, const char *text)
{
    snprintf(v->text[id], sizeof v->text[id], "%s", text ? text : "");
}

static void put_uint(label_values_t *v, label_slot_id_t id, unsigned value)
{
    snprintf(v->text[id], sizeof v->text[id], "%u", value);
}

/* A 0..100 percentage; 0xFF is the row's marker for "not measured", and prints nothing. */
static void put_pct(label_values_t *v, label_slot_id_t id, uint8_t pct)
{
    if (pct == SNAPSHOT_UNKNOWN_PCT) return;
    put_uint(v, id, (unsigned)(pct > 100u ? 100u : pct));
}

/* Latin-1's accented letters, decomposed: the base letter and the two UTF-8 bytes of its
 * combining mark, or zeros for the characters that do not decompose (the ligatures, eth, thorn,
 * the times and divide signs). U+00C0..U+00FF is the whole of what a music library's names use,
 * and decomposing more would buy a table and a shaper this module has no other use for. */
static const struct {
    char base;
    unsigned char mark[2];
} k_latin1_nfd[64] = {
    {'A', {0xCC, 0x80}}, {'A', {0xCC, 0x81}}, {'A', {0xCC, 0x82}}, {'A', {0xCC, 0x83}},
    {'A', {0xCC, 0x88}}, {'A', {0xCC, 0x8A}}, {0, {0, 0}},       {'C', {0xCC, 0xA7}},
    {'E', {0xCC, 0x80}}, {'E', {0xCC, 0x81}}, {'E', {0xCC, 0x82}}, {'E', {0xCC, 0x88}},
    {'I', {0xCC, 0x80}}, {'I', {0xCC, 0x81}}, {'I', {0xCC, 0x82}}, {'I', {0xCC, 0x88}},
    {0, {0, 0}},         {'N', {0xCC, 0x83}}, {'O', {0xCC, 0x80}}, {'O', {0xCC, 0x81}},
    {'O', {0xCC, 0x82}}, {'O', {0xCC, 0x83}}, {'O', {0xCC, 0x88}}, {0, {0, 0}},
    {0, {0, 0}},         {'U', {0xCC, 0x80}}, {'U', {0xCC, 0x81}}, {'U', {0xCC, 0x82}},
    {'U', {0xCC, 0x88}}, {'Y', {0xCC, 0x81}}, {0, {0, 0}},         {0, {0, 0}},
    {'a', {0xCC, 0x80}}, {'a', {0xCC, 0x81}}, {'a', {0xCC, 0x82}}, {'a', {0xCC, 0x83}},
    {'a', {0xCC, 0x88}}, {'a', {0xCC, 0x8A}}, {0, {0, 0}},         {'c', {0xCC, 0xA7}},
    {'e', {0xCC, 0x80}}, {'e', {0xCC, 0x81}}, {'e', {0xCC, 0x82}}, {'e', {0xCC, 0x88}},
    {'i', {0xCC, 0x80}}, {'i', {0xCC, 0x81}}, {'i', {0xCC, 0x82}}, {'i', {0xCC, 0x88}},
    {0, {0, 0}},         {'n', {0xCC, 0x83}}, {'o', {0xCC, 0x80}}, {'o', {0xCC, 0x81}},
    {'o', {0xCC, 0x82}}, {'o', {0xCC, 0x83}}, {'o', {0xCC, 0x88}}, {0, {0, 0}},
    {0, {0, 0}},         {'u', {0xCC, 0x80}}, {'u', {0xCC, 0x81}}, {'u', {0xCC, 0x82}},
    {'u', {0xCC, 0x88}}, {'y', {0xCC, 0x81}}, {0, {0, 0}},         {0, {0, 0}},
};

/* The path with its Latin-1 accents decomposed, or 0 when it has none (so the caller does not
 * hash it twice for nothing) or does not fit. */
static size_t nfd_path(const char *in, char *out, size_t cap)
{
    size_t o = 0;
    bool changed = false;
    for (const unsigned char *p = (const unsigned char *)in; *p; p++) {
        size_t n = 1;
        if (p[0] == 0xC3 && (p[1] & 0xC0) == 0x80) {
            const unsigned idx = (0xC0u | (p[1] & 0x3Fu)) - 0xC0u;
            if (k_latin1_nfd[idx].base) {
                if (o + 3 > cap) return 0;
                out[o++] = k_latin1_nfd[idx].base;
                out[o++] = (char)k_latin1_nfd[idx].mark[0];
                out[o++] = (char)k_latin1_nfd[idx].mark[1];
                changed = true;
                p++;
                continue;
            }
        }
        if (o + n > cap) return 0;
        out[o++] = (char)*p;
    }
    if (!changed) return 0;
    out[o] = '\0';
    return o;
}

bool label_row_for(const snapshot_t *snap, const char *relpath, uint32_t size,
                   snapshot_row_t *row)
{
    if (!snap || !relpath || !relpath[0] || !row) return false;
    if (snapshot_find(snap, snapshot_path_hash(relpath), size, row)) return true;

    char nfd[512];
    if (!nfd_path(relpath, nfd, sizeof nfd - 1)) return false;
    return snapshot_find(snap, snapshot_path_hash(nfd), size, row);
}

/* The row for a Track, when the Library still has the path and the Snapshot still matches
 * the file's size. The size is half the fingerprint, which is why it is asked for here. */
static bool row_for(const library_t *lib, const snapshot_t *snap, const char *relpath,
                    snapshot_row_t *row)
{
    if (!lib || !snap || !relpath || !relpath[0]) return false;

    lib_node_t n = library_find(lib, relpath);
    if (n == LIB_NODE_NONE || library_kind(lib, n) != LIB_TRACK) return false;

    uint64_t size = library_size(lib, n);
    if (size > UINT32_MAX) return false;   /* a row cannot represent it: unanalysed */

    return label_row_for(snap, relpath, (uint32_t)size, row);
}

/* ---------------------------------------------------------------- fill */

void label_values_clear(label_values_t *v)
{
    if (v) memset(v, 0, sizeof *v);
}

void label_values_fill(label_values_t *v, const library_t *lib, const snapshot_t *snap,
                       const char *relpath)
{
    if (!v) return;
    label_values_clear(v);

    snapshot_row_t row;
    if (!row_for(lib, snap, relpath, &row)) return;
    label_values_apply(v, snap, &row);
}

void label_values_apply(label_values_t *v, const snapshot_t *snap, const snapshot_row_t *row)
{
    if (!v) return;
    /* Cleared first: a slot the new row does not fill must print nothing, not the previous
     * Track's value. The caller may reuse one label_values_t for a whole side. */
    label_values_clear(v);
    if (!snap || !row) return;

    put(v, LABEL_SLOT_SONG_TITLE, snapshot_row_string(snap, row, SNAPSHOT_STRING_TITLE));
    put(v, LABEL_SLOT_SONG_ARTIST, snapshot_row_string(snap, row, SNAPSHOT_STRING_ARTIST));
    put(v, LABEL_SLOT_SONG_ALBUM, snapshot_row_string(snap, row, SNAPSHOT_STRING_ALBUM));
    put(v, LABEL_SLOT_GENRE, snapshot_row_string(snap, row, SNAPSHOT_STRING_GENRE));

    /* BPM is held as tenths; the panel prints a whole number, rounded, because the editor's
     * field holds three characters and "124.4" is not a cassette's BPM. */
    if (row->bpm_x10) put_uint(v, LABEL_SLOT_BPM, (row->bpm_x10 + 5u) / 10u);

    /* The Camelot code, the same string the MCP layer prints for the same Track. */
    if (row->key >= 1u && row->key <= 24u) {
        snprintf(v->text[LABEL_SLOT_MUSICAL_KEY], sizeof v->text[LABEL_SLOT_MUSICAL_KEY],
                 "%u%c", (unsigned)(((row->key - 1u) % 12u) + 1u), row->key > 12u ? 'B' : 'A');
    }

    put_pct(v, LABEL_SLOT_ENERGY, row->energy);
    put_pct(v, LABEL_SLOT_DANCEABILITY, row->danceability);
    put_pct(v, LABEL_SLOT_VALENCE, row->valence);
    put_pct(v, LABEL_SLOT_AROUSAL, row->arousal);

    /* One word, the lowest set bit: the image's order, so a Track with two moods always
     * prints the same one. */
    for (unsigned bit = 0; bit < 5; bit++) {
        if (!(row->mood_bits & (1u << bit))) continue;
        const char *name = snapshot_mood_name(bit);
        if (name) put(v, LABEL_SLOT_MOOD, name);
        break;
    }
}

const char *label_values_get(const label_values_t *v, const char *slot_name)
{
    if (!v) return "";
    int i = slot_index(slot_name);
    return i < 0 ? "" : v->text[i];
}
