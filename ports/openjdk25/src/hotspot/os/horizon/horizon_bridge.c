/* SPDX-License-Identifier: GPL-2.0-only */
#include "horizon_bridge.h"
#include <switch/arm/counter.h>
#include <switch/kernel/semaphore.h>
#include <switch/kernel/condvar.h>
#include <stdlib.h>
uint64_t js_horizon_clock_ticks(void) { return armGetSystemTick(); }
uint64_t js_horizon_clock_frequency(void) { return armGetSystemTickFreq(); }
struct js_horizon_semaphore { Semaphore native; };
js_horizon_semaphore *js_horizon_semaphore_create(unsigned value) {
    js_horizon_semaphore *s = malloc(sizeof(*s));
    if (s) semaphoreInit(&s->native, value);
    return s;
}
void js_horizon_semaphore_destroy(js_horizon_semaphore *s) { free(s); }
void js_horizon_semaphore_signal(js_horizon_semaphore *s, unsigned count) {
    for (unsigned i = 0; i < count; i++) semaphoreSignal(&s->native);
}
void js_horizon_semaphore_wait(js_horizon_semaphore *s) { semaphoreWait(&s->native); }
int js_horizon_semaphore_trywait(js_horizon_semaphore *s) { return semaphoreTryWait(&s->native); }
struct js_horizon_monitor { Mutex mutex; CondVar condition; };
js_horizon_monitor *js_horizon_monitor_create(void) {
    js_horizon_monitor *m = malloc(sizeof(*m));
    if (m) { mutexInit(&m->mutex); condvarInit(&m->condition); }
    return m;
}
void js_horizon_monitor_destroy(js_horizon_monitor *m) { free(m); }
void js_horizon_monitor_lock(js_horizon_monitor *m) { mutexLock(&m->mutex); }
int js_horizon_monitor_trylock(js_horizon_monitor *m) { return mutexTryLock(&m->mutex); }
void js_horizon_monitor_unlock(js_horizon_monitor *m) { mutexUnlock(&m->mutex); }
uint32_t js_horizon_monitor_wait(js_horizon_monitor *m, uint64_t nanos) {
    return condvarWaitTimeout(&m->condition, &m->mutex, nanos);
}
void js_horizon_monitor_notify(js_horizon_monitor *m, int all) {
    /* svcSignalProcessWideKey has no Result return, unlike the wait syscall. */
    condvarWake(&m->condition, all ? -1 : 1);
}
