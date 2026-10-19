/*
 * T17: the cassette's arithmetic, without LVGL.
 *
 * The two reels' speeds, the hold when stopped, the pack fill, the progress throttle
 * and the label clean are all rules a panel used to be the only witness to. Each is
 * pinned here by feeding status snapshots: no display, no serial dump.
 */
#include "test_util.h"

#include <math.h>

#include "cassette.h"

static mini_icecast_status_t mkstatus(const char *title, const char *file,
                                      uint32_t elapsed, uint32_t duration, float level)
{
    mini_icecast_status_t st;
    memset(&st, 0, sizeof st);
    st.format = (file && file[0]) ? MI_FMT_MP3 : MI_FMT_NONE;
    snprintf(st.title, sizeof st.title, "%s", title ? title : "");
    snprintf(st.file, sizeof st.file, "%s", file ? file : "");
    st.elapsed_ms = elapsed;
    st.duration_ms = duration;
    st.level_l = level;
    st.level_r = level;
    return st;
}

/* How many times one reel's art frame changes over a position window. A faster
 * reel changes frames more often; this is the speed the domain cares about. */
static int frame_changes(cassette_t *c, uint32_t from, uint32_t to, uint32_t duration,
                         uint32_t step, int reel)
{
    cassette_reset(c);
    mini_icecast_status_t st = mkstatus("Song", "song.mp3", from, duration, 0.1f);
    const cassette_frame_t *f = cassette_step(c, &st, from);
    int prev = f->hub_frame[reel];
    int changes = 0;
    for (uint32_t t = from + step; t <= to; t += step) {
        st = mkstatus("Song", "song.mp3", t, duration, 0.1f);
        f = cassette_step(c, &st, t);
        if (f->hub_frame[reel] != prev) {
            changes++;
            prev = f->hub_frame[reel];
        }
    }
    return changes;
}

static void test_initial_and_progress(void)
{
    /* bar_period 0: the progress may update on every step, which makes the
     * elapsed -> permille mapping observable. */
    cassette_t *c = cassette_new(20, 0);
    CHECK(c != NULL, "a 20-frame reel is accepted");
    if (!c) return;

    mini_icecast_status_t st = mkstatus("(live, 1984, Le Spectrum) Song", "song.mp3", 0, 10000, 0.5f);
    const cassette_frame_t *f = cassette_step(c, &st, 0);
    CHECK(f != NULL, "a frame comes back");
    CHECK(strcmp(f->title, "Song") == 0, "the parenthesised aside is dropped: '%s'", f->title);
    CHECK(f->title_changed, "the first frame reports the label changed");
    CHECK(f->playing, "a file and a format mean playing");
    CHECK_EQ(f->permille, 0, "the position starts at zero");
    CHECK(f->permille_changed, "the first frame reports the bar changed");
    CHECK_EQ(f->pack_fill[0], 1000, "the supply pack starts full");
    CHECK_EQ(f->pack_fill[1], 0, "the take-up pack starts empty");
    CHECK_EQ(f->hub_frame[0], 0, "the phase starts at zero");
    CHECK_EQ(f->hub_frame[1], 0, "both reels start at the same phase");
    CHECK(f->level_l == 0.5f && f->level_r == 0.5f, "the level passes through");
    CHECK(f->vu_l == 0.5f && f->vu_r == 0.5f, "the VU starts at the level");

    /* The same title again: no label work. */
    st = mkstatus("(live, 1984, Le Spectrum) Song", "song.mp3", 1000, 10000, 0.5f);
    f = cassette_step(c, &st, 100);
    CHECK(!f->title_changed, "an unchanged title is not reported changed");
    CHECK_EQ(f->permille, 100, "one second of ten is 100 permille");
    CHECK(f->permille_changed, "and the bar is told to move");
    CHECK_EQ(f->pack_fill[0], 900, "the supply pack shrinks");
    CHECK_EQ(f->pack_fill[1], 100, "the take-up pack grows");

    st = mkstatus("Song", "song.mp3", 20000, 10000, 0.5f);
    f = cassette_step(c, &st, 200);
    CHECK_EQ(f->permille, 1000, "a position past the end is clamped to 1000");
    CHECK_EQ(f->pack_fill[0], 0, "an empty supply pack");
    CHECK_EQ(f->pack_fill[1], 1000, "a full take-up pack");

    st = mkstatus("Other Song", "other.mp3", 0, 5000, 0.25f);
    f = cassette_step(c, &st, 300);
    CHECK(f->title_changed, "a new title is reported changed");
    CHECK(strcmp(f->title, "Other Song") == 0, "and it is the new label: '%s'", f->title);
    CHECK(f->level_l == 0.25f, "the level follows the new status");

    /* A title that cleans to nothing must not crash or leave the buffer dirty. */
    st = mkstatus("(an aside)", "x.mp3", 0, 1000, 0);
    f = cassette_step(c, &st, 400);
    CHECK(strcmp(f->title, "") == 0, "a title with only an aside cleans to empty");
    CHECK(f->playing, "it is still a playing track");

    /* An empty status: not playing, and the label falls back to empty. */
    st = mkstatus("", "", 0, 0, 0);
    f = cassette_step(c, &st, 500);
    CHECK(!f->playing, "no file means not playing");
    CHECK_EQ(f->permille, 0, "no duration means no position");
    CHECK_EQ(f->pack_fill[0], 1000, "the packs reset with no track");

    cassette_free(c);
}

