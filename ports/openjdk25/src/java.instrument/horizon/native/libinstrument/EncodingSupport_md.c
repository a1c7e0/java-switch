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

#include <stddef.h>

/* Horizon filesystem paths use UTF-8 and the public SDK has no locale or
 * iconv service. The upstream callers supply both manifest UTF-8 and JNI
 * modified UTF-8. Preserve standard UTF-8 and translate valid modified UTF-8
 * surrogate pairs to standard four-byte UTF-8. */
static int continuation(unsigned char value) {
    return (value & 0xc0) == 0x80;
}

int convertUtf8ToPlatformString(char *utf8, int length, char *output, int capacity) {
    if (utf8 == NULL || output == NULL || length < 0 || capacity <= 0) return -1;
    const unsigned char *input = (const unsigned char *)utf8;
    int read = 0;
    int written = 0;
    while (read < length) {
        unsigned char first = input[read++];
        if (first == 0) return -1; /* Filesystem paths cannot contain NUL. */
        if (first < 0x80) {
            if (written + 1 >= capacity) return -1;
            output[written++] = (char)first;
            continue;
        }

        if (first >= 0xc2 && first <= 0xdf) {
            if (read >= length || !continuation(input[read])) return -1;
            if (written + 2 >= capacity) return -1;
            output[written++] = (char)first;
            output[written++] = (char)input[read++];
            continue;
        }

        if (first >= 0xe0 && first <= 0xef) {
            if (read + 1 >= length || !continuation(input[read]) ||
                !continuation(input[read + 1]) ||
                (first == 0xe0 && input[read] < 0xa0)) return -1;
            unsigned char second = input[read++];
            unsigned char third = input[read++];
            unsigned int codepoint = ((unsigned int)(first & 0x0f) << 12) |
                                     ((unsigned int)(second & 0x3f) << 6) |
                                     (unsigned int)(third & 0x3f);
            if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
                if (read + 2 >= length || input[read] != 0xed ||
                    input[read + 1] < 0xb0 || input[read + 1] > 0xbf ||
                    !continuation(input[read + 2])) return -1;
                unsigned int low = 0xdc00 +
                                   ((unsigned int)(input[read + 1] & 0x0f) << 6) +
                                   (unsigned int)(input[read + 2] & 0x3f);
                if (low < 0xdc00 || low > 0xdfff) return -1;
                read += 3;
                unsigned int scalar = 0x10000 + ((codepoint - 0xd800) << 10) +
                                      (low - 0xdc00);
                if (written + 4 >= capacity) return -1;
                output[written++] = (char)(0xf0 | (scalar >> 18));
                output[written++] = (char)(0x80 | ((scalar >> 12) & 0x3f));
                output[written++] = (char)(0x80 | ((scalar >> 6) & 0x3f));
                output[written++] = (char)(0x80 | (scalar & 0x3f));
                continue;
            }
            if (codepoint >= 0xdc00 && codepoint <= 0xdfff) return -1;
            if (written + 3 >= capacity) return -1;
            output[written++] = (char)first;
            output[written++] = (char)second;
            output[written++] = (char)third;
            continue;
        }

        if (first >= 0xf0 && first <= 0xf4) {
            if (read + 2 >= length || !continuation(input[read]) ||
                !continuation(input[read + 1]) || !continuation(input[read + 2]) ||
                (first == 0xf0 && input[read] < 0x90) ||
                (first == 0xf4 && input[read] > 0x8f)) return -1;
            if (written + 4 >= capacity) return -1;
            output[written++] = (char)first;
            output[written++] = (char)input[read++];
            output[written++] = (char)input[read++];
            output[written++] = (char)input[read++];
            continue;
        }
        return -1;
    }
    output[written] = '\0';
    return written;
}
