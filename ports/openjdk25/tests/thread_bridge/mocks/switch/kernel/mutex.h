/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef JS_TEST_SWITCH_KERNEL_MUTEX_H
#define JS_TEST_SWITCH_KERNEL_MUTEX_H
#include <stdint.h>
typedef uint32_t Mutex;
void mutexLock(Mutex *mutex);
void mutexUnlock(Mutex *mutex);
#endif
