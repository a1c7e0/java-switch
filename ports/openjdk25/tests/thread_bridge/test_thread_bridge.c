/* SPDX-License-Identifier: GPL-2.0-only */
#include "horizon_thread_bridge.h"
#include <switch/kernel/thread.h>
#include <switch/result.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Result create_result;
static Result start_result;
static Result join_result;
static Result close_result;
static Result id_result;
static uint32_t wait_handle;
static uint64_t wait_timeout;
static Thread current_thread;
static unsigned close_calls;
static unsigned wait_calls;
static int fail_next_allocation;

void *mock_calloc(size_t count, size_t size) {
    if (fail_next_allocation) {
        fail_next_allocation = 0;
        return NULL;
    }
    void *memory = malloc(count * size);
    if (memory != NULL) memset(memory, 0, count * size);
    return memory;
}

Result threadCreate(Thread *thread, ThreadFunc entry, void *arg, void *stack,
                    size_t stack_size, int priority, int core) {
    (void)entry; (void)arg; (void)stack;
    assert(stack_size == 0x8000);
    assert(priority == 0x2c && core == -2);
    if (create_result == 0) {
        thread->handle = 17;
        thread->stack_mirror = (void *)(uintptr_t)0x100000;
        thread->stack_sz = 0x7fc0;
    }
    return create_result;
}
Result threadStart(Thread *thread) { assert(thread->handle == 17); return start_result; }
Result waitSingleHandle(uint32_t handle, uint64_t timeout) {
    wait_calls++;
    wait_handle = handle; wait_timeout = timeout; return join_result;
}
Result threadClose(Thread *thread) {
    assert(thread->handle == 17);
    close_calls++;
    return close_result;
}
Thread *threadGetSelf(void) { return &current_thread; }
uint32_t threadGetCurHandle(void) { return 44; }
Result svcGetThreadId(uint64_t *id, uint32_t handle) {
    assert(handle == 44);
    if (id_result == 0) *id = UINT64_C(0x123456789abcdef0);
    return id_result;
}

static void entry(void *context) { (void)context; }

int main(void) {
    js_horizon_thread *thread = NULL;
    uint32_t raw = UINT32_MAX;
    assert(js_horizon_thread_create(entry, NULL, 0, &thread, &raw) == JS_HORIZON_THREAD_INVALID);
    assert(thread == NULL && raw == 0);
    assert(js_horizon_thread_create(entry, NULL, 0x8001, &thread, &raw) == JS_HORIZON_THREAD_INVALID);
    assert(thread == NULL);

    create_result = UINT32_C(0xabc001);
    assert(js_horizon_thread_create(entry, NULL, 0x8000, &thread, &raw) == JS_HORIZON_THREAD_SYSTEM);
    assert(thread == NULL && raw == create_result);
    create_result = MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
    assert(js_horizon_thread_create(entry, NULL, 0x8000, &thread, &raw) == JS_HORIZON_THREAD_NO_MEMORY);
    assert(thread == NULL && raw == create_result);
    create_result = 0;
    fail_next_allocation = 1;
    assert(js_horizon_thread_create(entry, NULL, 0x8000, &thread, &raw) == JS_HORIZON_THREAD_NO_MEMORY);
    assert(thread == NULL && raw == 0);
    assert(js_horizon_thread_create(entry, NULL, UINT64_C(256) * 1024 * 1024 + 0x1000,
                                    &thread, &raw) == JS_HORIZON_THREAD_INVALID);
    assert(thread == NULL);
    assert(js_horizon_thread_create(entry, NULL, 0x8000, &thread, &raw) == JS_HORIZON_THREAD_OK);
    assert(js_horizon_thread_close(thread, &raw) == JS_HORIZON_THREAD_OK);
    close_calls = 0;

    assert(js_horizon_thread_create(entry, NULL, 0x8000, &thread, &raw) == JS_HORIZON_THREAD_OK);
    assert(thread != NULL && raw == 0);

    assert(js_horizon_thread_join(thread, 1, &raw) == JS_HORIZON_THREAD_STATE);
    start_result = UINT32_C(0xabc002);
    assert(js_horizon_thread_start(thread, &raw) == JS_HORIZON_THREAD_SYSTEM);
    assert(raw == start_result);
    start_result = 0;
    assert(js_horizon_thread_start(thread, &raw) == JS_HORIZON_THREAD_OK);
    assert(js_horizon_thread_start(thread, &raw) == JS_HORIZON_THREAD_STATE);
    assert(js_horizon_thread_close(thread, &raw) == JS_HORIZON_THREAD_STATE);

    unsigned waits_before_invalid_timeout = wait_calls;
    assert(js_horizon_thread_join(thread, (uint64_t)INT64_MAX + 1, &raw) ==
           JS_HORIZON_THREAD_INVALID);
    assert(raw == 0 && wait_calls == waits_before_invalid_timeout);

    join_result = UINT32_C(0xabc003);
    assert(js_horizon_thread_join(thread, 123456, &raw) == JS_HORIZON_THREAD_SYSTEM);
    assert(raw == join_result && wait_handle == 17 && wait_timeout == 123456);
    assert(js_horizon_thread_close(thread, &raw) == JS_HORIZON_THREAD_STATE);
    join_result = 0;
    assert(js_horizon_thread_join(thread, UINT64_MAX, &raw) == JS_HORIZON_THREAD_OK);
    assert(wait_timeout == UINT64_MAX);
    close_result = UINT32_C(0xabc004);
    assert(js_horizon_thread_close(thread, &raw) == JS_HORIZON_THREAD_SYSTEM);
    assert(raw == close_result && close_calls == 1);
    close_result = 0;
    assert(js_horizon_thread_close(thread, &raw) == JS_HORIZON_THREAD_OK);
    assert(close_calls == 2);

    current_thread.stack_mirror = (void *)(uintptr_t)0x200000;
    current_thread.stack_sz = 0x4000;
    uintptr_t low = 0, high = 0;
    assert(js_horizon_thread_current_stack_bounds(&low, &high, &raw) == JS_HORIZON_THREAD_OK);
    assert(low == 0x200000 && high == 0x204000);
    current_thread.stack_sz = 0;
    low = 99; high = 101;
    assert(js_horizon_thread_current_stack_bounds(&low, &high, &raw) == JS_HORIZON_THREAD_STATE);
    assert(low == 0 && high == 0);
    current_thread.stack_sz = 0x4000;
    uint64_t id = 0;
    assert(js_horizon_thread_current_id(&id, &raw) == JS_HORIZON_THREAD_OK);
    assert(id == UINT64_C(0x123456789abcdef0));
    id_result = UINT32_C(0xabc005);
    assert(js_horizon_thread_current_id(&id, &raw) == JS_HORIZON_THREAD_SYSTEM);
    assert(raw == id_result && id == 0);
    puts("thread bridge host mocks passed");
    return 0;
}
