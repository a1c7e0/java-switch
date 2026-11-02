/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef OS_HORIZON_BRIDGE_H
#define OS_HORIZON_BRIDGE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Keep libnx u8/s8/Handle/Mutex out of HotSpot's global C++ namespace. */
uint64_t js_horizon_clock_ticks(void);
uint64_t js_horizon_clock_frequency(void);
typedef struct js_horizon_semaphore js_horizon_semaphore;
js_horizon_semaphore *js_horizon_semaphore_create(unsigned value);
void js_horizon_semaphore_destroy(js_horizon_semaphore *);
void js_horizon_semaphore_signal(js_horizon_semaphore *, unsigned count);
void js_horizon_semaphore_wait(js_horizon_semaphore *);
int js_horizon_semaphore_trywait(js_horizon_semaphore *);
#ifdef __cplusplus
}
#endif
#endif
