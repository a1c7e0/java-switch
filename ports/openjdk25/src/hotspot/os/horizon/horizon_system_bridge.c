/* SPDX-License-Identifier: GPL-2.0-only */
#include "horizon_system_bridge.h"

#include <switch/kernel/svc.h>
#include <switch/kernel/thread.h>
#include <switch/runtime/env.h>

#include <stddef.h>

static void set_raw(uint32_t *raw_result, Result result) {
    if (raw_result != NULL) *raw_result = (uint32_t)result;
}

static js_horizon_system_status query_info(uint64_t *value, uint32_t id,
                                           uint32_t *raw_result) {
    if (value == NULL) {
        set_raw(raw_result, 0);
        return JS_HORIZON_SYSTEM_INVALID;
    }
    u64 result_value = 0;
    Result result = svcGetInfo(&result_value, id, CUR_PROCESS_HANDLE, 0);
    set_raw(raw_result, result);
    if (R_FAILED(result)) return JS_HORIZON_SYSTEM_SYSTEM;
    *value = (uint64_t)result_value;
    return JS_HORIZON_SYSTEM_OK;
}

js_horizon_system_status js_horizon_system_core_mask(
    uint64_t *mask, uint32_t *raw_result) {
    return query_info(mask, InfoType_CoreMask, raw_result);
}

js_horizon_system_status js_horizon_system_memory(
    uint64_t *total, uint64_t *used, uint32_t *raw_result) {
    if (total == NULL || used == NULL) {
        set_raw(raw_result, 0);
        return JS_HORIZON_SYSTEM_INVALID;
    }
    uint64_t total_value = 0;
    uint64_t used_value = 0;
    js_horizon_system_status status = query_info(
        &total_value, InfoType_TotalMemorySize, raw_result);
    if (status != JS_HORIZON_SYSTEM_OK) return status;
    status = query_info(&used_value, InfoType_UsedMemorySize, raw_result);
    if (status != JS_HORIZON_SYSTEM_OK) return status;
    *total = total_value;
    *used = used_value;
    return JS_HORIZON_SYSTEM_OK;
}

js_horizon_system_status js_horizon_system_process_id(
    uint64_t *process_id, uint32_t *raw_result) {
    if (process_id == NULL) {
        set_raw(raw_result, 0);
        return JS_HORIZON_SYSTEM_INVALID;
    }
    u64 value = 0;
    Result result = svcGetProcessId(&value, CUR_PROCESS_HANDLE);
    set_raw(raw_result, result);
    if (R_FAILED(result)) return JS_HORIZON_SYSTEM_SYSTEM;
    *process_id = (uint64_t)value;
    return JS_HORIZON_SYSTEM_OK;
}

js_horizon_system_status js_horizon_system_aslr_base(
    uint64_t *base, uint32_t *raw_result) {
    return query_info(base, InfoType_AslrRegionAddress, raw_result);
}

js_horizon_system_status js_horizon_system_is_primordial(
    uint32_t *is_primordial, uint32_t *raw_result) {
    if (is_primordial == NULL) {
        set_raw(raw_result, 0);
        return JS_HORIZON_SYSTEM_INVALID;
    }
    const Handle current = threadGetCurHandle();
    const Handle main = envGetMainThreadHandle();
    if (current == INVALID_HANDLE || main == INVALID_HANDLE) {
        set_raw(raw_result, 0);
        return JS_HORIZON_SYSTEM_SYSTEM;
    }
    *is_primordial = current == main ? 1u : 0u;
    set_raw(raw_result, 0);
    return JS_HORIZON_SYSTEM_OK;
}

void js_horizon_system_sleep(uint64_t nanoseconds) {
    /* svcSleepThread takes a signed duration. Clamp oversized unsigned input
     * so the C bridge never converts it to a negative sleep. */
    const int64_t maximum = INT64_MAX;
    svcSleepThread(nanoseconds > (uint64_t)maximum
                       ? maximum : (int64_t)nanoseconds);
}
