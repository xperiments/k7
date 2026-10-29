/*
 * The Cassette screen's design grid, without LVGL.
 *
 * The view used to convert design coordinates through a file-static pose and three
 * macros. This pins the conversion - the identity grid, the centring at a smaller
 * scale, the offset, the three named poses, the even-diameter rule and the
 * interpolation the animator eases - and the frame the module now owns: the badge,
 * the two bands, the window, the Pad and the block; the two bars' cells and their
 * hit rule; the Menu's page and its five slots; and the edge-derived conversion of
 * a design rect. No display, no serial dump.
 */
#include "test_util.h"

#include "cassette_layout.h"

static void test_identity(void)
{
    cassette_pose_t p = cassette_pose_entry();

    CHECK_EQ(cassette_len(p, CASSETTE_DESIGN_W), 800, "the full width");
    CHECK_EQ(cassette_len(p, CASSETTE_DESIGN_H), 480, "the full height");
    CHECK_EQ(cassette_origin_x(p), 0, "no margin on x");
    CHECK_EQ(cassette_origin_y(p), 0, "no margin on y");
    CHECK_EQ(cassette_x(p, 216), 216, "a hub's x");
    CHECK_EQ(cassette_y(p, 206), 206, "a hub's y");
    CHECK_EQ(cassette_len_even(p, 39), 38, "an odd design length rounds down to even");
}

static void test_half_scale_centres(void)
{
    cassette_pose_t p = cassette_pose(128, 0, 0);

    CHECK_EQ(cassette_len(p, 800), 400, "half the width");
    CHECK_EQ(cassette_len(p, 480), 240, "half the height");
    CHECK_EQ(cassette_origin_x(p), 200, "the box is centred on x");
    CHECK_EQ(cassette_origin_y(p), 120, "the box is centred on y");
    CHECK_EQ(cassette_x(p, 0), 200, "the left edge");
    CHECK_EQ(cassette_x(p, 800), 600, "the right edge");
    CHECK_EQ(cassette_y(p, 0), 120, "the top edge");
    CHECK_EQ(cassette_y(p, 480), 360, "the bottom edge");
    CHECK_EQ(cassette_x(p, 216), 308, "a hub's x moves with the box");
    CHECK_EQ(cassette_y(p, 206), 223, "a hub's y moves with the box");
}

static void test_offset_is_screen_pixels(void)
{
    cassette_pose_t p = cassette_pose(CASSETTE_SCALE_NONE, 10, -20);

    CHECK_EQ(cassette_origin_x(p), 10, "the offset is not scaled");
    CHECK_EQ(cassette_origin_y(p), -20, "a negative offset");
    CHECK_EQ(cassette_x(p, 0), 10, "the left edge");
    CHECK_EQ(cassette_y(p, 0), -20, "the top edge");
    CHECK_EQ(cassette_x(p, 800), 810, "the right edge");
}

static void test_label_peek_pose(void)
{
    /* What the Peek uses: 0.78x, moved up so the bar clears the tape. */
    cassette_pose_t p = cassette_pose_peek();

    CHECK_EQ(cassette_len(p, 800), 625, "the peek width");
    CHECK_EQ(cassette_origin_x(p), 87, "the peek is centred");
    CHECK_EQ(cassette_len(p, 480), 375, "the peek height");
    CHECK_EQ(cassette_origin_y(p), -8, "the peek's origin includes the lift");
    CHECK_EQ(cassette_y(p, 0), -8, "the top edge is lifted with it");
    CHECK_EQ(cassette_len(p, 568), 443, "the Label's width at the peek");
    CHECK_EQ(cassette_len(p, 52), 40, "the Label's height at the peek");
    CHECK_EQ(cassette_len_even(p, 39), 30, "the roller at the peek");
}

static void test_transport_drawer_pose(void)
{
    /* What the controls use: 0.52x, pulled 51 px left so the tape starts at 148 px
     * instead of the centred 199, and lifted 20 px onto the button column. */
    cassette_pose_t p = cassette_pose_controls();

    CHECK_EQ(cassette_len(p, 800), 418, "the design box at the drawer's scale");
    CHECK_EQ(cassette_origin_x(p), 140, "the box is 51 px left of centre");
    CHECK_EQ(cassette_x(p, 16), 148, "the tape's outer frame starts at 148");
    CHECK_EQ(cassette_x(p, 784), 550, "and ends at 550");
    CHECK_EQ(cassette_x(p, 784) - cassette_x(p, 16), 402, "the tape is 402 px wide");
    CHECK_EQ(cassette_len(p, 480), 251, "the scaled height is odd, so it truncates");
    CHECK_EQ(cassette_origin_y(p), 94, "the box's top, centring then the -20 px lift");
    CHECK_EQ(cassette_y(p, 16), 102, "the frame's top edge");
    CHECK_EQ(cassette_y(p, 460), 334, "the bottom chips, the lowest ink");
    CHECK_EQ(p.dx, -51, "the drawer's horizontal offset is kept");
}

