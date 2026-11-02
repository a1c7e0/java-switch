/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef OS_HORIZON_THREAD_BRIDGE_H
#define OS_HORIZON_THREAD_BRIDGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct js_horizon_thread js_horizon_thread;

typedef enum js_horizon_thread_status {
    JS_HORIZON_THREAD_OK = 0,
    JS_HORIZON_THREAD_INVALID,
    JS_HORIZON_THREAD_STATE,
    JS_HORIZON_THREAD_NO_MEMORY,
    JS_HORIZON_THREAD_SYSTEM
} js_horizon_thread_status;

typedef void (*js_horizon_thread_entry)(void *context);

/* Lifecycle calls for one handle must be serialized by the caller. */
/* Requested stack size is page aligned (0x1000) and capped at 256 MiB.
 * The cap supports large Zero stacks; it does not promise memory availability. */
js_horizon_thread_status js_horizon_thread_create(
    js_horizon_thread_entry entry, void *context, uint64_t stack_size,
    js_horizon_thread **out_thread, uint32_t *raw_result);
js_horizon_thread_status js_horizon_thread_start(
    js_horizon_thread *thread, uint32_t *raw_result);
/* UINT64_MAX means infinite; other values above INT64_MAX are invalid. */
js_horizon_thread_status js_horizon_thread_join(
    js_horizon_thread *thread, uint64_t timeout_nanos, uint32_t *raw_result);
/* Close is valid only before start or after a successful join. libnx's
 * threadClose reports stack-unmap failures but discards svcCloseHandle's
 * Result, so that particular syscall failure cannot be reported here. */
js_horizon_thread_status js_horizon_thread_close(
    js_horizon_thread *thread, uint32_t *raw_result);

/* Start the process-lifetime native reaper before creating HotSpot-owned
 * workers. Requires libnx thread/synchronization initialization. One VM per
 * process is supported. If startup fails, the raw Horizon Result is returned
 * and any native resources that could not be closed remain owned for a retry. */
js_horizon_thread_status js_horizon_thread_ensure_reaper_started(
    uint32_t *raw_result);
/* Transfer ownership to the process-lifetime reaper without allocating. On
 * success the caller must never access thread again. Accepts an unstarted
 * handle or a started handle; the reaper closes the former directly and joins
 * then closes the latter. */
js_horizon_thread_status js_horizon_thread_retire(
    js_horizon_thread *thread, uint32_t *raw_result);
/* Snapshot cleanup status. last_result is the last retained wait/close error
 * (cleared by a later successful cleanup); pending_count includes queued or
 * retrying handles. */
void js_horizon_thread_reaper_status(
    uint32_t *last_result, uint32_t *pending_count);

#ifdef JS_HORIZON_THREAD_TESTING
/* Bounded single-item queue test hook; never starts or runs the reaper thread. */
js_horizon_thread_status js_horizon_thread_reaper_test_drain_once(
    uint64_t join_timeout_nanos, uint32_t *raw_result);
#endif

/* The range is half-open and describes the current libnx Thread stack. */
js_horizon_thread_status js_horizon_thread_current_stack_bounds(
    uintptr_t *low, uintptr_t *high, uint32_t *raw_result);
js_horizon_thread_status js_horizon_thread_current_id(
    uint64_t *thread_id, uint32_t *raw_result);

#ifdef __cplusplus
}
#endif
#endif
