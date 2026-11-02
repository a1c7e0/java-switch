/* SPDX-License-Identifier: GPL-2.0-only */
#include "horizon_memory_bridge.h"
#include <javaswitch/vmem.h>
#include <switch.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ALIAS_BASE ((uintptr_t)0x10000000)
#define ALIAS_SIZE ((size_t)(256 * 4096))
#define ALIAS_EXTRA_SIZE ((size_t)(2 * 4096))
#define ALIAS_PAGES (ALIAS_SIZE / 4096)
#define MAX_RESERVATIONS 128

struct VirtmemReservation { uintptr_t address; size_t size; int active; };
static struct VirtmemReservation reservations[MAX_RESERVATIONS];
static unsigned char mapped[ALIAS_PAGES];
static int virtmem_locked;
static int getinfo_failure_id = -1;
static int query_failure;
static int alias_full;
static int map_failure;
static int unmap_failure;
static int protect_failure;
static unsigned map_calls, unmap_calls, protect_calls;

void virtmemLock(void) { assert(!virtmem_locked); virtmem_locked = 1; }
void virtmemUnlock(void) { assert(virtmem_locked); virtmem_locked = 0; }
void* virtmemFindAslr(size_t, size_t) {
  assert(!"svcMapPhysicalMemory bridge used the ASLR region");
  return NULL;
}
VirtmemReservation* virtmemAddReservation(void* address, size_t size) {
  uintptr_t start = (uintptr_t)address;
  assert(virtmem_locked && start >= ALIAS_BASE && start <= ALIAS_BASE + ALIAS_SIZE);
  assert(size != 0 && size <= ALIAS_BASE + ALIAS_SIZE - start);
  for (size_t i = 0; i < MAX_RESERVATIONS; ++i) {
    if (!reservations[i].active) {
      reservations[i] = (struct VirtmemReservation){start, size, 1};
      return &reservations[i];
    }
  }
  return NULL;
}
void virtmemRemoveReservation(VirtmemReservation* reservation) {
  assert(virtmem_locked && reservation != NULL && reservation->active);
  reservation->active = 0;
}

Result svcGetInfo(u64* out, unsigned id, uint64_t handle, uint64_t sub_id) {
  assert(handle == CUR_PROCESS_HANDLE && sub_id == 0);
  if ((int)id == getinfo_failure_id) return 0x81;
  switch (id) {
    case InfoType_AliasRegionAddress: *out = ALIAS_BASE; return 0;
    case InfoType_AliasRegionSize: *out = ALIAS_SIZE; return 0;
    case InfoType_AliasRegionExtraSize: *out = ALIAS_EXTRA_SIZE; return 0;
    default: return 0x82;
  }
}

Result svcQueryMemory(MemoryInfo* info, u32* page_info, u64 address) {
  if (query_failure) return 0x83;
  assert(address >= ALIAS_BASE && address < ALIAS_BASE + ALIAS_SIZE);
  const size_t page = (size_t)((address - ALIAS_BASE) / 4096);
  size_t first = page, last = page + 1;
  while (first > 0 && mapped[first - 1] == mapped[page]) --first;
  while (last < ALIAS_PAGES && mapped[last] == mapped[page]) ++last;
  info->addr = ALIAS_BASE + first * 4096;
  info->size = (last - first) * 4096;
  info->type = (mapped[page] || alias_full) ? 2 : MemType_Unmapped;
  info->attr = info->perm = info->ipc_refcount = info->device_refcount = info->padding = 0;
  *page_info = 0;
  return 0;
}

static size_t page_index(void* address, size_t size) {
  uintptr_t start = (uintptr_t)address;
  assert(start >= ALIAS_BASE && !(start & 4095));
  assert(size && !(size & 4095) && size <= ALIAS_BASE + ALIAS_SIZE - start);
  return (size_t)((start - ALIAS_BASE) / 4096);
}

Result svcMapPhysicalMemory(void* address, u64 size) {
  ++map_calls;
  const size_t first = page_index(address, (size_t)size);
  for (size_t i = first; i < first + size / 4096; ++i) assert(!mapped[i]);
  if (map_failure) return 0x84;
  memset(mapped + first, 1, size / 4096);
  return 0;
}
Result svcUnmapPhysicalMemory(void* address, u64 size) {
  ++unmap_calls;
  const size_t first = page_index(address, (size_t)size);
  for (size_t i = first; i < first + size / 4096; ++i) assert(mapped[i]);
  if (unmap_failure) return 0x85;
  memset(mapped + first, 0, size / 4096);
  return 0;
}
Result svcSetMemoryPermission(void* address, u64 size, u32 permission) {
  ++protect_calls;
  (void)page_index(address, (size_t)size);
  assert(permission == Perm_None || permission == Perm_R || permission == Perm_Rw);
  return protect_failure ? 0x86 : 0;
}

