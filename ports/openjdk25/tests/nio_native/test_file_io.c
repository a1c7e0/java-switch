/* SPDX-License-Identifier: GPL-2.0-only WITH Classpath-exception-2.0 */
/* Copyright (c) 2026, Java Switch contributors. All rights reserved. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "horizon_file_io.h"
#include "horizon_stat_snapshot.h"

int main(void) {
    char path[] = "/tmp/horizon-nio-file-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    assert(write(fd, "abcdef", 6) == 6);
    assert(lseek(fd, 2, SEEK_SET) == 2);

    char positional[4] = {0};
    ssize_t bytes = -1;
    assert(horizon_positional_io(fd, positional, 3, 0, 0, &bytes) == 0);
    assert(bytes == 3 && memcmp(positional, "abc", 3) == 0);
    assert(lseek(fd, 0, SEEK_CUR) == 2);

    assert(horizon_positional_io(fd, "XY", 2, 4, 1, &bytes) == 0);
    assert(bytes == 2 && lseek(fd, 0, SEEK_CUR) == 2);

    struct stat st;
    assert(fstat(fd, &st) == 0);
    struct horizon_stat_snapshot snapshot = horizon_stat_snapshot_from_stat(&st);
    assert(S_ISREG(snapshot.mode));
    assert(snapshot.size == 6);
    assert(snapshot.inode == (int64_t)st.st_ino);
    assert(snapshot.device == (int64_t)st.st_dev);
    assert(snapshot.mtime_seconds == (int64_t)st.st_mtim.tv_sec);
    assert(snapshot.mtime_nanoseconds == (int64_t)st.st_mtim.tv_nsec);
    assert(snapshot.atime_nanoseconds >= 0 && snapshot.atime_nanoseconds < 1000000000);

    assert(lseek(fd, 0, SEEK_SET) == 0);
    char first[3] = {0};
    char second[5] = {0};
    struct horizon_iovec read_vec[] = {
        { .base = first, .length = 2 },
        { .base = second, .length = 4 },
    };
    assert(horizon_vector_io(fd, read_vec, 2, 0, &bytes) == 0);
    assert(bytes == 6);
    assert(memcmp(first, "ab", 2) == 0);
    assert(memcmp(second, "cdXY", 4) == 0);
    assert(lseek(fd, 0, SEEK_CUR) == 6);

    assert(lseek(fd, 0, SEEK_SET) == 0);
    const char left[] = "12";
    const char right[] = "345";
    struct horizon_iovec write_vec[] = {
        { .base = (void *)left, .length = 2 },
        { .base = (void *)right, .length = 3 },
    };
    assert(horizon_vector_io(fd, write_vec, 2, 1, &bytes) == 0);
    assert(bytes == 5 && lseek(fd, 0, SEEK_CUR) == 5);
    assert(lseek(fd, 0, SEEK_SET) == 0);
    char content[6] = {0};
    assert(read(fd, content, 6) == 6);
    assert(memcmp(content, "12345Y", 6) == 0);

    struct horizon_iovec oversized = {
        .base = content,
        .length = (size_t)INT_MAX + 1,
    };
    errno = 0;
    assert(horizon_vector_io(fd, &oversized, 1, 0, &bytes) == -1);
    assert(errno == EOVERFLOW);
    struct horizon_iovec null_vector = { .base = NULL, .length = 1 };
    errno = 0;
    assert(horizon_vector_io(fd, &null_vector, 1, 0, &bytes) == -1);
    assert(errno == EFAULT);
    errno = 0;
    assert(horizon_vector_io(fd, NULL, -1, 0, &bytes) == -1);
    assert(errno == EINVAL);
    assert(close(fd) == 0);
    assert(unlink(path) == 0);
    puts("Horizon native NIO host test: PASS");
    return 0;
}
