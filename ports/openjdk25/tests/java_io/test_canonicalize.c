/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int JDK_Canonicalize(const char *original, char *out, int length);

static void expect(const char *input, const char *expected) {
    char actual[PATH_MAX];
    assert(JDK_Canonicalize(input, actual, sizeof(actual)) == 0);
    if (strcmp(actual, expected) != 0) {
        fprintf(stderr, "canonicalize(%s) = %s, expected %s\n",
                input, actual, expected);
        abort();
    }
}

int main(void) {
    char cwd[PATH_MAX];
    assert(getcwd(cwd, sizeof(cwd)) != NULL);
    expect(".", cwd);

    expect("sdmc:/", "sdmc:/");
    expect("sdmc:/folder/../file.txt", "sdmc:/file.txt");
    expect("sdmc:/../../file.txt", "sdmc:/file.txt");
    expect("sdmc:", "sdmc:/");
    char expected[PATH_MAX];
    int length = snprintf(expected, sizeof(expected), "%s/file.txt", cwd);
    assert(length > 0 && length < (int)sizeof(expected));
    expect("relative/../file.txt", expected);

    char small[2];
    errno = 0;
    assert(JDK_Canonicalize("sdmc:/longer", small, sizeof(small)) == -1);
    assert(errno == ENAMETOOLONG);
    return 0;
}
