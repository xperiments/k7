/*
 * T50: the deck's name, host tier.
 *
 * The rule is one function and it is asked by both edges - the page's route and whatever
 * stores the word - so it is checked here as a table, not as prose: what a person may type,
 * what the address becomes, and what a buffer too small says. The address is always derived
 * from the word, never stored, which is why the last group checks the two together.
 */
#include "test_util.h"

#include <stdio.h>
#include <string.h>

#include "device_name.h"

static void accepts(const char *word)
{
    CHECK(device_name_word_ok(word), "%s is a legal word", word ? word : "(null)");
}

static void refuses(const char *word, const char *why)
{
    CHECK(!device_name_word_ok(word), "%s is refused: %s", word ? word : "(null)", why);
}

int main(void)
{
    /* The empty string is not an error: it is "no name", the product's own. */
    accepts("");
    accepts("kitchen");
    accepts("studio-2");
    accepts("a");
    accepts("192");
    accepts("a-b-c");

    refuses(NULL, "there is no word");
    refuses("Kitchen", "an uppercase letter");
    refuses("KITCHEN", "all uppercase");
    refuses("-kitchen", "a leading hyphen");
    refuses("kitchen-", "a trailing hyphen");
    refuses("kitchen--two", "a double hyphen");
    refuses("kitchen_2", "an underscore");
    refuses("kitch en", "a space");
    refuses("küche", "a letter outside a-z");
    refuses("kitchen!", "punctuation");
    refuses("kkkkkkkkkkkkkkkkkkkkkkkkk", "25 characters");

    /* The label is the product's word plus the person's, and nothing when there is none. */
    char out[64];
    CHECK(device_name_label("", out, sizeof out), "an unnamed deck has a label");
    CHECK(strcmp(out, "k7") == 0, "which is the product's: %s", out);
    CHECK(device_name_label("kitchen", out, sizeof out), "a named deck has a label");
    CHECK(strcmp(out, "k7-kitchen") == 0, "which is k7- and the word: %s", out);
    CHECK(!device_name_label("Kitchen", out, sizeof out), "a refused word makes no label");

    char address[64];
    CHECK(device_name_address("", address, sizeof address), "an unnamed deck has an address");
    CHECK(strcmp(address, "k7.local") == 0, "which is k7.local: %s", address);
    CHECK(device_name_address("kitchen", address, sizeof address), "a named deck has one");
    CHECK(strcmp(address, "k7-kitchen.local") == 0, "which keeps the .local: %s", address);

    /* A buffer that cannot hold the answer says so rather than truncating it. */
    char tiny[8];
    CHECK(!device_name_label("kitchen", tiny, sizeof tiny), "a short buffer refuses the label");
    CHECK(!device_name_address("kitchen", tiny, sizeof tiny), "and the address");

    TEST_END();
}
