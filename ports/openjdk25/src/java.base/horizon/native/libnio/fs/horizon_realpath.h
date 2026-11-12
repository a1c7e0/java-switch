/* SPDX-License-Identifier: GPL-2.0-only WITH Classpath-exception-2.0 */
/* Copyright (c) 2026, Java Switch contributors. All rights reserved. */
#ifndef HORIZON_REALPATH_H
#define HORIZON_REALPATH_H

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <string.h>
#include <sys/stat.h>
#include <stdlib.h>

/* Newlib's realpath may return a host-style path without the devoptab root,
 * or fail while canonicalizing a valid device path. Horizon storage exposes
 * no POSIX symlink API, so after stat confirms that the original device path
 * exists, lexical canonicalization preserves the sdmc volume and resolves
 * dot segments. Missing paths remain errors, so classpath URL creation still
 * drops a path that does not exist. */
static inline int horizon_realpath(const char *path, char *resolved,
                                   size_t capacity) {
    static const char root[] = "sdmc:/";
    const size_t root_length = sizeof(root) - 1;
    if (path == NULL || resolved == NULL || capacity == 0) {
        errno = EINVAL;
        return -1;
    }
    char native[PATH_MAX + 1];
    char *native_result = realpath(path, native);
    int device_path = strncmp(path, root, root_length) == 0;
    if (!device_path || (native_result != NULL &&
                        strncmp(native_result, root, root_length) == 0)) {
        if (native_result == NULL) return -1;
        size_t length = strlen(native_result);
        if (length >= capacity) { errno = ENAMETOOLONG; return -1; }
        memcpy(resolved, native_result, length + 1);
        return 0;
    }

    struct stat attributes;
    if (stat(path, &attributes) != 0) return -1;

    size_t input_length = strlen(path);
    if (input_length >= capacity || root_length >= capacity) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(resolved, root, root_length);
    size_t output_length = root_length;
    const char *cursor = path + root_length;
    while (*cursor != '\0') {
        while (*cursor == '/') ++cursor;
        if (*cursor == '\0') break;
        const char *component = cursor;
        while (*cursor != '\0' && *cursor != '/') ++cursor;
        size_t component_length = (size_t)(cursor - component);
        if (component_length == 1 && component[0] == '.') continue;
        if (component_length == 2 && component[0] == '.' &&
            component[1] == '.') {
            if (output_length > root_length) {
                while (output_length > root_length &&
                       resolved[output_length - 1] != '/') --output_length;
                if (output_length > root_length) --output_length;
            }
            continue;
        }
        if (output_length > root_length) {
            if (output_length + 1 >= capacity) {
                errno = ENAMETOOLONG;
                return -1;
            }
            resolved[output_length++] = '/';
        }
        if (component_length >= capacity - output_length) {
            errno = ENAMETOOLONG;
            return -1;
        }
        memcpy(resolved + output_length, component, component_length);
        output_length += component_length;
    }
    resolved[output_length] = '\0';
    return 0;
}

#endif
