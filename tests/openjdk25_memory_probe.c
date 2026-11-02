/* SPDX-License-Identifier: GPL-2.0-only */
#include "platform.h"
#include "../ports/openjdk25/src/hotspot/os/horizon/horizon_memory_bridge.h"
#include <javaswitch/vmem.h>

#include <stdint.h>

#define PROBE_PAGE_SIZE ((size_t)4096)
#define PROBE_PRIVATE_SIZE (4 * PROBE_PAGE_SIZE)
#define PROBE_ALIGNMENT (16 * PROBE_PAGE_SIZE)

typedef struct ProbeRange {
  void* address;
  size_t size;
} ProbeRange;

/* These fields are the ownership record. Never clear one until its matching
 * bridge release has succeeded; a later button press retries remaining work. */
static void* g_private_parent;
static ProbeRange g_private_children[2];
static size_t g_private_child_count;
static js_vmem* g_sdk_reservation;

static const char* memory_status_name(js_horizon_memory_status status) {
  switch (status) {
    case JS_HORIZON_MEMORY_OK: return "OK";
    case JS_HORIZON_MEMORY_INVALID: return "INVALID";
    case JS_HORIZON_MEMORY_STATE: return "STATE";
    case JS_HORIZON_MEMORY_NO_MEMORY: return "NO_MEMORY";
    case JS_HORIZON_MEMORY_SYSTEM: return "SYSTEM";
    case JS_HORIZON_MEMORY_UNSUPPORTED: return "UNSUPPORTED";
    default: return "UNKNOWN";
  }
}

static const char* sdk_status_name(js_vmem_status status) {
  switch (status) {
    case JS_VMEM_OK: return "OK";
    case JS_VMEM_INVALID: return "INVALID";
    case JS_VMEM_STATE: return "STATE";
    case JS_VMEM_NO_MEMORY: return "NO_MEMORY";
    case JS_VMEM_SYSTEM: return "SYSTEM";
    case JS_VMEM_UNSUPPORTED: return "UNSUPPORTED";
    default: return "UNKNOWN";
  }
}

static int report_memory(const char* operation,
                         js_horizon_memory_status status, uint32_t raw) {
  sc_log("memory probe %s: %s raw=0x%x\n", operation,
         memory_status_name(status), raw);
  return status == JS_HORIZON_MEMORY_OK;
}

static int report_sdk(const char* operation, js_vmem_status status,
                      uint32_t raw) {
  sc_log("memory probe %s: %s raw=0x%x\n", operation,
         sdk_status_name(status), raw);
  return status == JS_VMEM_OK;
}

static void remove_child(size_t index) {
  for (size_t i = index + 1; i < g_private_child_count; ++i)
    g_private_children[i - 1] = g_private_children[i];
  --g_private_child_count;
  g_private_children[g_private_child_count] = (ProbeRange){0};
}

static int retry_cleanup(void) {
  int clean = 1;
  uint32_t raw = 0;
  if (g_sdk_reservation != NULL) {
    js_vmem_status status = js_vmem_release(g_sdk_reservation, &raw);
    clean &= report_sdk("cleanup SDK reservation", status, raw);
    if (status == JS_VMEM_OK) g_sdk_reservation = NULL;
  }
  if (g_private_parent != NULL) {
    raw = 0;
    js_horizon_memory_status status = js_horizon_memory_release(
        g_private_parent, PROBE_PRIVATE_SIZE, &raw);
    clean &= report_memory("cleanup private parent", status, raw);
    if (status == JS_HORIZON_MEMORY_OK) g_private_parent = NULL;
  }
  size_t index = 0;
  while (index < g_private_child_count) {
    ProbeRange child = g_private_children[index];
    raw = 0;
    js_horizon_memory_status status = js_horizon_memory_release(
        child.address, child.size, &raw);
    clean &= report_memory("cleanup private child", status, raw);
    if (status == JS_HORIZON_MEMORY_OK) remove_child(index);
    else ++index;
  }
  return clean && g_sdk_reservation == NULL && g_private_parent == NULL &&
         g_private_child_count == 0;
}

static int ranges_overlap(const void* left, size_t left_size,
                          const void* right, size_t right_size) {
  const uintptr_t left_start = (uintptr_t)left;
  const uintptr_t right_start = (uintptr_t)right;
  return left_start < right_start + right_size &&
         right_start < left_start + left_size;
}

static int check_canary(const volatile uint32_t* slot, uint32_t expected,
                        const char* label) {
  const uint32_t actual = *slot;
  const int passed = actual == expected;
  sc_log("memory probe %s canary: %s expected=0x%x actual=0x%x\n",
         label, passed ? "PASS" : "FAIL", expected, actual);
  return passed;
}

