/* SPDX-License-Identifier: GPL-2.0-only */
#include "horizon_bridge.h"
#include <switch/arm/counter.h>
#include <switch/kernel/semaphore.h>
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
