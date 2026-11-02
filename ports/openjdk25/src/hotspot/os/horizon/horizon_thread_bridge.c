/* SPDX-License-Identifier: GPL-2.0-only */
#include "horizon_thread_bridge.h"

#include <switch/kernel/svc.h>
#include <switch/kernel/mutex.h>
#include <switch/kernel/condvar.h>
#include <switch/kernel/thread.h>
#include <switch/kernel/wait.h>
#include <switch/result.h>
#include <switch/runtime/diag.h>

#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define JS_HORIZON_PAGE_SIZE UINT64_C(0x1000)
#define JS_HORIZON_MIN_STACK_SIZE UINT64_C(0x1000)
/* Bound this port's requested stack sizes; libnx adds TLS/reent storage. */
#define JS_HORIZON_MAX_STACK_SIZE (UINT64_C(256) * 1024 * 1024)

typedef enum js_horizon_thread_state {
    JS_THREAD_UNSTARTED,
    JS_THREAD_RUNNING,
    JS_THREAD_JOINED,
    JS_THREAD_REAP_QUEUED,
    JS_THREAD_REAPING
} js_horizon_thread_state;

struct js_horizon_thread {
    Thread native;
    js_horizon_thread_state state;
    js_horizon_thread_entry entry;
    void *context;
    struct js_horizon_thread *reaper_next;
    bool retire_needs_join;
};

/* libnx documents zero as a valid static initializer for Mutex and CondVar. */
static Mutex g_reaper_mutex = 0;
static CondVar g_reaper_condvar = 0;
static Thread g_reaper_native;
static js_horizon_thread *g_reaper_head;
static js_horizon_thread *g_reaper_tail;
static uint32_t g_reaper_pending;
static uint32_t g_reaper_last_result;
static enum {
    JS_REAPER_NOT_STARTED,
    JS_REAPER_READY,
    JS_REAPER_NEEDS_CLOSE
} g_reaper_state;

#define JS_REAPER_STACK_SIZE UINT64_C(0x10000)
#define JS_REAPER_JOIN_SLICE_NANOS UINT64_C(1000000000)
#define JS_REAPER_CLOSE_RETRY_NANOS UINT64_C(100000000)

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

static bool is_wait_timeout(Result result) {
    return R_MODULE(result) == Module_Kernel &&
           R_VALUE(result) == KERNELRESULT(TimedOut);
}

static void requeue_locked(js_horizon_thread *thread) {
    thread->state = JS_THREAD_REAP_QUEUED;
    thread->reaper_next = NULL;
    if (g_reaper_tail != NULL) g_reaper_tail->reaper_next = thread;
    else g_reaper_head = thread;
    g_reaper_tail = thread;
}

/* Caller holds g_reaper_mutex. */
static js_horizon_thread *dequeue_locked(void) {
    js_horizon_thread *thread = g_reaper_head;
    if (thread == NULL) return NULL;
    g_reaper_head = thread->reaper_next;
    if (g_reaper_head == NULL) g_reaper_tail = NULL;
    thread->reaper_next = NULL;
    thread->state = JS_THREAD_REAPING;
    return thread;
}

/* Remove one queue item, join if it was started, and close it. Every failure
 * keeps ownership by restoring the item to the intrusive retry queue. */
static js_horizon_thread_status reap_one(uint64_t join_timeout,
                                         uint32_t *raw_result,
                                         bool *close_failure) {
    set_raw(raw_result, 0);
    if (close_failure != NULL) *close_failure = false;
    mutexLock(&g_reaper_mutex);
    js_horizon_thread *thread = dequeue_locked();
    if (thread == NULL) {
        mutexUnlock(&g_reaper_mutex);
        return JS_HORIZON_THREAD_STATE;
    }
    /* dequeue_locked changes state; recover the lifecycle state from whether
     * the native handle was started before it entered the retirement queue. */
    bool needs_join = thread->retire_needs_join;
    mutexUnlock(&g_reaper_mutex);

    if (needs_join) {
        Result wait_result = waitSingleHandle(thread->native.handle, join_timeout);
        if (R_FAILED(wait_result)) {
            set_raw(raw_result, wait_result);
            mutexLock(&g_reaper_mutex);
            g_reaper_last_result = (uint32_t)wait_result;
            requeue_locked(thread);
            mutexUnlock(&g_reaper_mutex);
            return result_status(wait_result);
        }
        thread->retire_needs_join = false;
    }

    Result close_result = threadClose(&thread->native);
    set_raw(raw_result, close_result);
    if (R_FAILED(close_result)) {
        if (close_failure != NULL) *close_failure = true;
        mutexLock(&g_reaper_mutex);
        g_reaper_last_result = (uint32_t)close_result;
        requeue_locked(thread);
        mutexUnlock(&g_reaper_mutex);
        return result_status(close_result);
    }

    mutexLock(&g_reaper_mutex);
    if (g_reaper_pending > 0) g_reaper_pending--;
    g_reaper_last_result = 0;
    mutexUnlock(&g_reaper_mutex);
    free(thread);
    return JS_HORIZON_THREAD_OK;
}

