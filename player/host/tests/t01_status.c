/*
 * T01: the status light's rule, host tier.
 *
 * The four states and the order they are decided in are docs/specs/k7player.md R31's table;
 * what is checked here is the part that is easy to get wrong and invisible until a person is
 * looking at a board: that Playing needs *both* the stream and audio leaving an output, that
 * a missing speaker is Ready rather than a fault, and that Fault outranks everything and
 * clears back to whatever the rest of the state says.
 */
#include "test_util.h"

#include "player_status.h"

int main(void)
{
    player_status_t s;

    /* Nothing known yet: a device that has just booted is searching. */
    player_status_init(&s);
    CHECK_EQ(player_status_light(&s), PLAYER_LIGHT_SEARCHING, "a fresh status is searching");
    CHECK(strcmp(player_light_name(PLAYER_LIGHT_SEARCHING), "searching") == 0, "its name");

    /* A network it can use, nothing playing. */
    player_status_set_network(&s, true);
    CHECK_EQ(player_status_light(&s), PLAYER_LIGHT_READY, "a network is ready");
    CHECK(strcmp(player_light_name(PLAYER_LIGHT_READY), "ready") == 0, "its name");

    /* The stream arrives but no output is taking it: still ready, not playing. */
    player_status_set_stream(&s, true);
    CHECK_EQ(player_status_light(&s), PLAYER_LIGHT_READY,
             "a stream with nothing to play to is ready");

    /* A speaker that is absent is Ready, not a fault (R14). */
    player_status_set_audio(&s, false);
    CHECK_EQ(player_status_light(&s), PLAYER_LIGHT_READY, "no output, no fault");

    /* Both: playing. */
    player_status_set_audio(&s, true);
    CHECK_EQ(player_status_light(&s), PLAYER_LIGHT_PLAYING, "the stream and an output are playing");
    CHECK(strcmp(player_light_name(PLAYER_LIGHT_PLAYING), "playing") == 0, "its name");

    /* Audio leaving an output is not enough on its own: without a stream there is nothing
     * being played. */
    player_status_set_stream(&s, false);
    CHECK_EQ(player_status_light(&s), PLAYER_LIGHT_READY, "audio with no stream is not playing");

    /* Losing the network is searching, whatever else is still true. */
    player_status_set_network(&s, false);
    CHECK_EQ(player_status_light(&s), PLAYER_LIGHT_SEARCHING, "no network, no usable state");

    /* Fault outranks everything, and clearing it returns to the derivation. */
    player_status_set_network(&s, true);
    player_status_set_stream(&s, true);
    player_status_set_audio(&s, true);
    player_status_set_fault(&s, true);
    CHECK_EQ(player_status_light(&s), PLAYER_LIGHT_FAULT, "a fault outranks the stream");
    CHECK(strcmp(player_light_name(PLAYER_LIGHT_FAULT), "fault") == 0, "its name");
    player_status_set_fault(&s, false);
    CHECK_EQ(player_status_light(&s), PLAYER_LIGHT_PLAYING, "clearing the fault returns to playing");

    /* A state that is still searching while faulted stays faulted. */
    player_status_init(&s);
    player_status_set_fault(&s, true);
    CHECK_EQ(player_status_light(&s), PLAYER_LIGHT_FAULT, "fault outranks searching too");

    /* The derivation is total: a NULL status is the same as a fresh one, and the setters are
     * safe to call with NULL, because the entry policy may not have one yet. */
    CHECK_EQ(player_status_light(NULL), PLAYER_LIGHT_SEARCHING, "no status reads as searching");
    player_status_set_network(NULL, true);
    player_status_set_stream(NULL, true);
    player_status_set_audio(NULL, true);
    player_status_set_fault(NULL, true);
    player_status_init(NULL);

    TEST_END();
}
