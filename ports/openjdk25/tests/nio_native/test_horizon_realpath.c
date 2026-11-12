/* SPDX-License-Identifier: GPL-2.0-only WITH Classpath-exception-2.0 */
/* Copyright (c) 2026, Java Switch contributors. All rights reserved. */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "horizon_realpath.h"

static int realpath_mode;
static int stat_mode;

char *__wrap_realpath(const char *path, char *resolved) {
    (void)path;
    if (realpath_mode == 0) {
        errno = EINVAL;
        return NULL;
    }
    strcpy(resolved, realpath_mode == 1
        ? "/switch/mc-runtime/tests/app.jar"
        : "sdmc:/switch/mc-runtime/tests/app.jar");
    return resolved;
}

int __wrap_stat(const char *path, struct stat *attributes) {
    (void)path;
    (void)attributes;
    if (stat_mode == 0) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

int main(void) {
    char resolved[256];

    /* Newlib realpath fails for an existing devoptab URI. The fallback must
     * retain the mount and normalize dot segments after stat confirms it. */
    realpath_mode = 0;
    stat_mode = 1;
    assert(horizon_realpath("sdmc:/switch/mc-runtime/tests/./cache/../app.jar",
                            resolved, sizeof(resolved)) == 0);
    assert(strcmp(resolved, "sdmc:/switch/mc-runtime/tests/app.jar") == 0);

    /* A stripped device root is also invalid even when realpath succeeds. */
    realpath_mode = 1;
    assert(horizon_realpath("sdmc:/switch/mc-runtime/tests/app.jar",
                            resolved, sizeof(resolved)) == 0);
    assert(strcmp(resolved, "sdmc:/switch/mc-runtime/tests/app.jar") == 0);

    /* A valid devoptab-qualified result is kept verbatim. */
    realpath_mode = 2;
    assert(horizon_realpath("sdmc:/switch/mc-runtime/tests/app.jar",
                            resolved, sizeof(resolved)) == 0);
    assert(strcmp(resolved, "sdmc:/switch/mc-runtime/tests/app.jar") == 0);

    /* Missing classpath entries remain errors and are discarded by
     * URLClassPath rather than being canonicalized into apparent success. */
    realpath_mode = 0;
    stat_mode = 0;
    assert(horizon_realpath("sdmc:/missing.jar", resolved,
                            sizeof(resolved)) == -1);
    assert(errno == ENOENT);

    puts("Horizon NIO realpath device-root fallback: HOST-PASSED; Switch NOT TESTED");
    return 0;
}