static void test_rounding(void)
{
    cassette_pose_t p200 = cassette_pose(200, 0, 0);
    cassette_pose_t p128 = cassette_pose(128, 0, 0);

    CHECK_EQ(cassette_len(p200, 1), 0, "a design pixel can round to nothing");
    CHECK_EQ(cassette_len(p200, 2), 1, "and the next one does not");
    CHECK_EQ(cassette_len(p128, 39), 19, "an odd truncated length");
    CHECK_EQ(cassette_len_even(p128, 39), 18, "rounded down to even");
    CHECK_EQ(cassette_even(0), 0, "zero stays zero");
    CHECK_EQ(cassette_even(7), 6, "seven rounds to six");
    CHECK_EQ(cassette_even(8), 8, "an even length is left alone");
}

static void test_scale_clamp(void)
{
    CHECK_EQ(cassette_pose(1, 0, 0).scale, CASSETTE_SCALE_MIN, "1 clamps to the floor");
    CHECK_EQ(cassette_pose(0, 0, 0).scale, CASSETTE_SCALE_MIN, "0 clamps to the floor");
    CHECK_EQ(cassette_pose(2000, 0, 0).scale, CASSETTE_SCALE_MAX, "2000 clamps to the ceiling");
    CHECK_EQ(cassette_pose(CASSETTE_SCALE_NONE, 0, 0).scale, CASSETTE_SCALE_NONE,
             "the design scale is left alone");
}

static void test_lerp(void)
{
    cassette_pose_t from = cassette_pose(CASSETTE_SCALE_NONE, 0, 0);
    cassette_pose_t to = cassette_pose(200, 0, -60);
    cassette_pose_t mid = cassette_pose_lerp(from, to, 512);

    CHECK_EQ(cassette_lerp(0, 100, 0), 0, "step 0 is the from value");
    CHECK_EQ(cassette_lerp(0, 100, 1024), 100, "step 1024 is the to value");
    CHECK_EQ(cassette_lerp(0, 100, 512), 50, "half way");
    CHECK_EQ(cassette_lerp(-60, 0, 256), -45, "a negative direction");

    CHECK_EQ(mid.scale, 228, "the scale is mixed");
    CHECK_EQ(mid.dx, 0, "the x offset is unchanged");
    CHECK_EQ(mid.dy, -30, "the y offset is mixed");

    mid = cassette_pose_lerp(from, to, 0);
    CHECK_EQ(mid.scale, 256, "the animation starts from the from pose");
    mid = cassette_pose_lerp(from, to, 1024);
    CHECK_EQ(mid.scale, 200, "and lands on the to pose");
    CHECK_EQ(mid.dy, -60, "with no rounding left behind");
}

