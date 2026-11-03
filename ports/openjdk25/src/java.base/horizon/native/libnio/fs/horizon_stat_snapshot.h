/* SPDX-License-Identifier: GPL-2.0-only WITH Classpath-exception-2.0 */
/* Copyright (c) 2026, Java Switch contributors. All rights reserved. */
#ifndef HORIZON_STAT_SNAPSHOT_H
#define HORIZON_STAT_SNAPSHOT_H

#include <sys/stat.h>
#include <stdint.h>

struct horizon_stat_snapshot {
    int mode;
    int nlink;
    int uid;
    int gid;
    int64_t inode;
    int64_t device;
    int64_t special_device;
    int64_t size;
    int64_t atime_seconds;
    int64_t atime_nanoseconds;
    int64_t mtime_seconds;
    int64_t mtime_nanoseconds;
    int64_t ctime_seconds;
    int64_t ctime_nanoseconds;
};

static inline struct horizon_stat_snapshot
horizon_stat_snapshot_from_stat(const struct stat *st) {
    struct horizon_stat_snapshot result = {
        .mode = (int)st->st_mode,
        .nlink = (int)st->st_nlink,
        .uid = (int)st->st_uid,
        .gid = (int)st->st_gid,
        .inode = (int64_t)st->st_ino,
        .device = (int64_t)st->st_dev,
        .special_device = (int64_t)st->st_rdev,
        .size = (int64_t)st->st_size,
        .atime_seconds = (int64_t)st->st_atim.tv_sec,
        .atime_nanoseconds = (int64_t)st->st_atim.tv_nsec,
        .mtime_seconds = (int64_t)st->st_mtim.tv_sec,
        .mtime_nanoseconds = (int64_t)st->st_mtim.tv_nsec,
        .ctime_seconds = (int64_t)st->st_ctim.tv_sec,
        .ctime_nanoseconds = (int64_t)st->st_ctim.tv_nsec,
    };
    return result;
}

#endif
