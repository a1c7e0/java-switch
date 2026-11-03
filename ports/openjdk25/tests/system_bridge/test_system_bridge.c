/* SPDX-License-Identifier: GPL-2.0-only */
#include "horizon_system_bridge.h"
#include <switch/kernel/svc.h>
#include <switch/kernel/thread.h>
#include <switch/runtime/env.h>
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static uint32_t fail_info_id = UINT32_MAX;
static Result info_failure = -9;
static uint32_t fail_process_id;
static Result process_failure = -13;
static Handle current_handle = 4;
static Handle main_handle = 4;
static s64 last_sleep;
static unsigned sleep_calls;

Result svcGetInfo(u64 *out, uint32_t id, Handle handle, u64 sub_id) {
    assert(handle == CUR_PROCESS_HANDLE && sub_id == 0);
    if (id == fail_info_id) return info_failure;
    if (id == InfoType_CoreMask) *out = UINT64_C(0xD);
    else if (id == InfoType_TotalMemorySize) *out = UINT64_C(0x60000000);
    else if (id == InfoType_UsedMemorySize) *out = UINT64_C(0x12340000);
    else if (id == InfoType_AslrRegionAddress) *out = UINT64_C(0x8000000);
    else assert(0);
    return 0;
}

Result svcGetProcessId(u64 *out, Handle handle) {
    assert(handle == CUR_PROCESS_HANDLE);
    if (fail_process_id) return process_failure;
    *out = UINT64_C(0x12345);
    return 0;
}

void svcSleepThread(s64 nanoseconds) { last_sleep = nanoseconds; sleep_calls++; }
Handle threadGetCurHandle(void) { return current_handle; }
Handle envGetMainThreadHandle(void) { return main_handle; }

void svcExitProcess(void) { _Exit(83); } /* Host-only kernel termination sentinel. */

int main(int argc, char **argv) {
    if (argc > 1) js_horizon_system_exit_process(37, strcmp(argv[1], "quick") != 0);
    uint64_t mask = 99, total = 99, used = 99, pid = 99;
    uint32_t raw = 99, primordial = 99;
    assert(js_horizon_system_core_mask(&mask, &raw) == JS_HORIZON_SYSTEM_OK);
    assert(mask == 0xD && raw == 0);

    assert(js_horizon_system_memory(&total, &used, &raw) == JS_HORIZON_SYSTEM_OK);
    assert(total == UINT64_C(0x60000000) && used == UINT64_C(0x12340000));

    fail_info_id = InfoType_UsedMemorySize;
    total = 17; used = 23; raw = 0;
    assert(js_horizon_system_memory(&total, &used, &raw) == JS_HORIZON_SYSTEM_SYSTEM);
    assert(total == 17 && used == 23 && raw == (uint32_t)info_failure);
    fail_info_id = UINT32_MAX;
    raw = 88;
    assert(js_horizon_system_core_mask(NULL, &raw) == JS_HORIZON_SYSTEM_INVALID);
    assert(raw == 0);

    assert(js_horizon_system_process_id(&pid, &raw) == JS_HORIZON_SYSTEM_OK);
    assert(pid == 0x12345 && raw == 0);
    fail_process_id = 1; pid = 31;
    assert(js_horizon_system_process_id(&pid, &raw) == JS_HORIZON_SYSTEM_SYSTEM);
    assert(pid == 31 && raw == (uint32_t)process_failure);
    fail_process_id = 0;

    uint64_t aslr_base = 0;
    assert(js_horizon_system_aslr_base(&aslr_base, &raw) == JS_HORIZON_SYSTEM_OK);
    assert(aslr_base == UINT64_C(0x8000000) && raw == 0);
    fail_info_id = InfoType_AslrRegionAddress;
    aslr_base = 41;
    assert(js_horizon_system_aslr_base(&aslr_base, &raw) == JS_HORIZON_SYSTEM_SYSTEM);
    assert(aslr_base == 41 && raw == (uint32_t)info_failure);
    fail_info_id = UINT32_MAX;

    assert(js_horizon_system_is_primordial(&primordial, &raw) == JS_HORIZON_SYSTEM_OK);
    assert(primordial == 1);
    main_handle = 8;
    assert(js_horizon_system_is_primordial(&primordial, &raw) == JS_HORIZON_SYSTEM_OK);
    assert(primordial == 0);
    current_handle = INVALID_HANDLE;
    primordial = 7;
    assert(js_horizon_system_is_primordial(&primordial, &raw) == JS_HORIZON_SYSTEM_SYSTEM);
    assert(primordial == 7 && raw == 0);

    js_horizon_system_sleep(0);
    assert(last_sleep == 0 && sleep_calls == 1);
    js_horizon_system_sleep(42);
    assert(last_sleep == 42);
    js_horizon_system_sleep(UINT64_MAX);
    assert(last_sleep == INT64_MAX);
    assert(sleep_calls == 3);
    return 0;
}
