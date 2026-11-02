/* SPDX-License-Identifier: GPL-2.0-only */
#include "horizon_thread_bridge.h"
#include <switch/kernel/thread.h>
#include <switch/kernel/mutex.h>
#include <switch/kernel/condvar.h>
#include <switch/result.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static Result create_result;
static Result start_result;
static Result wait_result;
static Result close_result;
static Handle next_handle = 100;
static Handle last_created_handle;
static Handle last_wait_handle;
static unsigned create_calls;
static unsigned start_calls;
static unsigned wait_calls;
static unsigned close_calls;
static ThreadFunc entry_by_handle[512];
static void *arg_by_handle[512];
static Handle invoke_entry_on_start;
static int drain_during_start;
static void check_status(js_horizon_thread_status actual,
                         js_horizon_thread_status expected);

void mutexLock(Mutex *mutex) { (void)mutex; }
void mutexUnlock(Mutex *mutex) { (void)mutex; }
void condvarWake(CondVar *condvar, int count) { (void)condvar; (void)count; }
Result condvarWaitTimeout(CondVar *condvar, Mutex *mutex, uint64_t timeout) {
    (void)condvar; (void)mutex; (void)timeout; return 0;
}
void diagAbortWithResult(Result result) { fprintf(stderr, "unexpected abort: 0x%x\n", result); abort(); }

Result threadCreate(Thread *thread, ThreadFunc entry, void *arg, void *stack,
                    size_t stack_size, int priority, int core) {
    (void)entry; (void)arg; (void)stack;
    create_calls++;
    assert(stack_size >= 0x1000 && (stack_size & 0xfff) == 0);
    assert(priority == 0x2c && core == -2);
    if (create_result == 0) {
        thread->handle = next_handle++;
        last_created_handle = thread->handle;
        assert(thread->handle < 512);
        entry_by_handle[thread->handle] = entry;
        arg_by_handle[thread->handle] = arg;
        thread->stack_mirror = (void *)(uintptr_t)0x100000;
        thread->stack_sz = stack_size;
    }
    return create_result;
}
Result threadStart(Thread *thread) {
    assert(thread->handle != 0);
    start_calls++;
    if (thread->handle == invoke_entry_on_start && start_result == 0) {
        invoke_entry_on_start = 0;
        entry_by_handle[thread->handle](arg_by_handle[thread->handle]);
        if (drain_during_start) {
            uint32_t raw = UINT32_MAX;
            unsigned waits_before = wait_calls, closes_before = close_calls;
            wait_result = 0;
            check_status(js_horizon_thread_reaper_test_drain_once(0, &raw),
                         JS_HORIZON_THREAD_OK);
            assert(raw == 0 && wait_calls == waits_before + 1 &&
                   close_calls == closes_before + 1);
        }
    }
    return start_result;
}
Result waitSingleHandle(Handle handle, uint64_t timeout) {
    (void)timeout;
    wait_calls++;
    last_wait_handle = handle;
    return wait_result;
}
Result threadClose(Thread *thread) {
    assert(thread->handle != 0);
    close_calls++;
    return close_result;
}
Thread *threadGetSelf(void) { return NULL; }
Handle threadGetCurHandle(void) { return 0; }
Result svcGetThreadId(uint64_t *id, Handle handle) { (void)id; (void)handle; return 1; }

static void entry(void *context) { (void)context; }
typedef struct RetireOnEntry {
    js_horizon_thread *thread;
    js_horizon_thread_status status;
    uint32_t raw;
} RetireOnEntry;
static void retire_from_entry(void *context) {
    RetireOnEntry *retire = (RetireOnEntry *)context;
    retire->status = js_horizon_thread_retire(retire->thread, &retire->raw);
}

static void check_status(js_horizon_thread_status actual,
                         js_horizon_thread_status expected) {
    if (actual != expected) {
        fprintf(stderr, "status %d, expected %d\n", actual, expected);
        abort();
    }
}

