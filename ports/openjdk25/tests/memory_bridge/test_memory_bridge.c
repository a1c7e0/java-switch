/* SPDX-License-Identifier: GPL-2.0-only */
#include "../../src/hotspot/os/horizon/horizon_memory_bridge.h"
#include <javaswitch/vmem.h>
#include <switch.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAGE_SIZE 4096u
#define TEST_BASE ((uintptr_t)0x10000000u)
#define TEST_PAGE_COUNT 8192u

static pthread_mutex_t g_virtmem_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_virtmem_locked;
static VirtmemReservation* g_reservations;
static unsigned char g_mapped[TEST_PAGE_COUNT];
static unsigned char g_permission[TEST_PAGE_COUNT];
static unsigned g_add_calls;
static unsigned g_remove_calls;
static unsigned g_map_calls;
static unsigned g_unmap_calls;
static unsigned g_permission_calls;
static unsigned g_add_fail_at;
static unsigned g_map_fail_at;
static unsigned g_unmap_fail_at;
static unsigned g_permission_fail_at;
static unsigned g_calloc_calls;
static unsigned g_calloc_fail_at;
static MemoryInfo g_query_spans[16];
static size_t g_query_span_count;
static size_t g_query_calls;
static unsigned g_query_fail_at;
static Result g_query_failure_result;

void* __real_calloc(size_t count, size_t size);
void* __wrap_calloc(size_t count, size_t size) {
  g_calloc_calls++;
  if (g_calloc_fail_at != 0 && g_calloc_calls == g_calloc_fail_at) return NULL;
  return __real_calloc(count, size);
}

void mutexLock(Mutex* mutex) { assert(pthread_mutex_lock(&mutex->mutex) == 0); }
void mutexUnlock(Mutex* mutex) { assert(pthread_mutex_unlock(&mutex->mutex) == 0); }
void virtmemLock(void) {
  assert(!g_virtmem_locked);
  assert(pthread_mutex_lock(&g_virtmem_mutex) == 0);
  g_virtmem_locked = 1;
}
void virtmemUnlock(void) {
  assert(g_virtmem_locked);
  g_virtmem_locked = 0;
  assert(pthread_mutex_unlock(&g_virtmem_mutex) == 0);
}

static int overlaps(uintptr_t left, size_t left_size, uintptr_t right, size_t right_size) {
  return left < right + right_size && right < left + left_size;
}

void* virtmemFindAslr(size_t size, size_t guard_size) {
  assert(g_virtmem_locked && guard_size == PAGE_SIZE && size != 0);
  uintptr_t candidate = TEST_BASE + PAGE_SIZE;
  for (; candidate <= TEST_BASE + TEST_PAGE_COUNT * PAGE_SIZE - size; candidate += PAGE_SIZE) {
    int occupied = 0;
    for (VirtmemReservation* reservation = g_reservations; reservation != NULL;
         reservation = reservation->next) {
      if (overlaps(candidate, size, reservation->address, reservation->size)) {
        occupied = 1;
        candidate = (reservation->address + reservation->size + PAGE_SIZE - 1) &
                    ~(uintptr_t)(PAGE_SIZE - 1);
        candidate -= PAGE_SIZE;
        break;
      }
    }
    if (!occupied) {
      return (void*)candidate;
    }
  }
  return NULL;
}

VirtmemReservation* virtmemAddReservation(void* address, size_t size) {
  assert(g_virtmem_locked && address != NULL && size != 0);
  g_add_calls++;
  if (g_add_fail_at != 0 && g_add_calls == g_add_fail_at) return NULL;
  VirtmemReservation* reservation = __real_calloc(1, sizeof(*reservation));
  if (reservation == NULL) return NULL;
  reservation->address = (uintptr_t)address;
  reservation->size = size;
  reservation->next = g_reservations;
  g_reservations = reservation;
  return reservation;
}

void virtmemRemoveReservation(VirtmemReservation* reservation) {
  assert(g_virtmem_locked && reservation != NULL);
  VirtmemReservation** link = &g_reservations;
  while (*link != reservation) {
    assert(*link != NULL);
    link = &(*link)->next;
  }
  *link = reservation->next;
  g_remove_calls++;
  free(reservation);
}