static void verifies_alias_allocation(void) {
  void* first = NULL;
  void* second = NULL;
  uint32_t raw = 999;
  assert(js_horizon_memory_reserve(8192, 65536, &first, &raw) == JS_HORIZON_MEMORY_OK);
  assert(first != NULL && raw == 0 && (uintptr_t)first >= ALIAS_BASE);
  assert((uintptr_t)first + 8192 <= ALIAS_BASE + ALIAS_SIZE - ALIAS_EXTRA_SIZE);
  assert(((uintptr_t)first & 65535) == 0);
  assert(js_horizon_memory_reserve(8192, 65536, &second, &raw) == JS_HORIZON_MEMORY_OK);
  assert(second != first && ((uintptr_t)second & 65535) == 0);
  assert((uintptr_t)second + 8192 <= ALIAS_BASE + ALIAS_SIZE - ALIAS_EXTRA_SIZE);
  assert((uintptr_t)first + 8192 <= (uintptr_t)second ||
         (uintptr_t)second + 8192 <= (uintptr_t)first);

  map_failure = 1;
  assert(js_horizon_memory_commit(first, 4096, &raw) == JS_HORIZON_MEMORY_SYSTEM);
  assert(raw == 0x84);
  map_failure = 0;
  assert(js_horizon_memory_commit(first, 4096, &raw) == JS_HORIZON_MEMORY_OK);
  assert(raw == 0 && map_calls == 2);
  protect_failure = 1;
  assert(js_horizon_memory_protect(first, 4096, JS_HORIZON_MEMORY_READ, &raw) == JS_HORIZON_MEMORY_SYSTEM);
  assert(raw == 0x86);
  protect_failure = 0;
  assert(js_horizon_memory_protect(first, 4096, JS_HORIZON_MEMORY_READ, &raw) == JS_HORIZON_MEMORY_OK);
  assert(protect_calls == 2);
  assert(js_horizon_memory_release(first, 8192, &raw) == JS_HORIZON_MEMORY_OK);
  assert(js_horizon_memory_release(second, 8192, &raw) == JS_HORIZON_MEMORY_OK);
  assert(!virtmem_locked);
}

static void reports_region_errors_and_exhaustion(void) {
  void* address = (void*)1;
  uint32_t raw = 0;
  getinfo_failure_id = InfoType_AliasRegionAddress;
  assert(js_horizon_memory_reserve(4096, 4096, &address, &raw) == JS_HORIZON_MEMORY_SYSTEM);
  assert(address == NULL && raw == 0x81);
  getinfo_failure_id = -1;
  query_failure = 1;
  assert(js_horizon_memory_reserve(4096, 4096, &address, &raw) == JS_HORIZON_MEMORY_SYSTEM);
  assert(address == NULL && raw == 0x83);
  query_failure = 0;
  alias_full = 1;
  assert(js_horizon_memory_reserve(4096, 4096, &address, &raw) == JS_HORIZON_MEMORY_NO_MEMORY);
  assert(address == NULL && raw == 0);
  alias_full = 0;
}

static void sdk_and_vm_share_alias_reservations(void) {
  void* vm = NULL;
  uint32_t raw = 0;
  assert(js_horizon_memory_reserve(5 * 4096, 4096, &vm, &raw) ==
         JS_HORIZON_MEMORY_OK);
  const uintptr_t base = (uintptr_t)vm;
  assert(js_horizon_memory_commit((void*)(base + 4096), 4096, &raw) ==
         JS_HORIZON_MEMORY_OK);

  /* Failed unmap rolls back the children and retains the whole parent. */
  unmap_failure = 1;
  assert(js_horizon_memory_release((void*)(base + 4096), 4096, &raw) ==
         JS_HORIZON_MEMORY_SYSTEM);
  assert(raw == 0x85);
  js_vmem* during_rollback = NULL;
  assert(js_vmem_reserve(4096, &during_rollback) == JS_VMEM_OK);
  assert((uintptr_t)js_vmem_address(during_rollback) >= base + 5 * 4096);
  assert(js_vmem_release(during_rollback, &raw) == JS_VMEM_OK);
  unmap_failure = 0;

  /* Replacing the parent with left/right reservations exposes exactly the
   * released page to the SDK allocator while retaining both VM neighbors. */
  assert(js_horizon_memory_release((void*)(base + 4096), 4096, &raw) ==
         JS_HORIZON_MEMORY_OK);
  js_vmem* sdk = NULL;
  assert(js_vmem_reserve(4096, &sdk) == JS_VMEM_OK);
  assert(js_vmem_address(sdk) == (void*)(base + 4096));
  assert(js_vmem_release(sdk, &raw) == JS_VMEM_OK);
  assert(js_horizon_memory_release((void*)base, 4096, &raw) ==
         JS_HORIZON_MEMORY_OK);
  assert(js_horizon_memory_release((void*)(base + 2 * 4096), 3 * 4096,
                                   &raw) == JS_HORIZON_MEMORY_OK);
}

int main(void) {
  verifies_alias_allocation();
  reports_region_errors_and_exhaustion();
  sdk_and_vm_share_alias_reservations();
  assert(unmap_calls == 3 && !virtmem_locked);
  for (size_t i = 0; i < MAX_RESERVATIONS; ++i) assert(!reservations[i].active);
  puts("Horizon physical-memory bridge Alias-region placement and failures: PASS");
  return 0;
}
