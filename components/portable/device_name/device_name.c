/*
 * device_name - see include/device_name.h.
 *
 * Two functions and a rule. The rule is a DNS label, checked here and nowhere else: the
 * setup page refuses with the same answer the route gives, because both ask this.
 */
#include "device_name.h"

#include <stdio.h>
#include <string.h>

/* A word: lowercase letters, digits, single hyphens, not leading or trailing. The empty
 * string is not a word and not an error: it is "no name". */
bool device_name_word_ok(const char *word)
{
    if (!word) return false;
    size_t n = strlen(word);
    if (n == 0) return true;
    if (n > DEVICE_NAME_MAX) return false;
    if (word[0] == '-' || word[n - 1] == '-') return false;
    for (size_t i = 0; i < n; ++i) {
        char c = word[i];
        if (c == '-') {
            if (i + 1 < n && word[i + 1] == '-') return false;
            continue;
        }
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) return false;
    }
    return true;
}

bool device_name_label(const char *word, char *out, size_t out_size)
{
    if (!word || !out || !out_size) return false;
    if (!device_name_word_ok(word)) return false;
    int n = *word ? snprintf(out, out_size, "%s-%s", DEVICE_NAME_PRODUCT, word)
                  : snprintf(out, out_size, "%s", DEVICE_NAME_PRODUCT);
    return n > 0 && (size_t)n < out_size;
}

bool device_name_address(const char *word, char *out, size_t out_size)
{
    char label[DEVICE_NAME_MAX + sizeof(DEVICE_NAME_PRODUCT) + 2];
    if (!device_name_label(word, label, sizeof label)) return false;
    if (!out || !out_size) return false;
    int n = snprintf(out, out_size, "%s.local", label);
    return n > 0 && (size_t)n < out_size;
}