void js_zero_memory_probe(void) {
  sc_log("Native Alias memory probe: begin (physical mapping syscalls; no VM execution)\n");
  if (!retry_cleanup()) {
    sc_log("Native Alias memory probe: prior cleanup still owns resources; retry later\n");
    return;
  }

  int passed = 1;
  uint32_t raw = 0;
  void* base = NULL;
  js_horizon_memory_status reserve_status = js_horizon_memory_reserve(
      PROBE_PRIVATE_SIZE, PROBE_ALIGNMENT, &base, &raw);
  passed &= report_memory("reserve aligned private 16 KiB", reserve_status, raw);
  if (reserve_status != JS_HORIZON_MEMORY_OK) goto done;
  g_private_parent = base;
  if (((uintptr_t)base & (PROBE_ALIGNMENT - 1)) != 0) {
    sc_log("memory probe alignment: FAIL address=%p required=%zu\n",
           base, PROBE_ALIGNMENT);
    passed = 0;
    goto done;
  }
  sc_log("memory probe alignment: PASS address=%p alignment=%zu\n",
         base, PROBE_ALIGNMENT);

  raw = 0;
  js_horizon_memory_status status = js_horizon_memory_commit(
      base, PROBE_PRIVATE_SIZE, &raw);
  passed &= report_memory("commit private 4 pages", status, raw);
  if (status != JS_HORIZON_MEMORY_OK) goto done;

  volatile uint32_t* canaries[4];
  const uint32_t expected[4] = {
    0x484d0000u, 0x484d0001u, 0x484d0002u, 0x484d0003u
  };
  for (size_t page = 0; page < 4; ++page) {
    canaries[page] = (volatile uint32_t*)((uintptr_t)base + page * PROBE_PAGE_SIZE);
    *canaries[page] = expected[page];
  }
  for (size_t page = 0; page < 4; ++page)
    passed &= check_canary(canaries[page], expected[page], "initial");
  if (!passed) goto done;

  raw = 0;
  status = js_horizon_memory_protect(base, PROBE_PAGE_SIZE,
      JS_HORIZON_MEMORY_READ, &raw);
  passed &= report_memory("protect page 0 read-only", status, raw);
  if (status != JS_HORIZON_MEMORY_OK) goto done;
  raw = 0;
  status = js_horizon_memory_protect(base, PROBE_PAGE_SIZE,
      JS_HORIZON_MEMORY_NONE, &raw);
  passed &= report_memory("protect page 0 no-access", status, raw);
  if (status != JS_HORIZON_MEMORY_OK) goto done;
  raw = 0;
  status = js_horizon_memory_protect(base, PROBE_PAGE_SIZE,
      JS_HORIZON_MEMORY_READ_WRITE, &raw);
  passed &= report_memory("restore page 0 read-write", status, raw);
  if (status != JS_HORIZON_MEMORY_OK) goto done;
  passed &= check_canary(canaries[0], expected[0], "protection round-trip");
  if (!passed) goto done;

  void* page_one = (void*)((uintptr_t)base + PROBE_PAGE_SIZE);
  raw = 0;
  status = js_horizon_memory_uncommit(page_one, PROBE_PAGE_SIZE, &raw);
  passed &= report_memory("uncommit page 1", status, raw);
  if (status != JS_HORIZON_MEMORY_OK) goto done;
  raw = 0;
  status = js_horizon_memory_commit(page_one, PROBE_PAGE_SIZE, &raw);
  passed &= report_memory("recommit page 1", status, raw);
  if (status != JS_HORIZON_MEMORY_OK) goto done;
  *canaries[1] = expected[1];
  passed &= check_canary(canaries[1], expected[1], "recommit");
  if (!passed) goto done;

  void* committed_start = NULL;
  size_t committed_size = 0;
  raw = 0;
  status = js_horizon_memory_committed_in_range(base, PROBE_PRIVATE_SIZE,
      &committed_start, &committed_size, &raw);
  passed &= report_memory("query committed range", status, raw);
  if (status != JS_HORIZON_MEMORY_OK) goto done;
  const int exact_query = committed_start == base &&
                          committed_size == PROBE_PRIVATE_SIZE;
  sc_log("memory probe committed range: %s start=%p size=%zu\n",
         exact_query ? "PASS" : "FAIL", committed_start, committed_size);
  passed &= exact_query;
  if (!passed) goto done;

  raw = 0;
  status = js_horizon_memory_release(page_one, PROBE_PAGE_SIZE, &raw);
  passed &= report_memory("release middle page 1", status, raw);
  if (status != JS_HORIZON_MEMORY_OK) goto done;
  g_private_parent = NULL;
  g_private_children[0] = (ProbeRange){base, PROBE_PAGE_SIZE};
  g_private_children[1] = (ProbeRange){(void*)((uintptr_t)base + 2 * PROBE_PAGE_SIZE),
                                       2 * PROBE_PAGE_SIZE};
  g_private_child_count = 2;

  passed &= check_canary(canaries[0], expected[0], "left child");
  passed &= check_canary(canaries[2], expected[2], "right child");
  passed &= check_canary(canaries[3], expected[3], "right child");
  if (!passed) goto done;

  js_vmem* sdk_region = NULL;
  js_vmem_status sdk_status = js_vmem_reserve_aligned(
      PROBE_PAGE_SIZE, PROBE_PAGE_SIZE, &sdk_region);
  passed &= report_sdk("reserve SDK 4 KiB", sdk_status, 0);
  if (sdk_status != JS_VMEM_OK) goto done;
  g_sdk_reservation = sdk_region;
  void* sdk_address = js_vmem_address(sdk_region);
  const int separate =
      !ranges_overlap(sdk_address, PROBE_PAGE_SIZE,
                      g_private_children[0].address, g_private_children[0].size) &&
      !ranges_overlap(sdk_address, PROBE_PAGE_SIZE,
                      g_private_children[1].address, g_private_children[1].size);
  sc_log("memory probe shared SDK/VM reservation: %s sdk=%p left=%p+%zu right=%p+%zu\n",
         separate ? "PASS" : "FAIL", sdk_address,
         g_private_children[0].address, g_private_children[0].size,
         g_private_children[1].address, g_private_children[1].size);
  passed &= separate;

done:
  if (!retry_cleanup()) passed = 0;
  if (passed)
    sc_log("Native Alias memory probe: native PASS (actual Horizon bridge syscalls; hardware result only)\n");
  else
    sc_log("Native Alias memory probe: FAIL; retained ownership will be retried on next press\n");
}
