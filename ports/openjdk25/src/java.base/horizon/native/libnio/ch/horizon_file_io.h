/* SPDX-License-Identifier: GPL-2.0-only WITH Classpath-exception-2.0 */
/* Copyright (c) 2026, Java Switch contributors. All rights reserved. */
#ifndef HORIZON_FILE_IO_H
#define HORIZON_FILE_IO_H

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

struct horizon_iovec {
    void *base;
    size_t length;
};

/* Emulate pread/pwrite without changing the shared open-file-description cursor. */
static inline int horizon_positional_io(int fd, void *buffer, size_t length,
                                        off_t offset, int writing,
                                        ssize_t *result) {
    if (offset < 0) {
        errno = EINVAL;
        return -1;
    }
    off_t original = lseek(fd, (off_t)0, SEEK_CUR);
    if (original == (off_t)-1) return -1;
    if (lseek(fd, offset, SEEK_SET) == (off_t)-1) return -1;

    ssize_t count;
    do {
        count = writing ? write(fd, buffer, length) : read(fd, buffer, length);
    } while (count < 0 && errno == EINTR);
    int operation_error = errno;

    if (lseek(fd, original, SEEK_SET) == (off_t)-1) return -1;
    if (count < 0) errno = operation_error;
    *result = count;
    return 0;
}

/* One libc read/write preserves the descriptor cursor's atomic advancement. */
static inline int horizon_vector_io(int fd, const struct horizon_iovec *iov,
                                    int count, int writing, ssize_t *result) {
    if (count < 0 || count > 1024 || (count > 0 && iov == NULL)) {
        errno = EINVAL;
        return -1;
    }
    size_t total = 0;
    for (int i = 0; i < count; i++) {
        if (iov[i].length != 0 && iov[i].base == NULL) {
            errno = EFAULT;
            return -1;
        }
        if (iov[i].length > (size_t)INT_MAX - total) {
            errno = EOVERFLOW;
            return -1;
        }
        total += iov[i].length;
    }
    if (total == 0) {
        *result = 0;
        return 0;
    }

    unsigned char *buffer = malloc(total);
    if (buffer == NULL) {
        errno = ENOMEM;
        return -1;
    }
    if (writing) {
        size_t offset = 0;
        for (int i = 0; i < count; i++) {
            memcpy(buffer + offset, iov[i].base, iov[i].length);
            offset += iov[i].length;
        }
    }

    ssize_t bytes;
    do {
        bytes = writing ? write(fd, buffer, total) : read(fd, buffer, total);
    } while (bytes < 0 && errno == EINTR);

    if (bytes >= 0 && !writing) {
        size_t remaining = (size_t)bytes;
        size_t offset = 0;
        for (int i = 0; i < count && remaining != 0; i++) {
            size_t copied = iov[i].length < remaining ? iov[i].length : remaining;
            memcpy(iov[i].base, buffer + offset, copied);
            offset += copied;
            remaining -= copied;
        }
    }
    free(buffer);
    *result = bytes;
    return bytes < 0 ? -1 : 0;
}

#endif
