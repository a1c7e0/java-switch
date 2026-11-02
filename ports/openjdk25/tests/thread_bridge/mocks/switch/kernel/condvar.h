/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef JS_TEST_SWITCH_KERNEL_CONDVAR_H
#define JS_TEST_SWITCH_KERNEL_CONDVAR_H
#include <stdint.h>
#include <switch/result.h>
typedef uint32_t CondVar;
void condvarWake(CondVar *condvar, int count);
Result condvarWaitTimeout(CondVar *condvar, uint32_t *mutex, uint64_t timeout);
#endif
