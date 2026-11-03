/*
 * Copyright (c) 2004, 2026, Oracle and/or its affiliates. All rights reserved.
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
 */

#include <string.h>
#include "FileSystemSupport_md.h"

#define slash '/'

/* libnx fsdev names are at most 31 bytes and are followed by ":/" in paths. */
static size_t deviceRootLength(const char *path) {
    if (path == NULL || path[0] == '\0' || path[0] == slash) return 0;
    for (size_t i = 0; path[i] != '\0' && path[i] != slash; i++) {
        if (path[i] == ':') {
            return i > 0 && i <= 31 && path[i + 1] == slash ? i + 2 : 0;
        }
    }
    return 0;
}

static char *copy_string(const char *source, size_t length) {
    char *copy = (char *)malloc(length + 1);
    if (copy == NULL) return NULL;
    memcpy(copy, source, length);
    copy[length] = '\0';
    return copy;
}

char *basePath(const char *path) {
    const char *last = strrchr(path, slash);
    if (last == NULL) return (char *)path;
    size_t prefix = (size_t)(last - path);
    size_t device_root = deviceRootLength(path);
    if (device_root > 0 && prefix == device_root - 1)
        return copy_string(path, device_root);
    if (path[0] == slash && prefix == 0) return copy_string("/", 1);
    return copy_string(path, prefix);
}

int isAbsolute(const char *path) {
    return path != NULL && (path[0] == slash || deviceRootLength(path) > 0);
}

char *normalize_path(const char *path) {
    if (path == NULL) return NULL;
    size_t length = strlen(path);
    size_t root = deviceRootLength(path);
    if (root == 0 && length > 0 && path[0] == slash) root = 1;
    size_t end = length;
    while (end > root && path[end - 1] == slash) end--;

    int needs_normalization = end != length;
    for (size_t i = root; i < end; i++) {
        if (path[i] == slash && i > 0 && path[i - 1] == slash) {
            needs_normalization = 1;
            break;
        }
    }
    if (!needs_normalization) return (char *)path;

    char *result = (char *)malloc(length + 1);
    if (result == NULL) return NULL;
    size_t used = root;
    if (root > 0) memcpy(result, path, root);
    for (size_t i = root; i < end; i++) {
        if (path[i] == slash && used > 0 && result[used - 1] == slash) continue;
        result[used++] = path[i];
    }
    result[used] = '\0';
    return result;
}

char *resolve(const char *parent, const char *child) {
    if (parent == NULL || child == NULL) return NULL;
    if (isAbsolute(child)) return copy_string(child, strlen(child));
    size_t parent_length = strlen(parent);
    size_t child_length = strlen(child);
    if (parent_length == 0) return copy_string(child, child_length);
    if (child_length == 0) return copy_string(parent, parent_length);
    size_t separator = parent[parent_length - 1] == slash ? 0 : 1;
    char *result = (char *)malloc(parent_length + separator + child_length + 1);
    if (result == NULL) return NULL;
    memcpy(result, parent, parent_length);
    if (separator != 0) result[parent_length] = slash;
    memcpy(result + parent_length + separator, child, child_length);
    result[parent_length + separator + child_length] = '\0';
    return result;
}

char *fromURIPath(const char *path) {
    if (path == NULL) return NULL;
    size_t length = strlen(path);
    size_t start = 0;
    if (length > 2 && path[0] == slash && deviceRootLength(path + 1) > 0)
        start = 1;
    const char *converted = path + start;
    size_t converted_length = length - start;
    size_t root = deviceRootLength(converted);
    if (root == 0 && converted_length > 0 && converted[0] == slash) root = 1;
    while (converted_length > root && converted[converted_length - 1] == slash)
        converted_length--;
    if (start == 0 && converted_length == length) return (char *)path;
    return copy_string(converted, converted_length);
}