static void test_frame(void)
{
    cassette_rect_t badge = cassette_part_rect(CASSETTE_PART_BADGE);
    cassette_rect_t header = cassette_part_rect(CASSETTE_PART_HEADER);
    cassette_rect_t footer = cassette_part_rect(CASSETTE_PART_FOOTER);
    cassette_rect_t window = cassette_part_rect(CASSETTE_PART_WINDOW);
    cassette_rect_t pad = cassette_part_rect(CASSETTE_PART_PAD);
    cassette_rect_t block = cassette_part_rect(CASSETTE_PART_BLOCK);
    cassette_rect_t back = cassette_part_rect(CASSETTE_PART_BACK);

    CHECK_EQ(badge.x, 16, "the badge starts on the 16 px margin");
    CHECK_EQ(badge.y, 6, "the badge is on the 6 px top margin");
    CHECK_EQ(badge.w, 96, "the badge is one cell wide");
    CHECK_EQ(badge.h, 72, "the badge is as tall as the band");
    CHECK_EQ(header.x, badge.x + badge.w - CASSETTE_BORDER_W, "the badge's border divides the header");
    CHECK_EQ(header.y, 6, "the band starts on the top margin");
    CHECK_EQ(header.w, 674, "the band runs from the badge to 784");
    CHECK_EQ(header.h, 72, "the band's border lands on the cells' top line");
    CHECK_EQ(header.x + header.w, 784, "the frame's right edge");
    CHECK_EQ(window.x, header.x, "the window starts on the band's left edge");
    CHECK_EQ(window.y, 76, "the window starts on the cells' top line");
    CHECK_EQ(window.w, 486, "the window is 768 - 3 pitches");
    CHECK_EQ(window.h, 284, "the window is three rows");
    CHECK_EQ(window.x + window.w, pad.x, "the window's right edge is the Pad's left");
    CHECK_EQ(block.x + CASSETTE_BORDER_W, pad.x, "the Pad is the block inset 2");
    CHECK_EQ(block.x, 594, "the block's drawn ink");
    CHECK_EQ(block.w, 190, "two columns share a border");
    CHECK_EQ(block.x + block.w, 784, "the block ends on the frame's right edge");
    CHECK_EQ(window.y + window.h, 360, "the window's bottom line");
    CHECK_EQ(window.y + window.h - CASSETTE_BORDER_W, footer.y, "the footer meets the window");
    CHECK_EQ(footer.x, 16, "the footer starts on the 16 px margin");
    CHECK_EQ(footer.w, 768, "the footer is the frame's width");
    CHECK_EQ(footer.h, 116, "the footer runs to 474");
    CHECK_EQ(footer.y + footer.h, 474, "the footer ends on the 6 px bottom margin");
    CHECK_EQ(header.h + window.h + footer.h - 2 * CASSETTE_BORDER_W, 468, "the vertical chain");
    CHECK_EQ(badge.w + header.w - CASSETTE_BORDER_W, footer.w, "the composite header is 768");
    CHECK_EQ(badge.w + window.w + block.w - 2 * CASSETTE_BORDER_W, 768, "the width tiles");
    CHECK_EQ(back.x, 16, "the back cover is the Page's 16 px ring");
    CHECK_EQ(back.w, 768, "and the frame's width");
    CHECK_EQ(back.h, 448, "and 480 - 2 * 16");
    CHECK_EQ(cassette_part_rect(CASSETTE_PART_COUNT).w, 0, "an unknown part is empty");
}

/* The timeline is one instrument drawn in two places - the Peek's bar and the Menu's footer - so
 * its numbers are shared and pinned here: a drifting copy is the whole thing this prevents. */
static void test_timeline(void)
{
    CHECK_EQ(CASSETTE_TIMELINE_X, 116, "the bar's left");
    CHECK_EQ(CASSETTE_TIMELINE_Y, 424, "the bar's top at rest");
    CHECK_EQ(CASSETTE_TIMELINE_HIDDEN_Y, 488, "and where its slide begins");
    CHECK_EQ(CASSETTE_TIMELINE_W, 568, "the bar is centred in the 800 px panel");
    CHECK_EQ(CASSETTE_TIMELINE_H, 52, "the bar's height");
    CHECK_EQ(CASSETTE_TIMELINE_X + CASSETTE_TIMELINE_W, 684, "and its right edge");

    CHECK_EQ(CASSETTE_TIMELINE_PAD, 20, "the time boxes' inset");
    CHECK_EQ(CASSETTE_TIMELINE_TIME_W, 110, "their width");
    CHECK_EQ(CASSETTE_TIMELINE_GAP, 10, "the gap to the track");
    CHECK_EQ(CASSETTE_TIMELINE_TRACK_X, 140, "so the track starts 140 in");
    CHECK_EQ(CASSETTE_TIMELINE_TRACK_W, 288, "and is 288 wide, symmetric in the bar");
    CHECK_EQ(CASSETTE_TIMELINE_TRACK_X * 2 + CASSETTE_TIMELINE_TRACK_W,
             CASSETTE_TIMELINE_W, "the two insets and the track fill the bar");
    CHECK_EQ(CASSETTE_TIMELINE_TRACK_H, 6, "the track's height");

    /* The bar sits inside the footer band, near its bottom edge, which is why the footer draws it
     * as a screen child and not inside the band: 2 px of the bar hang below the band. */
    cassette_rect_t band = cassette_part_rect(CASSETTE_PART_FOOTER);
    CHECK(CASSETTE_TIMELINE_Y >= band.y, "the bar is inside the band's top");
    CHECK(CASSETTE_TIMELINE_Y + CASSETTE_TIMELINE_H <= CASSETTE_DESIGN_H,
          "and its bottom is on the panel");
}