static void test_reels(void)
{
    cassette_t *c = cassette_new(20, 1000);
    CHECK(c != NULL, "the reel model starts");
    if (!c) return;

    /* A full supply and an empty take-up: the take-up is at the hub, the supply
     * at the full pack, so their frames differ. */
    mini_icecast_status_t st = mkstatus("Song", "song.mp3", 0, 600000, 0.1f);
    const cassette_frame_t *f = cassette_step(c, &st, 0);
    CHECK_EQ(f->hub_frame[0], 0, "the phase starts at zero");
    CHECK_EQ(f->hub_frame[1], 0, "both reels start at the same phase");

    /* Over the first tenth of the side the take-up turns faster than the supply. */
    int supply = frame_changes(c, 0, 60000, 600000, 25, 0);
    int takeup = frame_changes(c, 0, 60000, 600000, 25, 1);
    CHECK(takeup > supply, "the empty take-up turns faster than the full supply (%d > %d)",
          takeup, supply);

    /* At half the side both packs have the same radius, so the reels turn at
     * the same speed even though their phases are different. */
    int mid_supply = frame_changes(c, 300000, 310000, 600000, 25, 0);
    int mid_takeup = frame_changes(c, 300000, 310000, 600000, 25, 1);
    int mid_diff = mid_supply - mid_takeup;
    if (mid_diff < 0) mid_diff = -mid_diff;
    CHECK(mid_diff <= 1, "the reels turn at the same speed at half the side (%d vs %d)",
          mid_supply, mid_takeup);

    /* At the end the supply is empty and the take-up full: the supply is faster. */
    supply = frame_changes(c, 540000, 600000, 600000, 25, 0);
    takeup = frame_changes(c, 540000, 600000, 600000, 25, 1);
    CHECK(supply > takeup, "the empty supply turns faster than the full take-up (%d > %d)",
          supply, takeup);

    /* Stopped: the frames hold, and a status-less step does not move them. */
    mini_icecast_status_t idle = mkstatus("", "", 0, 0, 0);
    f = cassette_step(c, &idle, 600000);
    int held0 = f->hub_frame[0], held1 = f->hub_frame[1];
    f = cassette_step(c, NULL, 600025);
    CHECK_EQ(f->hub_frame[0], held0, "a stopped track holds the supply frame");
    CHECK_EQ(f->hub_frame[1], held1, "and the take-up frame");

    /* The same position at a different wall time is the same frame: the reels
     * follow the tape, not the call cadence. */
    st = mkstatus("Song", "song.mp3", 150000, 600000, 0.1f);
    f = cassette_step(c, &st, 0);
    int a0 = f->hub_frame[0], a1 = f->hub_frame[1];
    f = cassette_step(c, &st, 123456);
    CHECK_EQ(f->hub_frame[0], a0, "the supply frame does not depend on now_ms");
    CHECK_EQ(f->hub_frame[1], a1, "the take-up frame does not depend on now_ms");

    cassette_free(c);
}

