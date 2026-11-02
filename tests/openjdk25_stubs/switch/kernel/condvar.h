/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>
typedef pthread_mutex_t Mutex;
typedef pthread_cond_t CondVar;
void mutexInit(Mutex *);
void mutexLock(Mutex *);
void mutexUnlock(Mutex *);
bool mutexTryLock(Mutex *);
void condvarInit(CondVar *);
uint32_t condvarWaitTimeout(CondVar *, Mutex *, uint64_t);
uint32_t condvarWake(CondVar *, int);