static void test_cells(void)
{
    const cassette_bar_geometry_t *left = cassette_bar_geometry(CASSETTE_BAR_LEFT);
    const cassette_bar_geometry_t *right = cassette_bar_geometry(CASSETTE_BAR_RIGHT);
    cassette_rect_t r;

    CHECK(left != NULL && right != NULL, "both bars have a geometry");
    CHECK_EQ(left->cell_x, 16, "the left bar starts on the margin");
    CHECK_EQ(left->columns, 1, "one column");
    CHECK_EQ(left->hidden_dx, -120, "the left bar slides out by its panel");
    CHECK_EQ(right->cell_x, 594, "the right bar starts at the block's ink");
    CHECK_EQ(right->columns, 2, "two columns");
    CHECK_EQ(right->hidden_dx, 206, "the right bar's left edge lands on 800");

    r = cassette_cell_rect(CASSETTE_BAR_LEFT, 0, 0);
    CHECK_EQ(r.x, 16, "the first cell");
    CHECK_EQ(r.y, 76, "on the cells' top line");
    CHECK_EQ(r.w, 96, "96 wide");
    CHECK_EQ(r.h, 96, "96 tall");
    r = cassette_cell_rect(CASSETTE_BAR_LEFT, 0, 2);
    CHECK_EQ(r.y, 264, "the third row is two pitches down");
    r = cassette_cell_rect(CASSETTE_BAR_RIGHT, 1, 2);
    CHECK_EQ(r.x, 688, "the second column");
    CHECK_EQ(r.y, 264, "the third row");
    CHECK_EQ(r.x + r.w, 784, "the block's right edge");
    r = cassette_cell_rect(CASSETTE_BAR_RIGHT, 0, 0);
    CHECK_EQ(r.x + r.w, 690, "the drawn carcase overlaps its neighbour by 2 px");
    r = cassette_cell_rect(CASSETTE_BAR_RIGHT, 2, 0);
    CHECK_EQ(r.w, 0, "an out-of-range column is empty, not an alias of the origin");
    CHECK_EQ(r.x, 0, "and has no position");

    CHECK_EQ(cassette_cell_at(CASSETTE_BAR_LEFT, 16, 76), 0, "the first cell");
    CHECK_EQ(cassette_cell_at(CASSETTE_BAR_LEFT, 111, 169), 0, "the last pixel of the first cell");
    CHECK_EQ(cassette_cell_at(CASSETTE_BAR_LEFT, 112, 76), -1, "the drawer's margin is not a cell");
    CHECK_EQ(cassette_cell_at(CASSETTE_BAR_LEFT, 16, 360), -1, "below the last row");
    CHECK_EQ(cassette_cell_at(CASSETTE_BAR_RIGHT, 594, 76), 0, "the first cell");
    CHECK_EQ(cassette_cell_at(CASSETTE_BAR_RIGHT, 687, 76), 0, "the left column's last pixel");
    CHECK_EQ(cassette_cell_at(CASSETTE_BAR_RIGHT, 688, 76), 3, "the shared border belongs to the right column");
    CHECK_EQ(cassette_cell_at(CASSETTE_BAR_RIGHT, 688, 170), 4, "and to the row below");
    CHECK_EQ(cassette_cell_at(CASSETTE_BAR_RIGHT, 594, 170), 1, "the left column, second row");
    CHECK_EQ(cassette_cell_at(CASSETTE_BAR_RIGHT, 783, 359), 5, "the last cell's remaining pixels");
    CHECK_EQ(cassette_cell_at(CASSETTE_BAR_RIGHT, 784, 76), -1, "past the block");
    CHECK_EQ(cassette_cell_at(CASSETTE_BAR_RIGHT, 594, 360), -1, "below the block");
    CHECK(cassette_bar_geometry(CASSETTE_BAR_COUNT) == NULL, "an unknown bar has no geometry");
}