static size_t page_index(void* address, size_t size) {
  uintptr_t start = (uintptr_t)address;
  assert(start >= TEST_BASE && (start & (PAGE_SIZE - 1)) == 0);
  assert(size != 0 && (size & (PAGE_SIZE - 1)) == 0);
  assert(start <= TEST_BASE + TEST_PAGE_COUNT * PAGE_SIZE - size);
  return (size_t)((start - TEST_BASE) / PAGE_SIZE);
}

Result svcMapPhysicalMemory(void* address, uint64_t size64) {
  size_t size = (size_t)size64;
  size_t first = page_index(address, size);
  g_map_calls++;
  for (size_t page = first; page < first + size / PAGE_SIZE; page++) assert(!g_mapped[page]);
  if (g_map_fail_at != 0 && g_map_calls == g_map_fail_at) return 0x13579;
  memset(g_mapped + first, 1, size / PAGE_SIZE);
  memset(g_permission + first, JS_HORIZON_MEMORY_READ_WRITE, size / PAGE_SIZE);
  return 0;
}

Result svcUnmapPhysicalMemory(void* address, uint64_t size64) {
  size_t size = (size_t)size64;
  size_t first = page_index(address, size);
  g_unmap_calls++;
  for (size_t page = first; page < first + size / PAGE_SIZE; page++) assert(g_mapped[page]);
  if (g_unmap_fail_at != 0 && g_unmap_calls == g_unmap_fail_at) return 0x24680;
  memset(g_mapped + first, 0, size / PAGE_SIZE);
  memset(g_permission + first, JS_HORIZON_MEMORY_NONE, size / PAGE_SIZE);
  return 0;
}

Result svcSetMemoryPermission(void* address, uint64_t size64, u32 permission) {
  size_t size = (size_t)size64;
  size_t first = page_index(address, size);
  g_permission_calls++;
  assert(permission == Perm_None || permission == Perm_R || permission == Perm_Rw);
  for (size_t page = first; page < first + size / PAGE_SIZE; page++) assert(g_mapped[page]);
  if (g_permission_fail_at != 0 && g_permission_calls == g_permission_fail_at) return 0x35791;
  memset(g_permission + first, (unsigned char)permission, size / PAGE_SIZE);
  return 0;
}

Result svcGetInfo(u64* out, unsigned id, uint64_t handle, uint64_t sub_id) {
  assert(out != NULL && handle == CUR_PROCESS_HANDLE && sub_id == 0);
  switch (id) {
    case InfoType_AliasRegionAddress: *out = TEST_BASE; return 0;
    case InfoType_AliasRegionSize: *out = TEST_PAGE_COUNT * PAGE_SIZE; return 0;
    case InfoType_AliasRegionExtraSize: *out = 0; return 0;
    default: return 0x56789;
  }
}

Result svcQueryMemory(MemoryInfo* info, u32* page_info, u64 address) {
  assert(info != NULL && page_info != NULL);
  *page_info = 0;
  if (g_query_span_count == 0) {
    assert(address >= TEST_BASE && address < TEST_BASE + TEST_PAGE_COUNT * PAGE_SIZE);
    const size_t page = (size_t)((address - TEST_BASE) / PAGE_SIZE);
    size_t first = page, last = page + 1;
    while (first > 0 && g_mapped[first - 1] == g_mapped[page]) first--;
    while (last < TEST_PAGE_COUNT && g_mapped[last] == g_mapped[page]) last++;
    memset(info, 0, sizeof(*info));
    info->addr = TEST_BASE + first * PAGE_SIZE;
    info->size = (last - first) * PAGE_SIZE;
    info->type = g_mapped[page] ? MemType_MappedMemory : MemType_Unmapped;
    info->perm = g_mapped[page] ? Perm_Rw : Perm_None;
    return 0;
  }
  assert(g_query_calls < g_query_span_count);
  g_query_calls++;
  if (g_query_fail_at != 0 && g_query_calls == g_query_fail_at) {
    return g_query_failure_result;
  }
  *info = g_query_spans[g_query_calls - 1];
  return 0;
}

