#pragma once
/*
 * card_use - a card file's lifetime, so a card is never unmounted under an open file.
 *
 * FATFS unmounts by forgetting the volume. A directory lookup that runs after that - the
 * decoder opening the next Track, the artwork loader asking for a cover - walks freed
 * structure and panics. The card watcher cannot know who holds a file, so it asks: `quiesce`
 * closes the gate and waits for every open card file to close, and `resume` opens it again
 * after a mount.
 *
 * This was four functions on `app_services`, beside boot ordering and route handlers, where
 * the only exercise it got was an acceptance test that could not assert on it. A gate with a
 * deadline is worth testing on its own.
 *
 * The rule the caller keeps: a `quiesce` that times out must leave the volume mounted, and a
 * `begin` that returns false means "not now" - the caller must not touch the card.
 */
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CARD_USE_OK = 0,
    CARD_USE_NO_MEM,
} card_use_result_t;

/* Create the gate, open, with nobody inside. Called once, before anything can ask. */
card_use_result_t card_use_init(void);

/* Enter. False when the gate is closed - the card is being unmounted - so nothing may be
 * opened. Before card_use_init() this is always true: there is nothing to close yet. */
bool card_use_begin(void);

/* Leave. The last one out wakes a waiting `quiesce`. */
void card_use_end(void);

/* Close the gate and wait for the open files to close, at most `wait_ms`. False on timeout,
 * in which case the volume must be left mounted and the caller retries later. */
bool card_use_quiesce(uint32_t wait_ms);

/* Open the gate again, after a mount. */
void card_use_resume(void);

/* How many files are open, for a log line or a test. */
int card_use_users(void);

#ifdef __cplusplus
}
#endif