static void test_bar_throttle(void)
{
    cassette_t *c = cassette_new(20, 1000);
    CHECK(c != NULL, "the throttled model starts");
    if (!c) return;

    mini_icecast_status_t st = mkstatus("Song", "song.mp3", 0, 10000, 0.1f);
    const cassette_frame_t *f = cassette_step(c, &st, 0);
    CHECK(f->permille_changed && f->permille == 0, "the first position is reported");

    st = mkstatus("Song", "song.mp3", 1000, 10000, 0.1f);
    f = cassette_step(c, &st, 100);
    CHECK_EQ(f->permille, 0, "inside the period the bar keeps its value");
    CHECK(!f->permille_changed, "and is not told to move");

    st = mkstatus("Song", "song.mp3", 2000, 10000, 0.1f);
    f = cassette_step(c, &st, 1100);
    CHECK_EQ(f->permille, 200, "the next period takes the new position");
    CHECK(f->permille_changed, "and reports it");

    st = mkstatus("Song", "song.mp3", 3000, 10000, 0.1f);
    f = cassette_step(c, &st, 1500);
    CHECK_EQ(f->permille, 200, "a mid-period change waits");

    f = cassette_step(c, &st, 2100);
    CHECK_EQ(f->permille, 300, "and lands on the next period");

    cassette_free(c);
}

static void test_reset(void)
{
    cassette_t *c = cassette_new(20, 1000);
    mini_icecast_status_t st = mkstatus("Song", "song.mp3", 5000, 10000, 0.4f);
    cassette_step(c, &st, 0);
    cassette_step(c, NULL, 1000);

    cassette_reset(c);
    const cassette_frame_t *f = cassette_step(c, NULL, 5000);
    CHECK(strcmp(f->title, "") == 0, "a reset clears the label");
    CHECK(f->title_changed, "and reports it changed, so the adapter redraws it");
    CHECK(!f->playing, "nothing is playing after a reset");
    CHECK_EQ(f->permille, 0, "the bar is back to zero");
    CHECK(f->permille_changed, "and is reported changed");
    CHECK_EQ(f->pack_fill[0], 1000, "the supply pack is full again");
    CHECK_EQ(f->hub_frame[0], 0, "the reels are back at frame 0");
    CHECK(f->vu_l == 0.0f && f->vu_r == 0.0f, "and the VU is back at zero");
    cassette_free(c);
}

static void test_vu(void)
{
    cassette_t *c = cassette_new(20, 1000);
    CHECK(c != NULL, "the VU model starts");
    if (!c) return;

    mini_icecast_status_t st = mkstatus("Song", "song.mp3", 0, 600000, 0.8f);
    const cassette_frame_t *f = cassette_step(c, &st, 0);
    CHECK(fabsf(f->vu_l - 0.8f) < 0.001f, "a loud chunk shows on the VU");
    CHECK(fabsf(f->vu_r - 0.8f) < 0.001f, "both channels");

    st = mkstatus("Song", "song.mp3", 25, 600000, 0.2f);
    f = cassette_step(c, &st, 25);
    CHECK(f->vu_l > 0.2f, "the peak holds above a quiet chunk");
    CHECK(f->vu_l < 0.8f, "but starts to fall");

    float prev = f->vu_l;
    for (int k = 0; k < 20; k++) {
        st = mkstatus("Song", "song.mp3", 50 + k * 25, 600000, 0.2f);
        f = cassette_step(c, &st, 50 + k * 25);
        CHECK(f->vu_l <= prev + 0.0001f, "the hold falls monotonically");
        prev = f->vu_l;
    }
    CHECK(f->vu_l < 0.5f, "and has fallen after half a second");

    st = mkstatus("Song", "song.mp3", 600, 600000, 0.9f);
    f = cassette_step(c, &st, 600);
    CHECK(fabsf(f->vu_l - 0.9f) < 0.001f, "a new peak attacks immediately");

    cassette_reset(c);
    f = cassette_step(c, NULL, 700);
    CHECK(f->vu_l == 0.0f && f->vu_r == 0.0f, "a reset clears the VU");

    cassette_free(c);
}

int main(void)
{
    test_initial_and_progress();
    test_reels();
    test_bar_throttle();
    test_reset();
    test_vu();

    CHECK(cassette_new(0, 1000) == NULL, "no frames means no reel");

    TEST_END();
}
