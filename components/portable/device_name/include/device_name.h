#pragma once
/*
 * device_name - the word a person gives the deck, and the address it makes.
 *
 * A deck with no name is the product's own: `k7`, answering `k7.local`. A person may give
 * it a word - *kitchen*, *studio* - and the address becomes the product's word plus theirs,
 * `k7-kitchen.local`, so two decks on one LAN are two names rather than two hosts fighting
 * over one (HA-04). The bare product name keeps answering as an alias; that is the
 * compatibility promise, and it is why the name is additive.
 *
 * The word is a lowercase DNS label: `a-z`, `0-9` and single hyphens, not starting or
 * ending with one, at most {@link DEVICE_NAME_MAX} characters. It is stored as the person
 * typed it; the address is always derived, never stored, so a change to the derivation is
 * not a migration.
 */
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The product's word: a deck without a name is this, and it stays an alias when there is one. */
#define DEVICE_NAME_PRODUCT "k7"

/* Short enough to type from a phone and to say out loud; the resulting label is well under
 * the DNS limit of 63. */
#define DEVICE_NAME_MAX 24

/* The longest address this can make: "k7-" + the word + ".local". */
#define DEVICE_NAME_ADDRESS_MAX (sizeof(DEVICE_NAME_PRODUCT) - 1 + 1 + DEVICE_NAME_MAX + 6 + 1)

/* Is this a legal word? The empty string is legal and means "no name": the setting is
 * optional, and an unnamed deck is the product's own name. */
bool device_name_word_ok(const char *word);

/* The mDNS label for a word: "k7" when there is none, "k7-<word>" when there is. False for
 * a word {@link device_name_word_ok} refuses, or a buffer too small to hold the label. */
bool device_name_label(const char *word, char *out, size_t out_size);

/* The address for a word: "<label>.local" - what a person types and what the MCP guard
 * accepts. */
bool device_name_address(const char *word, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
