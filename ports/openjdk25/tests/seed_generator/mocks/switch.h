/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef TEST_SWITCH_H
#define TEST_SWITCH_H

#include <stddef.h>
#include <stdint.h>

typedef uint32_t Result;
#define R_FAILED(value) ((value) != 0)
#define R_SUCCEEDED(value) ((value) == 0)

Result csrngInitialize(void);
void csrngExit(void);
Result csrngGetRandomBytes(void *out, size_t out_size);

#endif