static void set_query_script(const MemoryInfo* spans, size_t count,
                             unsigned fail_at, Result failure_result) {
  assert(count <= sizeof(g_query_spans) / sizeof(g_query_spans[0]));
  memcpy(g_query_spans, spans, count * sizeof(*spans));
  g_query_span_count = count;
  g_query_calls = 0;
  g_query_fail_at = fail_at;
  g_query_failure_result = failure_result;
}

static size_t reservation_count(void) {
  size_t count = 0;
  for (VirtmemReservation* current = g_reservations; current != NULL; current = current->next) count++;
  return count;
}

static int address_reserved(uintptr_t address, size_t size) {
  for (VirtmemReservation* current = g_reservations; current != NULL; current = current->next) {
    if (overlaps(address, size, current->address, current->size)) return 1;
  }
  return 0;
}

static void expect_raw(js_horizon_memory_status status, uint32_t expected,
                       js_horizon_memory_status actual, uint32_t raw) {
  assert(actual == status);
  assert(raw == expected);
}

static void test_validation_and_reserve(void) {
  void* address = (void*)1;
  uint32_t raw = 99;
  js_horizon_memory_status status = js_horizon_memory_reserve(0, PAGE_SIZE, &address, &raw);
  expect_raw(JS_HORIZON_MEMORY_INVALID, 0, status, raw);
  assert(address == NULL && raw == 0);
  assert(js_horizon_memory_reserve(2 * PAGE_SIZE, 3 * PAGE_SIZE, &address, NULL) ==
         JS_HORIZON_MEMORY_INVALID);
  assert(js_horizon_memory_reserve(PAGE_SIZE, PAGE_SIZE, NULL, NULL) == JS_HORIZON_MEMORY_INVALID);
  assert(js_horizon_memory_reserve(SIZE_MAX & ~(size_t)(PAGE_SIZE - 1), SIZE_MAX / 2 + 1,
                                   &address, NULL) == JS_HORIZON_MEMORY_INVALID);
  assert(js_horizon_memory_reserve(4 * PAGE_SIZE, 64 * PAGE_SIZE, &address, &raw) ==
         JS_HORIZON_MEMORY_OK);
  assert(((uintptr_t)address & (64 * PAGE_SIZE - 1)) == 0 && raw == 0);
  assert(reservation_count() == 1);
  g_map_fail_at = g_map_calls + 1;
  assert(js_horizon_memory_commit(address, PAGE_SIZE, &raw) == JS_HORIZON_MEMORY_SYSTEM &&
         raw == 0x13579);
  g_map_fail_at = 0;
  assert(js_horizon_memory_commit(address, PAGE_SIZE, &raw) == JS_HORIZON_MEMORY_OK && raw == 0);
  assert(js_horizon_memory_commit(address, PAGE_SIZE, &raw) == JS_HORIZON_MEMORY_STATE && raw == 0);
  assert(js_horizon_memory_protect(address, PAGE_SIZE, JS_HORIZON_MEMORY_READ, &raw) ==
         JS_HORIZON_MEMORY_OK && raw == 0);
  assert(js_horizon_memory_protect(address, PAGE_SIZE, JS_HORIZON_MEMORY_EXECUTE, &raw) ==
         JS_HORIZON_MEMORY_UNSUPPORTED && raw == 0);
  assert(js_horizon_memory_protect(address, PAGE_SIZE, (js_horizon_memory_access)99, &raw) ==
         JS_HORIZON_MEMORY_INVALID && raw == 0);
  g_permission_fail_at = g_permission_calls + 1;
  assert(js_horizon_memory_protect(address, PAGE_SIZE, JS_HORIZON_MEMORY_NONE, &raw) ==
         JS_HORIZON_MEMORY_SYSTEM && raw == 0x35791);
  g_permission_fail_at = 0;
  g_unmap_fail_at = g_unmap_calls + 1;
  assert(js_horizon_memory_uncommit(address, PAGE_SIZE, &raw) == JS_HORIZON_MEMORY_SYSTEM &&
         raw == 0x24680);
  g_unmap_fail_at = 0;
  assert(js_horizon_memory_uncommit(address, PAGE_SIZE, &raw) == JS_HORIZON_MEMORY_OK && raw == 0);
  assert(js_horizon_memory_release(address, 4 * PAGE_SIZE, &raw) == JS_HORIZON_MEMORY_OK && raw == 0);
  assert(reservation_count() == 0);
}

