/*
 * T17: the cassette's arithmetic, without LVGL.
 *
 * The reel loop, the clamped gap, the pack fill, the progress throttle and the
 * label clean are all rules a panel used to be the only witness to. Each is pinned
 * here by feeding a time series: no display, no serial dump.
 */
#include "test_util.h"

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

static void test_initial_and_progress(void)
{
    /* bar_period 0: the progress may update on every step, which makes the
     * elapsed -> permille mapping observable. */
    cassette_t *c = cassette_new(2000, 20, 0);
    CHECK(c != NULL, "a 2 s loop of 20 frames is accepted");
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
    CHECK_EQ(f->hub_frame[0], 0, "the reels start at frame 0");
    CHECK_EQ(f->hub_frame[1], 0, "both reels start at frame 0");
    CHECK(f->level_l == 0.5f && f->level_r == 0.5f, "the level passes through");

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
    cassette_t *c = cassette_new(2000, 20, 1000);
    CHECK(c != NULL, "the reel model starts");
    if (!c) return;

    mini_icecast_status_t st = mkstatus("Song", "song.mp3", 0, 10000, 0.1f);
    const cassette_frame_t *f = cassette_step(c, &st, 0);
    CHECK_EQ(f->hub_frame[0], 0, "the first step does not advance (no elapsed time)");

    f = cassette_step(c, NULL, 100);
    CHECK_EQ(f->hub_frame[0], 1, "100 ms advances one of twenty frames over 2 s");
    CHECK_EQ(f->hub_frame[1], 1, "both hubs advance together");

    for (int i = 2; i <= 19; i++) f = cassette_step(c, NULL, (uint32_t)i * 100);
    CHECK_EQ(f->hub_frame[0], 19, "the loop reaches its last frame at 1.9 s");
    f = cassette_step(c, NULL, 2000);
    CHECK_EQ(f->hub_frame[0], 0, "and wraps at 2 s");

    /* A long gap is clamped: 8 s of stall must not spin the reels 80 frames. */
    f = cassette_step(c, NULL, 10000);
    CHECK_EQ(f->hub_frame[0], 2, "an 8 s gap advances only the clamped 250 ms (two frames)");

    /* Stopped: time passes, the art does not move. */
    mini_icecast_status_t idle = mkstatus("", "", 0, 0, 0);
    f = cassette_step(c, &idle, 10000);
    f = cassette_step(c, NULL, 10100);
    CHECK_EQ(f->hub_frame[0], 2, "the reels stop when nothing is playing");

    /* Resume: no backlog is spent. */
    f = cassette_step(c, &st, 10200);
    CHECK_EQ(f->hub_frame[0], 3, "resuming advances from where it stopped");
    f = cassette_step(c, NULL, 10300);
    CHECK_EQ(f->hub_frame[0], 4, "and keeps advancing");

    cassette_free(c);
}

static void test_bar_throttle(void)
{
    cassette_t *c = cassette_new(2000, 20, 1000);
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
    cassette_t *c = cassette_new(2000, 20, 1000);
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
    cassette_free(c);
}

int main(void)
{
    test_initial_and_progress();
    test_reels();
    test_bar_throttle();
    test_reset();

    CHECK(cassette_new(0, 20, 1000) == NULL, "a zero loop cannot advance");
    CHECK(cassette_new(2000, 0, 1000) == NULL, "no frames means no reel");

    TEST_END();
}
