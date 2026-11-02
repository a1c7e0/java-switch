/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef JS_TEST_SWITCH_KERNEL_THREAD_H
#define JS_TEST_SWITCH_KERNEL_THREAD_H
#include <stddef.h>
#include <stdint.h>
#include <switch/result.h>
typedef uint32_t Handle;
typedef struct Thread {
    Handle handle;
    void *stack_mirror;
    size_t stack_sz;
} Thread;
typedef void (*ThreadFunc)(void *);
Result threadCreate(Thread *, ThreadFunc, void *, void *, size_t, int, int);
Result threadStart(Thread *);
Result threadClose(Thread *);
Thread *threadGetSelf(void);
Handle threadGetCurHandle(void);
#endif
