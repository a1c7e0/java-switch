/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef HORIZON_MEMORY_BRIDGE_H
#define HORIZON_MEMORY_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  JS_HORIZON_MEMORY_OK = 0,
  JS_HORIZON_MEMORY_INVALID,
  JS_HORIZON_MEMORY_STATE,
  JS_HORIZON_MEMORY_NO_MEMORY,
  JS_HORIZON_MEMORY_SYSTEM,
  JS_HORIZON_MEMORY_UNSUPPORTED
} js_horizon_memory_status;

typedef enum {
  JS_HORIZON_MEMORY_NONE = 0,
  JS_HORIZON_MEMORY_READ,
  JS_HORIZON_MEMORY_READ_WRITE,
  JS_HORIZON_MEMORY_EXECUTE
} js_horizon_memory_access;

/* Private HotSpot backend. With nonzero process SystemResourceSize it reserves
 * Alias addresses and uses physical mapping SVCs. When the checked total is
 * zero, it eagerly allocates the full backing from the C heap instead; this
 * fallback consumes the full requested size until the last split is released.
 * Logical uncommit protects pages as NONE and recommit zeros them. It does not
 * release physical backing. raw_result is zero for bridge outcomes and carries
 * the exact SVC Result for system failures. Callers must quiesce accesses before
 * uncommit, protection changes, or release. */
js_horizon_memory_status js_horizon_memory_reserve(size_t size, size_t alignment,
                                                   void** address, uint32_t* raw_result);
js_horizon_memory_status js_horizon_memory_commit(void* address, size_t size,
                                                  uint32_t* raw_result);
js_horizon_memory_status js_horizon_memory_uncommit(void* address, size_t size,
                                                    uint32_t* raw_result);
js_horizon_memory_status js_horizon_memory_protect(void* address, size_t size,
                                                   js_horizon_memory_access access,
                                                   uint32_t* raw_result);
js_horizon_memory_status js_horizon_memory_protect_with_commit(
    void* address, size_t size, js_horizon_memory_access access, int committed,
    uint32_t* raw_result);
js_horizon_memory_status js_horizon_memory_release(void* address, size_t size,
                                                   uint32_t* raw_result);
js_horizon_memory_status js_horizon_memory_committed_in_range(
    void* start, size_t size, void** committed_start, size_t* committed_size,
    uint32_t* raw_result);

#ifdef __cplusplus
}
#endif

#endif
