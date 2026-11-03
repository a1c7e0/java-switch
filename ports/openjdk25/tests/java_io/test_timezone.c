/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "TimeZone_md.h"

static void expect_id(const char *expected) {
    char *id = findJavaTZ_md(NULL);
    assert(id != NULL);
    assert(strcmp(id, expected) == 0);
    free(id);
}

int main(void) {
    unsetenv("TZ");
    assert(findJavaTZ_md(NULL) == NULL);
    assert(getGMTOffsetID() == NULL);

    assert(setenv("TZ", "UTC0", 1) == 0);
    expect_id("GMT");
    char *offset = getGMTOffsetID();
    assert(offset != NULL && strcmp(offset, "GMT") == 0);
    free(offset);

    assert(setenv("TZ", "EST5", 1) == 0);
    expect_id("GMT-05:00");

    assert(setenv("TZ", "EST5EDT,M3.2.0/2,M11.1.0/2", 1) == 0);
    char *dst_offset = findJavaTZ_md(NULL);
    assert(dst_offset != NULL);
    assert(strncmp(dst_offset, "GMT-", 4) == 0);
    assert(strlen(dst_offset) == strlen("GMT-05:00"));
    free(dst_offset);

    assert(setenv("TZ", "America/New_York", 1) == 0);
    assert(findJavaTZ_md(NULL) == NULL);
    assert(getGMTOffsetID() == NULL);
    return 0;
}
