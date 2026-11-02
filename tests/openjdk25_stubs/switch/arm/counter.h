/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stdint.h>
#include <time.h>
static inline uint64_t armGetSystemTick(void) {
    struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000 + now.tv_nsec;
}
static inline uint64_t armGetSystemTickFreq(void) { return 1000000000; }
