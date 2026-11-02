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

/* The range is half-open and describes the current libnx Thread stack. */
js_horizon_thread_status js_horizon_thread_current_stack_bounds(
    uintptr_t *low, uintptr_t *high, uint32_t *raw_result);
js_horizon_thread_status js_horizon_thread_current_id(
    uint64_t *thread_id, uint32_t *raw_result);

#ifdef __cplusplus
}
#endif
#endif
