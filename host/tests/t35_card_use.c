/*
 * T35: the card-file gate.
 *
 * This was four functions on `app_services`, between the boot ordering and the route
 * handlers, and its only exercise was an acceptance test that mounted and removed a card and
 * could not say anything about the gate itself. What it protects is a panic - FATFS unmounts
 * by forgetting the volume, and a directory lookup after that walks freed structure - so the
 * protocol is worth pinning: who may enter, what a close waits for, and what a timeout leaves
 * behind.
 */
#include "test_util.h"

#include "card_use.h"

int main(void)
{
    /* Before the gate exists there is nothing to close, so nothing may be refused. */
    CHECK(card_use_begin(), "before init, a reader is not refused");
    card_use_end(); /* and the count it never took is not decremented into the negative */
    CHECK_EQ(card_use_users(), 0, "before init, nobody is counted");

    CHECK_EQ(card_use_init(), CARD_USE_OK, "the gate is created");
    CHECK_EQ(card_use_users(), 0, "an open gate has nobody inside");

    CHECK(card_use_begin(), "a card file may be opened");
    CHECK_EQ(card_use_users(), 1, "and it is counted");
    card_use_end();
    CHECK_EQ(card_use_users(), 0, "and released");

    /* An idle card closes at once, and the gate then refuses the next reader. */
    CHECK(card_use_quiesce(0), "an idle card closes without waiting");
    CHECK(!card_use_begin(), "a closed gate refuses a reader");
    card_use_resume();
    CHECK(card_use_begin(), "after a mount it accepts one again");

    /* A card with a file open waits, times out, and stays shut: the volume must not be
     * unmounted, so every later reader is refused until a real mount resumes it. */
    CHECK(!card_use_quiesce(5), "a close waits for the open file and gives up");
    CHECK(!card_use_begin(), "and the gate stays shut after the timeout");

    card_use_end();
    CHECK(card_use_quiesce(5), "the close completes once the file is released");
    card_use_resume();

    /* A give left over from an earlier close must not satisfy a later one. */
    CHECK(card_use_begin(), "two readers can share the card");
    CHECK(card_use_begin(), "one more");
    CHECK_EQ(card_use_users(), 2, "both are counted");
    card_use_end();
    card_use_end();
    CHECK(card_use_quiesce(5), "the last one out wakes the close");
    card_use_resume();
    CHECK(card_use_begin(), "and the gate opens again");
    CHECK(!card_use_quiesce(5), "a stale wake does not close a gate with a reader inside");
    card_use_end();

    TEST_END();
}
