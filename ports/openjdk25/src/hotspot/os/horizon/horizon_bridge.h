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
/* Process-lifetime libnx TLS slot. -1 means allocation failure. Get/set require
 * a slot returned by create; the callback follows libnx's native exit order. */
int js_horizon_tls_create(void (*destructor)(void *));
void *js_horizon_tls_get(int slot);
void js_horizon_tls_set(int slot, void *value);
typedef struct js_horizon_semaphore js_horizon_semaphore;
js_horizon_semaphore *js_horizon_semaphore_create(unsigned value);
void js_horizon_semaphore_destroy(js_horizon_semaphore *);
void js_horizon_semaphore_signal(js_horizon_semaphore *, unsigned count);
void js_horizon_semaphore_wait(js_horizon_semaphore *);
int js_horizon_semaphore_trywait(js_horizon_semaphore *);
typedef struct js_horizon_monitor js_horizon_monitor;
js_horizon_monitor *js_horizon_monitor_create(void);
/* Destruction requires no owner, waiters, or concurrent accesses. */
void js_horizon_monitor_destroy(js_horizon_monitor *);
void js_horizon_monitor_lock(js_horizon_monitor *);
int js_horizon_monitor_trylock(js_horizon_monitor *);
void js_horizon_monitor_unlock(js_horizon_monitor *);
/* Call while locked. UINT64_MAX means infinite; finite timeouts are <= INT64_MAX.
 * Raw Horizon Result is preserved. On success/timeout libnx reacquires the mutex;
 * other errors must be fatal to the caller because ownership is not established. */
uint32_t js_horizon_monitor_wait(js_horizon_monitor *, uint64_t nanos);
void js_horizon_monitor_notify(js_horizon_monitor *, int all);
#ifdef __cplusplus
}
#endif
#endif