static void test_release_split_reuse_and_rollback(void) {
  void* address = NULL;
  uint32_t raw = 0;
  assert(js_horizon_memory_reserve(6 * PAGE_SIZE, 64 * PAGE_SIZE, &address, &raw) ==
         JS_HORIZON_MEMORY_OK);
  assert(js_horizon_memory_commit(address, 6 * PAGE_SIZE, &raw) == JS_HORIZON_MEMORY_OK);
  void* released = (unsigned char*)address + 2 * PAGE_SIZE;
  size_t adds_before = g_add_calls;
  size_t removes_before = g_remove_calls;

  g_add_fail_at = (unsigned)g_add_calls + 2;
  assert(js_horizon_memory_release(released, 2 * PAGE_SIZE, &raw) ==
         JS_HORIZON_MEMORY_NO_MEMORY && raw == 0);
  g_add_fail_at = 0;
  assert(reservation_count() == 1 && g_add_calls == adds_before + 2);
  assert(g_remove_calls == removes_before + 1);
  assert(g_mapped[page_index(address, PAGE_SIZE) + 2]);

  g_calloc_fail_at = g_calloc_calls + 4;
  assert(js_horizon_memory_release(released, 2 * PAGE_SIZE, &raw) ==
         JS_HORIZON_MEMORY_NO_MEMORY && raw == 0);
  g_calloc_fail_at = 0;
  assert(reservation_count() == 1 && g_mapped[page_index(address, PAGE_SIZE) + 2]);

  assert(js_horizon_memory_release(released, 2 * PAGE_SIZE, &raw) == JS_HORIZON_MEMORY_OK && raw == 0);
  assert(reservation_count() == 2 && g_remove_calls == removes_before + 2);
  assert(!address_reserved((uintptr_t)released, 2 * PAGE_SIZE));
  assert(!g_mapped[page_index(released, PAGE_SIZE)] && !g_mapped[page_index(released, PAGE_SIZE) + 1]);

  void* reused = NULL;
  assert(js_horizon_memory_reserve(2 * PAGE_SIZE, PAGE_SIZE, &reused, &raw) == JS_HORIZON_MEMORY_OK);
  assert(reused == released);
  assert(js_horizon_memory_release(reused, 2 * PAGE_SIZE, &raw) == JS_HORIZON_MEMORY_OK);
  assert(js_horizon_memory_release(address, 2 * PAGE_SIZE, &raw) == JS_HORIZON_MEMORY_OK);
  assert(js_horizon_memory_release((unsigned char*)address + 4 * PAGE_SIZE, 2 * PAGE_SIZE, &raw) ==
         JS_HORIZON_MEMORY_OK);
  assert(reservation_count() == 0);
}

static void test_unmap_retry_tracks_successes(void) {
  void* address = NULL;
  uint32_t raw = 0;
  assert(js_horizon_memory_reserve(5 * PAGE_SIZE, PAGE_SIZE, &address, &raw) == JS_HORIZON_MEMORY_OK);
  assert(js_horizon_memory_commit((unsigned char*)address + PAGE_SIZE, PAGE_SIZE, &raw) ==
         JS_HORIZON_MEMORY_OK);
  assert(js_horizon_memory_commit((unsigned char*)address + 3 * PAGE_SIZE, PAGE_SIZE, &raw) ==
         JS_HORIZON_MEMORY_OK);
  void* released = (unsigned char*)address + PAGE_SIZE;
  g_unmap_fail_at = g_unmap_calls + 2;
  js_horizon_memory_status release_status =
      js_horizon_memory_release(released, 3 * PAGE_SIZE, &raw);
  assert(release_status == JS_HORIZON_MEMORY_SYSTEM && raw == 0x24680);
  g_unmap_fail_at = 0;
  assert(!g_mapped[page_index(address, PAGE_SIZE)] && !g_mapped[page_index(address, PAGE_SIZE) + 1] &&
         !g_mapped[page_index(address, PAGE_SIZE) + 2] && g_mapped[page_index(address, PAGE_SIZE) + 3]);
  assert(js_horizon_memory_commit((unsigned char*)address + 2 * PAGE_SIZE, PAGE_SIZE, &raw) ==
         JS_HORIZON_MEMORY_OK);
  assert(js_horizon_memory_release(released, 3 * PAGE_SIZE, &raw) == JS_HORIZON_MEMORY_OK);
  assert(reservation_count() == 2);
  assert(js_horizon_memory_release(address, PAGE_SIZE, &raw) == JS_HORIZON_MEMORY_OK);
  assert(js_horizon_memory_release((unsigned char*)address + 4 * PAGE_SIZE, PAGE_SIZE, &raw) ==
         JS_HORIZON_MEMORY_OK);
  assert(reservation_count() == 0);
}

