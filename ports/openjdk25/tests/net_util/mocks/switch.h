/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef HORIZON_NET_UTIL_TEST_SWITCH_H
#define HORIZON_NET_UTIL_TEST_SWITCH_H

#include <pthread.h>
#include <stdint.h>

typedef uint32_t Result;
typedef struct { pthread_mutex_t native; } Mutex;
enum { Module_Libnx = 1, LibnxError_AlreadyInitialized = 2 };
#define R_SUCCEEDED(result) ((result) == 0)
#define R_FAILED(result) ((result) != 0)
#define MAKERESULT(module, description) (((module) << 9) | (description))

void mutexLock(Mutex* mutex);
void mutexUnlock(Mutex* mutex);
Result socketInitializeDefault(void);
void socketExit(void);

#endif