static void test_page(void)
{
    cassette_rect_t page = cassette_page_rect();

    CHECK_EQ(page.x, 112, "the window's inner left");
    CHECK_EQ(page.y, 78, "the window's inner top");
    CHECK_EQ(page.w, 482, "the window's inner width");
    CHECK_EQ(page.h, 280, "the window's inner height");
    CHECK_EQ(cassette_page_inset(), 6, "the row inset");
    CHECK_EQ(cassette_page_slot_y(0), 0, "the page's content starts at the inset");
    CHECK_EQ(cassette_page_slot_y(1), 54, "the second slot");
    CHECK_EQ(cassette_page_slot_y(2), 107, "the third slot");
    CHECK_EQ(cassette_page_slot_y(3), 161, "the fourth slot");
    CHECK_EQ(cassette_page_slot_y(4), 214, "the fifth slot");
    CHECK_EQ(cassette_page_slot_y(5), 268, "the content's height");
    CHECK_EQ(cassette_page_slot_h(0), 54, "the rest slot's height is the ratchet pitch");
    CHECK_EQ(cassette_page_slot_h(1), 53, "the second slot");
    CHECK_EQ(cassette_page_slot_h(2), 54, "the third slot");
    CHECK_EQ(cassette_page_slot_h(3), 53, "the fourth slot");
    CHECK_EQ(cassette_page_slot_h(4), 54, "the fifth slot");
    CHECK_EQ(cassette_page_slot_h(5), 0, "there is no sixth slot");
    CHECK_EQ(cassette_page_slot_h(0) + cassette_page_slot_h(1) + cassette_page_slot_h(2) +
                 cassette_page_slot_h(3) + cassette_page_slot_h(4),
             268, "the five slots fill the content");
}

static void test_named_poses(void)
{
    cassette_pose_t entry = cassette_pose_entry();
    cassette_pose_t peek = cassette_pose_peek();
    cassette_pose_t controls = cassette_pose_controls();

    CHECK_EQ(entry.scale, CASSETTE_SCALE_NONE, "the entry is the design scale");
    CHECK_EQ(entry.dx, 0, "with no offset");
    CHECK_EQ(entry.dy, 0, "on either axis");
    CHECK_EQ(peek.scale, CASSETTE_SCALE_PEEK, "the Peek's scale");
    CHECK_EQ(peek.dy, CASSETTE_PEEK_DY, "and its lift");
    CHECK_EQ(controls.scale, CASSETTE_SCALE_CONTROLS, "the Controls' scale");
    CHECK_EQ(controls.dx, CASSETTE_CONTROLS_DX, "and its pull left");
    CHECK_EQ(controls.dy, CASSETTE_CONTROLS_DY, "and its lift");
    CHECK_EQ(cassette_x(controls, 16), 148, "the named Controls pose still starts the tape at 148");
    CHECK_EQ(cassette_x(controls, 784), 550, "and ends at 550");
}

static void test_screen_rect(void)
{
    cassette_pose_t controls = cassette_pose_controls();
    cassette_rect_t span = { 16, 16, 80, 80 };
    cassette_rect_t screen = cassette_screen_rect(controls, span);

    CHECK_EQ(screen.x, 148, "the design span's left edge");
    CHECK_EQ(screen.y, 102, "its top edge");
    CHECK_EQ(screen.w, 42, "the edges are converted first, so the width is 42");
    CHECK_EQ(screen.h, 42, "and so is the height");
    CHECK_EQ(cassette_screen_rect(cassette_pose_entry(), cassette_tape_box()).w, 768,
             "at entry the design box is pixel for pixel");
}

static void test_tape(void)
{
    cassette_rect_t box = cassette_tape_box();
    cassette_rect_t ink = cassette_tape_ink();

    CHECK_EQ(box.x, 16, "the tape's frame starts on the design margin");
    CHECK_EQ(box.y, 16, "on both axes");
    CHECK_EQ(box.w, 768, "the frame is 768 wide");
    CHECK_EQ(box.h, 360, "and three rows tall");
    CHECK_EQ(ink.h, 444, "the ink reaches the bottom chips");
    CHECK_EQ(ink.y + ink.h, 460, "the lowest ink");
    CHECK_EQ(cassette_tape_centre_x(), 400, "the centre the edge mark follows");
    CHECK_EQ(cassette_tape_centre_y(), 238, "on both axes");
}

int main(void)
{
    test_identity();
    test_half_scale_centres();
    test_offset_is_screen_pixels();
    test_label_peek_pose();
    test_transport_drawer_pose();
    test_rounding();
    test_scale_clamp();
    test_lerp();
    test_frame();
    test_cells();
    test_page();
    test_named_poses();
    test_screen_rect();
    test_timeline();
    test_tape();

    TEST_END();
}