static void test_sdk_vm_shared_alias_reservations(void) {
  void* vm_address = NULL;
  uint32_t raw = 0;
  assert(js_horizon_memory_reserve(5 * PAGE_SIZE, PAGE_SIZE,
                                  &vm_address, &raw) == JS_HORIZON_MEMORY_OK);
  const uintptr_t base = (uintptr_t)vm_address;
  assert(js_horizon_memory_commit((void*)(base + PAGE_SIZE), PAGE_SIZE, &raw) ==
         JS_HORIZON_MEMORY_OK);
  assert(js_horizon_memory_release((void*)(base + PAGE_SIZE), PAGE_SIZE, &raw) ==
         JS_HORIZON_MEMORY_OK);

  js_vmem* sdk_region = NULL;
  assert(js_vmem_reserve(PAGE_SIZE, &sdk_region) == JS_VMEM_OK);
  assert(js_vmem_address(sdk_region) == (void*)(base + PAGE_SIZE));
  assert(js_vmem_release(sdk_region, &raw) == JS_VMEM_OK);
  assert(js_horizon_memory_release((void*)base, PAGE_SIZE, &raw) ==
         JS_HORIZON_MEMORY_OK);
  assert(js_horizon_memory_release((void*)(base + 2 * PAGE_SIZE),
                                   3 * PAGE_SIZE, &raw) == JS_HORIZON_MEMORY_OK);
  assert(reservation_count() == 0);
}

static MemoryInfo memory_span(uintptr_t address, size_t size, u32 type, u32 permission) {
  MemoryInfo info = {0};
  info.addr = address;
  info.size = size;
  info.type = type;
  info.perm = permission;
  return info;
}