int main(void) {
    uint32_t raw = UINT32_MAX, last_error = UINT32_MAX, pending = UINT32_MAX;
    js_horizon_thread *thread = NULL;

    /* A handle may predate reaper setup, but failed transfer leaves ownership
     * with this caller so it can close the still-unstarted thread itself. */
    check_status(js_horizon_thread_create(entry, NULL, 0x8000, &thread, &raw),
                 JS_HORIZON_THREAD_OK);
    check_status(js_horizon_thread_retire(thread, &raw), JS_HORIZON_THREAD_STATE);
    check_status(js_horizon_thread_close(thread, &raw), JS_HORIZON_THREAD_OK);

    create_result = UINT32_C(0xabc101);
    check_status(js_horizon_thread_ensure_reaper_started(&raw), JS_HORIZON_THREAD_SYSTEM);
    assert(raw == create_result);
    create_result = 0;
    start_result = UINT32_C(0xabc102);
    close_result = UINT32_C(0xabc103);
    check_status(js_horizon_thread_ensure_reaper_started(&raw), JS_HORIZON_THREAD_SYSTEM);
    assert(raw == start_result); /* preserve primary startup failure */
    close_result = 0;
    start_result = 0;
    check_status(js_horizon_thread_ensure_reaper_started(&raw), JS_HORIZON_THREAD_OK);
    assert(start_calls == 2 && close_calls == 3); /* failed start retained, then closed */
    check_status(js_horizon_thread_ensure_reaper_started(&raw), JS_HORIZON_THREAD_OK);
    assert(start_calls == 2); /* idempotent, process-lifetime worker */

    /* Retire an unstarted object: it must close without trying to join. */
    check_status(js_horizon_thread_create(entry, NULL, 0x8000, &thread, &raw),
                 JS_HORIZON_THREAD_OK);
    check_status(js_horizon_thread_retire(thread, &raw), JS_HORIZON_THREAD_OK);
    check_status(js_horizon_thread_retire(thread, &raw), JS_HORIZON_THREAD_STATE);
    unsigned waits = wait_calls;
    check_status(js_horizon_thread_reaper_test_drain_once(0, &raw), JS_HORIZON_THREAD_OK);
    assert(raw == 0 && wait_calls == waits);
    js_horizon_thread_reaper_status(&last_error, &pending);
    assert(last_error == 0 && pending == 0);

    check_status(js_horizon_thread_create(entry, NULL, 0x8000, &thread, &raw),
                 JS_HORIZON_THREAD_OK);
    check_status(js_horizon_thread_retire(thread, &raw), JS_HORIZON_THREAD_OK);
    close_result = UINT32_C(0xabc105);
    waits = wait_calls;
    check_status(js_horizon_thread_reaper_test_drain_once(0, &raw),
                 JS_HORIZON_THREAD_SYSTEM);
    assert(raw == close_result && wait_calls == waits);
    js_horizon_thread_reaper_status(&last_error, &pending);
    assert(last_error == close_result && pending == 1);
    close_result = 0;
    check_status(js_horizon_thread_reaper_test_drain_once(0, &raw),
                 JS_HORIZON_THREAD_OK);
    assert(raw == 0 && wait_calls == waits);
    js_horizon_thread_reaper_status(&last_error, &pending);
    assert(last_error == 0 && pending == 0);

    /* Retirement can happen while the callback is still running. Timeout keeps
     * ownership queued; successful join followed by close failure keeps it for
     * a close-only retry, without waiting a second time. */
    check_status(js_horizon_thread_create(entry, NULL, 0x8000, &thread, &raw),
                 JS_HORIZON_THREAD_OK);
    check_status(js_horizon_thread_start(thread, &raw), JS_HORIZON_THREAD_OK);
    Handle worker_handle = last_created_handle;
    check_status(js_horizon_thread_retire(thread, &raw), JS_HORIZON_THREAD_OK);
    check_status(js_horizon_thread_retire(thread, &raw), JS_HORIZON_THREAD_STATE);
    wait_result = KERNELRESULT(TimedOut);
    check_status(js_horizon_thread_reaper_test_drain_once(0, &raw), JS_HORIZON_THREAD_SYSTEM);
    assert(raw == wait_result && last_wait_handle == worker_handle);
    js_horizon_thread_reaper_status(&last_error, &pending);
    assert(last_error == wait_result && pending == 1);

    wait_result = 0;
    close_result = UINT32_C(0xabc104);
    check_status(js_horizon_thread_reaper_test_drain_once(0, &raw), JS_HORIZON_THREAD_SYSTEM);
    assert(raw == close_result && wait_calls == waits + 2);
    js_horizon_thread_reaper_status(&last_error, &pending);
    assert(last_error == close_result && pending == 1);

    close_result = 0;
    waits = wait_calls;
    check_status(js_horizon_thread_reaper_test_drain_once(0, &raw), JS_HORIZON_THREAD_OK);
    assert(raw == 0 && wait_calls == waits); /* join was already completed */
    js_horizon_thread_reaper_status(&last_error, &pending);
    assert(last_error == 0 && pending == 0);

    /* The entrypoint may retire and be fully reaped before svcStartThread
     * returns. start() must publish RUNNING first and must not touch the handle
     * after successful start because the reaper can already have freed it. */
    RetireOnEntry retire = { NULL, JS_HORIZON_THREAD_INVALID, UINT32_MAX };
    check_status(js_horizon_thread_create(retire_from_entry, &retire, 0x8000,
                                          &thread, &raw),
                 JS_HORIZON_THREAD_OK);
    retire.thread = thread;
    invoke_entry_on_start = last_created_handle;
    drain_during_start = 1;
    check_status(js_horizon_thread_start(thread, &raw), JS_HORIZON_THREAD_OK);
    drain_during_start = 0;
    assert(retire.status == JS_HORIZON_THREAD_OK && retire.raw == 0);
    js_horizon_thread_reaper_status(&last_error, &pending);
    assert(last_error == 0 && pending == 0);

    puts("thread reaper host mocks passed: startup ownership, unstarted retire, timeout and close retry");
    return 0;
}
