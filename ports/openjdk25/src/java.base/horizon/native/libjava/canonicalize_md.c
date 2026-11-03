/*
 * Copyright (c) 1994, 2021, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

/* Horizon device paths have a root such as "sdmc:/". Keep that root while
 * applying lexical rules for "." and "..". realpath is used for existing
 * paths when available; missing tails are normalized lexically. The SD card
 * and ROM storage APIs do not expose POSIX symbolic links.
 */
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "jdk_util.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static size_t device_root_length(const char *path) {
    if (path[0] == '/') return 1;
    for (size_t i = 0; path[i] != '\0' && path[i] != '/'; ++i) {
        if (path[i] == ':' && path[i + 1] == '/') return i + 2;
        if (path[i] == ':' && path[i + 1] == '\0') return i + 1;
    }
    return 0;
}

static int same_device_root(const char *original, const char *resolved) {
    size_t root = device_root_length(original);
    if (root <= 1 || original[0] == '/') return 1;
    size_t device_length = root - (original[root - 1] == '/' ? 2 : 1);
    if (strlen(resolved) <= device_length) return 0;
    return strncmp(original, resolved, device_length) == 0 &&
           resolved[device_length] == ':';
}

static int lexical_absolute(const char *input, char *out, int length) {
    char absolute[PATH_MAX + 1];
    size_t input_length = strlen(input);
    size_t root_length;

    if (device_root_length(input) != 0) {
        if (input_length >= sizeof(absolute)) {
            errno = ENAMETOOLONG;
            return -1;
        }
        memcpy(absolute, input, input_length + 1);
    } else {
        char cwd[PATH_MAX + 1];
        if (getcwd(cwd, sizeof(cwd)) == NULL) return -1;
        size_t cwd_length = strlen(cwd);
        size_t separator = cwd_length != 0 && cwd[cwd_length - 1] != '/';
        if (cwd_length + separator + input_length >= sizeof(absolute)) {
            errno = ENAMETOOLONG;
            return -1;
        }
        memcpy(absolute, cwd, cwd_length);
        if (separator) absolute[cwd_length++] = '/';
        memcpy(absolute + cwd_length, input, input_length + 1);
    }

    root_length = device_root_length(absolute);
    if (root_length == 0) {
        errno = EINVAL;
        return -1;
    }
    if (absolute[root_length - 1] != '/') {
        if (root_length + 1 >= sizeof(absolute)) {
            errno = ENAMETOOLONG;
            return -1;
        }
        memmove(absolute + root_length + 1, absolute + root_length,
                strlen(absolute + root_length) + 1);
        absolute[root_length++] = '/';
    }

    size_t absolute_length = strlen(absolute);
    size_t max_parts = absolute_length / 2 + 1;
    size_t *restore = malloc(max_parts * sizeof(*restore));
    if (restore == NULL) {
        errno = ENOMEM;
        return -1;
    }

    size_t out_length = root_length;
    if (out_length >= (size_t)length) {
        free(restore);
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(out, absolute, root_length);

    size_t part_count = 0;
    const char *cursor = absolute + root_length;
    while (*cursor != '\0') {
        while (*cursor == '/') ++cursor;
        const char *part = cursor;
        while (*cursor != '\0' && *cursor != '/') ++cursor;
        size_t part_length = (size_t)(cursor - part);
        if (part_length == 0 || (part_length == 1 && part[0] == '.')) continue;
        if (part_length == 2 && part[0] == '.' && part[1] == '.') {
            if (part_count > 0) out_length = restore[--part_count];
            continue;
        }
        if (part_count >= max_parts) {
            free(restore);
            errno = ENAMETOOLONG;
            return -1;
        }
        restore[part_count++] = out_length;
        size_t separator = out_length > root_length ? 1 : 0;
        if (out_length + separator + part_length >= (size_t)length) {
            free(restore);
            errno = ENAMETOOLONG;
            return -1;
        }
        if (separator) out[out_length++] = '/';
        memcpy(out + out_length, part, part_length);
        out_length += part_length;
    }
    out[out_length] = '\0';
    free(restore);
    return 0;
}

JNIEXPORT int
JDK_Canonicalize(const char *original, char *out, int length)
{
    char resolved[PATH_MAX + 1];
    size_t original_length;

    if (original == NULL || out == NULL || length <= 0) {
        errno = EINVAL;
        return -1;
    }
    original_length = strlen(original);
    if (original_length > PATH_MAX) {
        errno = ENAMETOOLONG;
        return -1;
    }

    if (realpath(original, resolved) != NULL) {
        if (same_device_root(original, resolved)) {
            return lexical_absolute(resolved, out, length);
        }
        /* A filesystem implementation that drops its device prefix cannot
         * provide a usable canonical path for that device. Preserve the
         * caller's prefix and apply the supported lexical normalization.
         */
        return lexical_absolute(original, out, length);
    }

    if (errno != ENOENT && errno != ENOTDIR && errno != EACCES &&
        errno != EINVAL) {
        return -1;
    }
    return lexical_absolute(original, out, length);
}
