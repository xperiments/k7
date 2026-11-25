/*
 * T49: the deck's name, host tier.
 *
 * The device's name was spelled out four times - the mDNS host and instance, the endpoint
 * its TXT record advertises, the name the MCP origin guard accepts, and the hostname the
 * share record hands a K7Player - so a rename was four edits that could disagree. This
 * file reads what each of them says about one name, and then moves that name at the seam
 * the host adapter offers (`K7_HOSTNAME`) and reads them all again. The guard is read over
 * HTTP in origin_tests.py and the record in t42_share, because those are the tiers they
 * live in; what is here is the seam itself and the two strings derived from it.
 */
#include "test_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_board.h"

static const char *hostname(void)
{
    return app_board_hostname();
}

static bool local_name(char *out, size_t cap) { return app_board_local_name(out, cap); }

static bool endpoint(char *out, size_t cap, uint16_t port, const char *path)
{
    return app_board_endpoint(out, cap, port, path);
}

int main(void)
{
    /* The product's word, before anyone names the deck. */
    CHECK(strcmp(hostname(), "k7") == 0, "the default name is the product's");

    char local[64];
    CHECK(local_name(local, sizeof local), "the name has a .local form");
    CHECK(strcmp(local, "k7.local") == 0, "and it is k7.local");

    char endpoint80[96];
    CHECK(endpoint(endpoint80, sizeof endpoint80, 80, "/mcp"), "the name has an endpoint");
    CHECK(strcmp(endpoint80, "http://k7.local:80/mcp") == 0,
          "and it names the same host: %s", endpoint80);

    /* A buffer that cannot hold the name says so rather than truncating it. */
    char tiny[5];
    CHECK(!local_name(tiny, sizeof tiny), "a short buffer refuses the .local form");

    /* The seam: one value moved, both derivations follow, with nothing else touched. */
    setenv("K7_HOSTNAME", "kitchen", 1);
    CHECK(strcmp(hostname(), "kitchen") == 0, "the moved name is the seam's");
    CHECK(local_name(local, sizeof local), "the moved name has a .local form");
    CHECK(strcmp(local, "kitchen.local") == 0, "and it moved with it: %s", local);
    CHECK(endpoint(endpoint80, sizeof endpoint80, 8080, "/mcp"), "the moved endpoint builds");
    CHECK(strcmp(endpoint80, "http://kitchen.local:8080/mcp") == 0,
          "and it names the moved host and port: %s", endpoint80);

    unsetenv("K7_HOSTNAME");
    CHECK(strcmp(hostname(), "k7") == 0, "and the default comes back");

    TEST_END();
}
