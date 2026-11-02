/* SPDX-License-Identifier: GPL-2.0-only */
#include "horizon_thread_bridge.h"

#include <switch/kernel/svc.h>
#include <switch/kernel/thread.h>
#include <switch/kernel/wait.h>
#include <switch/result.h>

#include <limits.h>
#include <stdlib.h>

#define JS_HORIZON_PAGE_SIZE UINT64_C(0x1000)
#define JS_HORIZON_MIN_STACK_SIZE UINT64_C(0x1000)
/* Bound this port's requested stack sizes; libnx adds TLS/reent storage. */
#define JS_HORIZON_MAX_STACK_SIZE (UINT64_C(256) * 1024 * 1024)

typedef enum js_horizon_thread_state {
    JS_THREAD_UNSTARTED,
    JS_THREAD_RUNNING,
    JS_THREAD_JOINED
} js_horizon_thread_state;

struct js_horizon_thread {
    Thread native;
    js_horizon_thread_state state;
    js_horizon_thread_entry entry;
    void *context;
};

static void set_raw(uint32_t *raw_result, Result result) {
    if (raw_result != NULL) *raw_result = (uint32_t)result;
}

static js_horizon_thread_status result_status(Result result) {
    if (R_SUCCEEDED(result)) return JS_HORIZON_THREAD_OK;
    if (result == MAKERESULT(Module_Libnx, LibnxError_OutOfMemory))
        return JS_HORIZON_THREAD_NO_MEMORY;
    return JS_HORIZON_THREAD_SYSTEM;
}

static void thread_entry_trampoline(void *opaque) {
    js_horizon_thread *thread = (js_horizon_thread *)opaque;
    thread->entry(thread->context);
}

js_horizon_thread_status js_horizon_thread_create(
    js_horizon_thread_entry entry, void *context, uint64_t stack_size,
    js_horizon_thread **out_thread, uint32_t *raw_result) {
    set_raw(raw_result, 0);
    if (out_thread == NULL) return JS_HORIZON_THREAD_INVALID;
    *out_thread = NULL;
    if (entry == NULL || stack_size < JS_HORIZON_MIN_STACK_SIZE ||
        (stack_size & (JS_HORIZON_PAGE_SIZE - 1)) != 0 ||
        stack_size > JS_HORIZON_MAX_STACK_SIZE ||
        stack_size > (uint64_t)SIZE_MAX - JS_HORIZON_PAGE_SIZE)
        return JS_HORIZON_THREAD_INVALID;

    js_horizon_thread *thread = (js_horizon_thread *)calloc(1, sizeof(*thread));
    if (thread == NULL) return JS_HORIZON_THREAD_NO_MEMORY;
    thread->entry = entry;
    thread->context = context;

    Result result = threadCreate(&thread->native, thread_entry_trampoline,
                                 thread, NULL, (size_t)stack_size, 0x2C, -2);
    set_raw(raw_result, result);
    js_horizon_thread_status status = result_status(result);
    if (status != JS_HORIZON_THREAD_OK) {
        free(thread);
        return status;
    }

    thread->state = JS_THREAD_UNSTARTED;
    *out_thread = thread;
    return JS_HORIZON_THREAD_OK;
}

js_horizon_thread_status js_horizon_thread_start(
    js_horizon_thread *thread, uint32_t *raw_result) {
    set_raw(raw_result, 0);
    if (thread == NULL) return JS_HORIZON_THREAD_INVALID;
    if (thread->state != JS_THREAD_UNSTARTED) return JS_HORIZON_THREAD_STATE;
    Result result = threadStart(&thread->native);
    set_raw(raw_result, result);
    if (R_SUCCEEDED(result)) thread->state = JS_THREAD_RUNNING;
    return result_status(result);
}

js_horizon_thread_status js_horizon_thread_join(
    js_horizon_thread *thread, uint64_t timeout_nanos, uint32_t *raw_result) {
    set_raw(raw_result, 0);
    if (thread == NULL) return JS_HORIZON_THREAD_INVALID;
    if (thread->state != JS_THREAD_RUNNING) return JS_HORIZON_THREAD_STATE;
    if (timeout_nanos != UINT64_MAX && timeout_nanos > (uint64_t)INT64_MAX)
        return JS_HORIZON_THREAD_INVALID;
    Result result = waitSingleHandle(thread->native.handle, timeout_nanos);
    set_raw(raw_result, result);
    if (R_SUCCEEDED(result)) thread->state = JS_THREAD_JOINED;
    return result_status(result);
}

js_horizon_thread_status js_horizon_thread_close(
    js_horizon_thread *thread, uint32_t *raw_result) {
    set_raw(raw_result, 0);
    if (thread == NULL) return JS_HORIZON_THREAD_INVALID;
    if (thread->state == JS_THREAD_RUNNING) return JS_HORIZON_THREAD_STATE;
    Result result = threadClose(&thread->native);
    set_raw(raw_result, result);
    js_horizon_thread_status status = result_status(result);
    if (status == JS_HORIZON_THREAD_OK) free(thread);
    return status;
}

js_horizon_thread_status js_horizon_thread_current_stack_bounds(
    uintptr_t *low, uintptr_t *high, uint32_t *raw_result) {
    set_raw(raw_result, 0);
    if (low != NULL) *low = 0;
    if (high != NULL) *high = 0;
    if (low == NULL || high == NULL) return JS_HORIZON_THREAD_INVALID;
    Thread *thread = threadGetSelf();
    if (thread == NULL || thread->stack_mirror == NULL || thread->stack_sz == 0)
        return JS_HORIZON_THREAD_STATE;
    uintptr_t bottom = (uintptr_t)thread->stack_mirror;
    if (thread->stack_sz > UINTPTR_MAX - bottom)
        return JS_HORIZON_THREAD_STATE;
    *low = bottom;
    *high = bottom + thread->stack_sz;
    return JS_HORIZON_THREAD_OK;
}

js_horizon_thread_status js_horizon_thread_current_id(
    uint64_t *thread_id, uint32_t *raw_result) {
    set_raw(raw_result, 0);
    if (thread_id == NULL) return JS_HORIZON_THREAD_INVALID;
    *thread_id = 0;
    Result result = svcGetThreadId(thread_id, threadGetCurHandle());
    set_raw(raw_result, result);
    return result_status(result);
}
