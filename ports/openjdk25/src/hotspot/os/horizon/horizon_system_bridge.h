/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef OS_HORIZON_SYSTEM_BRIDGE_H
#define OS_HORIZON_SYSTEM_BRIDGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum js_horizon_system_status {
    JS_HORIZON_SYSTEM_OK = 0,
    JS_HORIZON_SYSTEM_INVALID,
    JS_HORIZON_SYSTEM_SYSTEM
} js_horizon_system_status;

/* Raw Horizon Results are preserved on syscall failure. */
js_horizon_system_status js_horizon_system_core_mask(
    uint64_t *mask, uint32_t *raw_result);
js_horizon_system_status js_horizon_system_memory(
    uint64_t *total, uint64_t *used, uint32_t *raw_result);
js_horizon_system_status js_horizon_system_process_id(
    uint64_t *process_id, uint32_t *raw_result);
js_horizon_system_status js_horizon_system_aslr_base(
    uint64_t *base, uint32_t *raw_result);
js_horizon_system_status js_horizon_system_is_primordial(
    uint32_t *is_primordial, uint32_t *raw_result);

/* svcSleepThread is a kernel wait/yield operation. Zero yields; positive
 * values sleep for that many nanoseconds. */
void js_horizon_system_sleep(uint64_t nanoseconds);

#ifdef __cplusplus
}
#endif
#endif
