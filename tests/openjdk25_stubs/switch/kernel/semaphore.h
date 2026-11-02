/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include "condvar.h"
typedef struct { Mutex mutex; CondVar condition; uint64_t count; } Semaphore;
void semaphoreInit(Semaphore *, uint64_t);
void semaphoreSignal(Semaphore *);
void semaphoreWait(Semaphore *);
bool semaphoreTryWait(Semaphore *);