static void reaper_entry(void *unused) {
    (void)unused;
    for (;;) {
        mutexLock(&g_reaper_mutex);
        while (g_reaper_head == NULL) {
            Result wait_result = condvarWaitTimeout(
                &g_reaper_condvar, &g_reaper_mutex, UINT64_MAX);
            if (R_FAILED(wait_result)) {
                /* libnx guarantees lock reacquisition on success/timeout only. */
                diagAbortWithResult(wait_result);
            }
        }
        mutexUnlock(&g_reaper_mutex);

        uint32_t raw = 0;
        bool close_failure = false;
        js_horizon_thread_status status = reap_one(
            JS_REAPER_JOIN_SLICE_NANOS, &raw, &close_failure);
        if (status == JS_HORIZON_THREAD_OK ||
            (raw != 0 && is_wait_timeout((Result)raw)))
            continue;
        if (status != JS_HORIZON_THREAD_STATE) {
            if (!close_failure) diagAbortWithResult((Result)raw);
            /* The item remains queued; expose the raw error and back off before
             * retrying a failed unmap rather than spinning or losing ownership. */
            mutexLock(&g_reaper_mutex);
            g_reaper_last_result = raw;
            Result delay_result = condvarWaitTimeout(
                &g_reaper_condvar, &g_reaper_mutex,
                JS_REAPER_CLOSE_RETRY_NANOS);
            if (R_FAILED(delay_result) && !is_wait_timeout(delay_result))
                diagAbortWithResult(delay_result);
            mutexUnlock(&g_reaper_mutex);
        }
    }
}

js_horizon_thread_status js_horizon_thread_ensure_reaper_started(
    uint32_t *raw_result) {
    set_raw(raw_result, 0);
    mutexLock(&g_reaper_mutex);
    if (g_reaper_state == JS_REAPER_READY) {
        mutexUnlock(&g_reaper_mutex);
        return JS_HORIZON_THREAD_OK;
    }

    if (g_reaper_state == JS_REAPER_NEEDS_CLOSE) {
        Result close_result = threadClose(&g_reaper_native);
        if (R_FAILED(close_result)) {
            g_reaper_last_result = (uint32_t)close_result;
            set_raw(raw_result, close_result);
            mutexUnlock(&g_reaper_mutex);
            return result_status(close_result);
        }
        memset(&g_reaper_native, 0, sizeof(g_reaper_native));
        g_reaper_state = JS_REAPER_NOT_STARTED;
        g_reaper_last_result = 0;
    }

    Result create_result = threadCreate(&g_reaper_native, reaper_entry, NULL,
                                        NULL, (size_t)JS_REAPER_STACK_SIZE,
                                        0x2C, -2);
    if (R_FAILED(create_result)) {
        memset(&g_reaper_native, 0, sizeof(g_reaper_native));
        set_raw(raw_result, create_result);
        mutexUnlock(&g_reaper_mutex);
        return result_status(create_result);
    }

    Result start_result = threadStart(&g_reaper_native);
    if (R_FAILED(start_result)) {
        Result close_result = threadClose(&g_reaper_native);
        if (R_FAILED(close_result)) {
            g_reaper_state = JS_REAPER_NEEDS_CLOSE;
            g_reaper_last_result = (uint32_t)close_result;
        } else {
            memset(&g_reaper_native, 0, sizeof(g_reaper_native));
        }
        set_raw(raw_result, start_result);
        mutexUnlock(&g_reaper_mutex);
        return result_status(start_result);
    }

    g_reaper_state = JS_REAPER_READY;
    g_reaper_last_result = 0;
    mutexUnlock(&g_reaper_mutex);
    return JS_HORIZON_THREAD_OK;
}

js_horizon_thread_status js_horizon_thread_retire(
    js_horizon_thread *thread, uint32_t *raw_result) {
    set_raw(raw_result, 0);
    if (thread == NULL) return JS_HORIZON_THREAD_INVALID;
    mutexLock(&g_reaper_mutex);
    if (g_reaper_state != JS_REAPER_READY) {
        mutexUnlock(&g_reaper_mutex);
        return JS_HORIZON_THREAD_STATE;
    }
    if (thread->state != JS_THREAD_UNSTARTED &&
        thread->state != JS_THREAD_RUNNING &&
        thread->state != JS_THREAD_JOINED) {
        mutexUnlock(&g_reaper_mutex);
        return JS_HORIZON_THREAD_STATE;
    }
    thread->retire_needs_join = thread->state == JS_THREAD_RUNNING;
    requeue_locked(thread);
    g_reaper_pending++;
    condvarWake(&g_reaper_condvar, 1);
    mutexUnlock(&g_reaper_mutex);
    return JS_HORIZON_THREAD_OK;
}

void js_horizon_thread_reaper_status(
    uint32_t *last_result, uint32_t *pending_count) {
    mutexLock(&g_reaper_mutex);
    if (last_result != NULL) *last_result = g_reaper_last_result;
    if (pending_count != NULL) *pending_count = g_reaper_pending;
    mutexUnlock(&g_reaper_mutex);
}

#ifdef JS_HORIZON_THREAD_TESTING
js_horizon_thread_status js_horizon_thread_reaper_test_drain_once(
    uint64_t join_timeout_nanos, uint32_t *raw_result) {
    return reap_one(join_timeout_nanos, raw_result, NULL);
}
#endif

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
    mutexLock(&g_reaper_mutex);
    if (thread->state != JS_THREAD_UNSTARTED) {
        mutexUnlock(&g_reaper_mutex);
        return JS_HORIZON_THREAD_STATE;
    }
    /* Publish before svcStartThread: the entry callback can retire immediately. */
    thread->state = JS_THREAD_RUNNING;
    mutexUnlock(&g_reaper_mutex);
    Result result = threadStart(&thread->native);
    set_raw(raw_result, result);
    if (R_FAILED(result)) {
        mutexLock(&g_reaper_mutex);
        /* A failed start does not execute the entrypoint, so ownership remains
         * with this caller and state can safely return to UNSTARTED. */
        thread->state = JS_THREAD_UNSTARTED;
        mutexUnlock(&g_reaper_mutex);
    }
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
    if (thread->state != JS_THREAD_UNSTARTED &&
        thread->state != JS_THREAD_JOINED)
        return JS_HORIZON_THREAD_STATE;
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