static void test_os_committed_query(void) {
  const uintptr_t base = TEST_BASE + 0x100000;
  void* committed = (void*)1;
  size_t committed_size = 99;
  uint32_t raw = 99;
  MemoryInfo contiguous[] = {
    memory_span(base, 2 * PAGE_SIZE, MemType_Unmapped, Perm_None),
    memory_span(base + 2 * PAGE_SIZE, 2 * PAGE_SIZE, MemType_Heap, Perm_None),
    memory_span(base + 4 * PAGE_SIZE, 2 * PAGE_SIZE, MemType_Normal, Perm_R)
  };
  set_query_script(contiguous, 3, 0, 0);
  assert(js_horizon_memory_committed_in_range((void*)base, 6 * PAGE_SIZE,
                                              &committed, &committed_size, &raw) ==
         JS_HORIZON_MEMORY_OK);
  assert(committed == (void*)(base + 2 * PAGE_SIZE) && committed_size == 4 * PAGE_SIZE && raw == 0);

  MemoryInfo clipped[] = {
    memory_span(base, 2 * PAGE_SIZE, MemType_Heap, Perm_None),
    memory_span(base + 2 * PAGE_SIZE, 2 * PAGE_SIZE, MemType_ThreadLocal, Perm_Rw),
    memory_span(base + 4 * PAGE_SIZE, 2 * PAGE_SIZE, MemType_Reserved, Perm_R)
  };
  set_query_script(clipped, 3, 0, 0);
  assert(js_horizon_memory_committed_in_range((void*)(base + PAGE_SIZE), 4 * PAGE_SIZE,
                                              &committed, &committed_size, &raw) ==
         JS_HORIZON_MEMORY_OK);
  assert(committed == (void*)(base + PAGE_SIZE) && committed_size == 3 * PAGE_SIZE && raw == 0);

  MemoryInfo no_backed[] = {
    memory_span(base, 2 * PAGE_SIZE, MemType_Reserved, Perm_R),
    memory_span(base + 2 * PAGE_SIZE, 2 * PAGE_SIZE, MemType_Unmapped, Perm_R)
  };
  set_query_script(no_backed, 2, 0, 0);
  committed = (void*)1;
  committed_size = 99;
  assert(js_horizon_memory_committed_in_range((void*)base, 4 * PAGE_SIZE,
                                              &committed, &committed_size, &raw) ==
         JS_HORIZON_MEMORY_STATE);
  assert(committed == NULL && committed_size == 0 && raw == 0);

  MemoryInfo fail_after_run[] = {
    memory_span(base, 2 * PAGE_SIZE, MemType_Heap, Perm_None),
    memory_span(base + 2 * PAGE_SIZE, 2 * PAGE_SIZE, MemType_Unmapped, Perm_None)
  };
  set_query_script(fail_after_run, 2, 2, 0x45678);
  committed = (void*)1;
  committed_size = 99;
  assert(js_horizon_memory_committed_in_range((void*)base, 4 * PAGE_SIZE,
                                              &committed, &committed_size, &raw) ==
         JS_HORIZON_MEMORY_SYSTEM);
  assert(committed == NULL && committed_size == 0 && raw == 0x45678);

  MemoryInfo malformed[] = { memory_span(base, 0, MemType_Heap, Perm_Rw) };
  set_query_script(malformed, 1, 0, 0);
  committed = (void*)1;
  committed_size = 99;
  assert(js_horizon_memory_committed_in_range((void*)base, PAGE_SIZE,
                                              &committed, &committed_size, &raw) ==
         JS_HORIZON_MEMORY_INVALID);
  assert(committed == NULL && committed_size == 0 && raw == 0);

  MemoryInfo overflowing[] = {
    memory_span(base, SIZE_MAX & ~(size_t)(PAGE_SIZE - 1), MemType_Heap, Perm_Rw)
  };
  set_query_script(overflowing, 1, 0, 0);
  assert(js_horizon_memory_committed_in_range((void*)base, PAGE_SIZE,
                                              &committed, &committed_size, &raw) ==
         JS_HORIZON_MEMORY_INVALID);
  assert(committed == NULL && committed_size == 0 && raw == 0);

  MemoryInfo starts_after_query[] = {
    memory_span(base + PAGE_SIZE, PAGE_SIZE, MemType_Heap, Perm_Rw)
  };
  set_query_script(starts_after_query, 1, 0, 0);
  assert(js_horizon_memory_committed_in_range((void*)base, PAGE_SIZE,
                                              &committed, &committed_size, &raw) ==
         JS_HORIZON_MEMORY_INVALID);
  assert(committed == NULL && committed_size == 0 && raw == 0);

  assert(js_horizon_memory_committed_in_range((void*)base, 0,
                                              &committed, &committed_size, &raw) ==
         JS_HORIZON_MEMORY_INVALID);
  assert(committed == NULL && committed_size == 0 && raw == 0);
  const uintptr_t near_end = UINTPTR_MAX & ~(uintptr_t)(PAGE_SIZE - 1);
  assert(js_horizon_memory_committed_in_range((void*)(near_end - PAGE_SIZE), 2 * PAGE_SIZE,
                                              &committed, &committed_size, &raw) ==
         JS_HORIZON_MEMORY_INVALID);
  assert(committed == NULL && committed_size == 0 && raw == 0);
}

int main(void) {
  test_validation_and_reserve();
  test_release_split_reuse_and_rollback();
  test_unmap_retry_tracks_successes();
  test_sdk_vm_shared_alias_reservations();
  test_os_committed_query();
  assert(reservation_count() == 0);
  assert(!g_virtmem_locked);
  puts("Horizon memory bridge mocked libnx tests: PASS");
  return 0;
}
