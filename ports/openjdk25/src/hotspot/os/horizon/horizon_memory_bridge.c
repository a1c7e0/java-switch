/* SPDX-License-Identifier: GPL-2.0-only */
#include "horizon_memory_bridge.h"
#include <javaswitch/detail/horizon_alias.h>

#include <switch.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>

#define HORIZON_PAGE_SIZE ((size_t)4096)

typedef struct HorizonMemoryBacking {
  unsigned references;
  size_t size;
  unsigned char* address;
} HorizonMemoryBacking;

typedef struct HorizonMemoryRegion {
  struct HorizonMemoryRegion* next;
  uintptr_t address;
  size_t size;
  js_horizon_alias_reservation* reservation;
  HorizonMemoryBacking* backing;
  int heap_backed;
  unsigned char* mapped;
  unsigned char* permissions;
} HorizonMemoryRegion;

static Mutex g_horizon_memory_lock;
static HorizonMemoryRegion* g_horizon_memory_regions;

static void set_raw(uint32_t* raw_result, Result result) {
  if (raw_result != NULL) {
    *raw_result = (uint32_t)result;
  }
}

static int valid_page_range(uintptr_t address, size_t size) {
  return address != 0 && size != 0 &&
         (address & (HORIZON_PAGE_SIZE - 1)) == 0 &&
         (size & (HORIZON_PAGE_SIZE - 1)) == 0 &&
         address <= UINTPTR_MAX - size;
}

static HorizonMemoryRegion* find_region(uintptr_t address, size_t size) {
  uintptr_t end = address + size;
  for (HorizonMemoryRegion* region = g_horizon_memory_regions;
       region != NULL; region = region->next) {
    uintptr_t region_end = region->address + region->size;
    if (address >= region->address && end <= region_end) {
      return region;
    }
  }
  return NULL;
}

static void free_region(HorizonMemoryRegion* region) {
  if (region != NULL) {
    if (region->backing != NULL && region->backing->references > 1) {
      region->backing->references--;
    }
    free(region->mapped);
    free(region->permissions);
    free(region);
  }
}

static HorizonMemoryBacking* allocate_heap_backing(size_t size, size_t alignment,
                                                  uint32_t* raw_result,
                                                  int* system_failure) {
  if (system_failure != NULL) *system_failure = 0;
  if (!size || (size & (HORIZON_PAGE_SIZE - 1)) ||
      alignment < HORIZON_PAGE_SIZE || (alignment & (alignment - 1)) != 0 ||
      size > SIZE_MAX - (alignment - 1)) return NULL;
  HorizonMemoryBacking* backing = calloc(1, sizeof(*backing));
  if (backing == NULL) return NULL;
  backing->address = (unsigned char*)memalign(alignment, size);
  if (backing->address == NULL) {
    free(backing);
    return NULL;
  }
  backing->size = size;
  backing->references = 1;
  Result result = svcSetMemoryPermission(backing->address, size, Perm_None);
  if (R_FAILED(result)) {
    set_raw(raw_result, result);
    if (system_failure != NULL) *system_failure = 1;
    backing->references = 0;
    free(backing->address);
    free(backing);
    return NULL;
  }
  return backing;
}

static void retain_backing(HorizonMemoryBacking* backing) {
  if (backing != NULL) backing->references++;
}

static int release_backing(HorizonMemoryBacking* backing, uint32_t* raw_result) {
  if (backing == NULL || backing->references == 0) return 0;
  if (backing->references > 1) {
    backing->references--;
    return 1;
  }
  Result result = svcSetMemoryPermission(backing->address, backing->size, Perm_Rw);
  if (raw_result != NULL) *raw_result = (uint32_t)result;
  if (R_FAILED(result)) return 0;
  memset(backing->address, 0, backing->size);
  free(backing->address);
  free(backing);
  return 2;
}

static HorizonMemoryRegion* allocate_region(uintptr_t address, size_t size,
                                            const HorizonMemoryRegion* source,
                                            size_t source_offset) {
  HorizonMemoryRegion* region = calloc(1, sizeof(*region));
  if (region == NULL) {
    return NULL;
  }
  const size_t page_count = size / HORIZON_PAGE_SIZE;
  region->mapped = calloc(page_count, sizeof(*region->mapped));
  region->permissions = calloc(page_count, sizeof(*region->permissions));
  if (region->mapped == NULL || region->permissions == NULL) {
    free_region(region);
    return NULL;
  }
  region->address = address;
  region->size = size;
  if (source != NULL) {
    const size_t first_page = source_offset / HORIZON_PAGE_SIZE;
    memcpy(region->mapped, source->mapped + first_page, page_count);
    memcpy(region->permissions, source->permissions + first_page, page_count);
    region->backing = source->backing;
    region->heap_backed = source->heap_backed;
    if (region->backing != NULL) retain_backing(region->backing);
  }
  return region;
}

static int all_mapped(const HorizonMemoryRegion* region, size_t first_page,
                      size_t page_count, int expected) {
  for (size_t page = first_page; page < first_page + page_count; page++) {
    if ((region->mapped[page] != 0) != (expected != 0)) {
      return 0;
    }
  }
  return 1;
}

static js_horizon_memory_status verify_unmapped_noaccess(
    uintptr_t start, size_t size, uint32_t* raw_result) {
  const uintptr_t end = start + size;
  uintptr_t cursor = start;
  while (cursor < end) {
    MemoryInfo info;
    u32 page_info = 0;
    Result result = svcQueryMemory(&info, &page_info, (u64)cursor);
    if (R_FAILED(result)) {
      set_raw(raw_result, result);
      return JS_HORIZON_MEMORY_SYSTEM;
    }
    if (info.size == 0 || info.addr > cursor || info.addr > UINTPTR_MAX ||
        info.size > UINTPTR_MAX || info.addr > UINTPTR_MAX - (uintptr_t)info.size ||
        info.addr + info.size <= cursor || info.type != MemType_Unmapped ||
        info.perm != Perm_None) {
      return JS_HORIZON_MEMORY_STATE;
    }
    uintptr_t span_end = (uintptr_t)(info.addr + info.size);
    cursor = span_end < end ? span_end : end;
  }
  return JS_HORIZON_MEMORY_OK;
}

js_horizon_memory_status js_horizon_memory_reserve(size_t size, size_t alignment,
                                                   void** address,
                                                   uint32_t* raw_result) {
  set_raw(raw_result, 0);
  if (address == NULL) {
    return JS_HORIZON_MEMORY_INVALID;
  }
  *address = NULL;
  if (size == 0 || (size & (HORIZON_PAGE_SIZE - 1)) != 0 ||
      alignment < HORIZON_PAGE_SIZE || (alignment & (alignment - 1)) != 0 ||
      (alignment & (HORIZON_PAGE_SIZE - 1)) != 0 ||
      size > SIZE_MAX - (alignment - HORIZON_PAGE_SIZE)) {
    return JS_HORIZON_MEMORY_INVALID;
  }

  HorizonMemoryRegion* region = allocate_region(0, size, NULL, 0);
  if (region == NULL) {
    return JS_HORIZON_MEMORY_NO_MEMORY;
  }

  u64 system_resource_size = 0;
  Result resource_result = svcGetInfo(&system_resource_size,
      InfoType_SystemResourceSizeTotal, CUR_PROCESS_HANDLE, 0);
  if (R_SUCCEEDED(resource_result) && system_resource_size == 0) {
    int system_failure = 0;
    HorizonMemoryBacking* backing = allocate_heap_backing(size, alignment,
        raw_result, &system_failure);
    if (backing == NULL) {
      free_region(region);
      return system_failure ? JS_HORIZON_MEMORY_SYSTEM : JS_HORIZON_MEMORY_NO_MEMORY;
    }
    region->backing = backing;
    region->heap_backed = 1;
    region->address = (uintptr_t)backing->address;
    mutexLock(&g_horizon_memory_lock);
    region->next = g_horizon_memory_regions;
    g_horizon_memory_regions = region;
    mutexUnlock(&g_horizon_memory_lock);
    *address = backing->address;
    return JS_HORIZON_MEMORY_OK;
  }

  void* found = NULL;
  js_horizon_alias_status alias_status = js_horizon_alias_reserve(
      size, alignment, &found, &region->reservation, raw_result);
  js_horizon_memory_status status = alias_status == JS_HORIZON_ALIAS_OK ? JS_HORIZON_MEMORY_OK :
      alias_status == JS_HORIZON_ALIAS_NO_MEMORY ? JS_HORIZON_MEMORY_NO_MEMORY :
      alias_status == JS_HORIZON_ALIAS_INVALID ? JS_HORIZON_MEMORY_INVALID :
      alias_status == JS_HORIZON_ALIAS_STATE ? JS_HORIZON_MEMORY_STATE : JS_HORIZON_MEMORY_SYSTEM;
  if (status == JS_HORIZON_MEMORY_OK) {
    region->address = (uintptr_t)found;
    mutexLock(&g_horizon_memory_lock);
    region->next = g_horizon_memory_regions;
    g_horizon_memory_regions = region;
    mutexUnlock(&g_horizon_memory_lock);
  }

  if (status != JS_HORIZON_MEMORY_OK) {
    free_region(region);
    return status;
  }
  *address = (void*)region->address;
  return JS_HORIZON_MEMORY_OK;
}

js_horizon_memory_status js_horizon_memory_commit(void* address, size_t size,
                                                  uint32_t* raw_result) {
  set_raw(raw_result, 0);
  const uintptr_t start = (uintptr_t)address;
  if (!valid_page_range(start, size)) {
    return JS_HORIZON_MEMORY_INVALID;
  }
  mutexLock(&g_horizon_memory_lock);
  HorizonMemoryRegion* region = find_region(start, size);
  if (region == NULL) {
    mutexUnlock(&g_horizon_memory_lock);
    return JS_HORIZON_MEMORY_INVALID;
  }
  const size_t first_page = (start - region->address) / HORIZON_PAGE_SIZE;
  const size_t page_count = size / HORIZON_PAGE_SIZE;
  if (!all_mapped(region, first_page, page_count, 0)) {
    mutexUnlock(&g_horizon_memory_lock);
    return JS_HORIZON_MEMORY_STATE;
  }
  Result result;
  if (region->heap_backed) {
    result = svcSetMemoryPermission(address, size, Perm_Rw);
    if (R_SUCCEEDED(result)) memset(address, 0, size);
  } else {
    virtmemLock();
    result = svcMapPhysicalMemory(address, size);
    virtmemUnlock();
  }
  set_raw(raw_result, result);
  if (R_SUCCEEDED(result)) {
    memset(region->mapped + first_page, 1, page_count);
    memset(region->permissions + first_page, JS_HORIZON_MEMORY_READ_WRITE, page_count);
  }
  mutexUnlock(&g_horizon_memory_lock);
  return R_SUCCEEDED(result) ? JS_HORIZON_MEMORY_OK : JS_HORIZON_MEMORY_SYSTEM;
}

js_horizon_memory_status js_horizon_memory_uncommit(void* address, size_t size,
                                                    uint32_t* raw_result) {
  set_raw(raw_result, 0);
  const uintptr_t start = (uintptr_t)address;
  if (!valid_page_range(start, size)) {
    return JS_HORIZON_MEMORY_INVALID;
  }
  mutexLock(&g_horizon_memory_lock);
  HorizonMemoryRegion* region = find_region(start, size);
  if (region == NULL) {
    mutexUnlock(&g_horizon_memory_lock);
    return JS_HORIZON_MEMORY_INVALID;
  }
  const size_t first_page = (start - region->address) / HORIZON_PAGE_SIZE;
  const size_t page_count = size / HORIZON_PAGE_SIZE;
  if (!all_mapped(region, first_page, page_count, 1)) {
    mutexUnlock(&g_horizon_memory_lock);
    return JS_HORIZON_MEMORY_STATE;
  }
  Result result;
  if (region->heap_backed) {
    result = svcSetMemoryPermission(address, size, Perm_None);
  } else {
    virtmemLock();
    result = svcUnmapPhysicalMemory(address, size);
    virtmemUnlock();
  }
  set_raw(raw_result, result);
  if (R_SUCCEEDED(result)) {
    memset(region->mapped + first_page, 0, page_count);
    memset(region->permissions + first_page, JS_HORIZON_MEMORY_NONE, page_count);
  }
  mutexUnlock(&g_horizon_memory_lock);
  return R_SUCCEEDED(result) ? JS_HORIZON_MEMORY_OK : JS_HORIZON_MEMORY_SYSTEM;
}

js_horizon_memory_status js_horizon_memory_protect_with_commit(
    void* address, size_t size, js_horizon_memory_access access, int committed,
    uint32_t* raw_result) {
  set_raw(raw_result, 0);
  const uintptr_t start = (uintptr_t)address;
  if (!valid_page_range(start, size)) {
    return JS_HORIZON_MEMORY_INVALID;
  }
  if (access == JS_HORIZON_MEMORY_EXECUTE) {
    return JS_HORIZON_MEMORY_UNSUPPORTED;
  }
  if (!committed && access != JS_HORIZON_MEMORY_NONE) {
    return JS_HORIZON_MEMORY_UNSUPPORTED;
  }
  u32 permission;
  switch (access) {
    case JS_HORIZON_MEMORY_NONE: permission = Perm_None; break;
    case JS_HORIZON_MEMORY_READ: permission = Perm_R; break;
    case JS_HORIZON_MEMORY_READ_WRITE: permission = Perm_Rw; break;
    default: return JS_HORIZON_MEMORY_INVALID;
  }
  mutexLock(&g_horizon_memory_lock);
  HorizonMemoryRegion* region = find_region(start, size);
  if (region == NULL) {
    mutexUnlock(&g_horizon_memory_lock);
    return JS_HORIZON_MEMORY_INVALID;
  }
  const size_t first_page = (start - region->address) / HORIZON_PAGE_SIZE;
  const size_t page_count = size / HORIZON_PAGE_SIZE;
  if (!all_mapped(region, first_page, page_count, 1)) {
    if (access != JS_HORIZON_MEMORY_NONE) {
      mutexUnlock(&g_horizon_memory_lock);
      return JS_HORIZON_MEMORY_STATE;
    }
    if (!region->heap_backed) {
      /* Reserved Alias pages have no mapping. Verify those pages are already
       * unmapped/NONE and protect any committed subranges with the real
       * permission syscall. Keep the per-page permission record in sync if a
       * later run fails. */
      size_t page = first_page;
      while (page < first_page + page_count) {
        const int mapped = region->mapped[page] != 0;
        size_t run_end = page + 1;
        while (run_end < first_page + page_count &&
               (region->mapped[run_end] != 0) == mapped) run_end++;
        void* run_address = (void*)(region->address + page * HORIZON_PAGE_SIZE);
        size_t run_size = (run_end - page) * HORIZON_PAGE_SIZE;
        if (mapped) {
          Result result = svcSetMemoryPermission(run_address, run_size, Perm_None);
          set_raw(raw_result, result);
          if (R_FAILED(result)) {
            mutexUnlock(&g_horizon_memory_lock);
            return JS_HORIZON_MEMORY_SYSTEM;
          }
        } else {
          js_horizon_memory_status query_status = verify_unmapped_noaccess(
              (uintptr_t)run_address, run_size, raw_result);
          if (query_status != JS_HORIZON_MEMORY_OK) {
            mutexUnlock(&g_horizon_memory_lock);
            return query_status;
          }
        }
        memset(region->permissions + page, JS_HORIZON_MEMORY_NONE,
               run_end - page);
        page = run_end;
      }
      mutexUnlock(&g_horizon_memory_lock);
      return JS_HORIZON_MEMORY_OK;
    }
    /* The eager C-heap allocation is physically present and already has NONE
     * permission, even while its logical committed bit is clear. */
  }
  Result result = svcSetMemoryPermission(address, size, permission);
  set_raw(raw_result, result);
  if (R_SUCCEEDED(result)) {
    memset(region->permissions + first_page, (unsigned char)access, page_count);
  }
  mutexUnlock(&g_horizon_memory_lock);
  return R_SUCCEEDED(result) ? JS_HORIZON_MEMORY_OK : JS_HORIZON_MEMORY_SYSTEM;
}

js_horizon_memory_status js_horizon_memory_protect(void* address, size_t size,
                                                   js_horizon_memory_access access,
                                                   uint32_t* raw_result) {
  return js_horizon_memory_protect_with_commit(address, size, access, 1,
                                               raw_result);
}

js_horizon_memory_status js_horizon_memory_release(void* address, size_t size,
                                                   uint32_t* raw_result) {
  set_raw(raw_result, 0);
  const uintptr_t start = (uintptr_t)address;
  if (!valid_page_range(start, size)) {
    return JS_HORIZON_MEMORY_INVALID;
  }
  mutexLock(&g_horizon_memory_lock);
  HorizonMemoryRegion* region = find_region(start, size);
  if (region == NULL) {
    mutexUnlock(&g_horizon_memory_lock);
    return JS_HORIZON_MEMORY_INVALID;
  }

  const uintptr_t end = start + size;
  const uintptr_t region_end = region->address + region->size;
  const size_t left_size = (size_t)(start - region->address);
  const size_t right_size = (size_t)(region_end - end);
  HorizonMemoryRegion* left = NULL;
  HorizonMemoryRegion* right = NULL;
  if (left_size != 0) {
    left = allocate_region(region->address, left_size, region, 0);
  }
  if (right_size != 0) {
    right = allocate_region(end, right_size, region, size + left_size);
  }
  if ((left_size != 0 && left == NULL) || (right_size != 0 && right == NULL)) {
    free_region(left);
    free_region(right);
    mutexUnlock(&g_horizon_memory_lock);
    return JS_HORIZON_MEMORY_NO_MEMORY;
  }

  if (region->heap_backed) {
    Result result = svcSetMemoryPermission((void*)start, size, Perm_None);
    if (R_FAILED(result)) {
      set_raw(raw_result, result);
      free_region(left);
      free_region(right);
      mutexUnlock(&g_horizon_memory_lock);
      return JS_HORIZON_MEMORY_SYSTEM;
    }
    int backing_status = release_backing(region->backing, raw_result);
    if (backing_status == 0) {
      /* Final cleanup restores RW before freeing. If that fails, the original
       * ownership record remains intact for a later release retry. */
      memset(region->permissions + left_size / HORIZON_PAGE_SIZE,
             JS_HORIZON_MEMORY_NONE, size / HORIZON_PAGE_SIZE);
      free_region(left);
      free_region(right);
      mutexUnlock(&g_horizon_memory_lock);
      return JS_HORIZON_MEMORY_SYSTEM;
    }
    HorizonMemoryRegion** link = &g_horizon_memory_regions;
    while (*link != region) link = &(*link)->next;
    *link = region->next;
    if (left != NULL) {
      left->next = g_horizon_memory_regions;
      g_horizon_memory_regions = left;
    }
    if (right != NULL) {
      right->next = g_horizon_memory_regions;
      g_horizon_memory_regions = right;
    }
    region->backing = NULL; /* release_backing already dropped this reference */
    free_region(region);
    mutexUnlock(&g_horizon_memory_lock);
    return JS_HORIZON_MEMORY_OK;
  }

  js_horizon_alias_split* split = NULL;
  js_horizon_alias_status split_status = js_horizon_alias_split_begin(
      region->reservation, left_size, size, right_size, &split, raw_result);
  if (split_status != JS_HORIZON_ALIAS_OK) {
    free_region(left);
    free_region(right);
    mutexUnlock(&g_horizon_memory_lock);
    return split_status == JS_HORIZON_ALIAS_NO_MEMORY ? JS_HORIZON_MEMORY_NO_MEMORY :
        split_status == JS_HORIZON_ALIAS_INVALID ? JS_HORIZON_MEMORY_INVALID :
        split_status == JS_HORIZON_ALIAS_STATE ? JS_HORIZON_MEMORY_STATE : JS_HORIZON_MEMORY_SYSTEM;
  }

  virtmemLock();

  const size_t first_page = left_size / HORIZON_PAGE_SIZE;
  const size_t release_pages = size / HORIZON_PAGE_SIZE;
  size_t page = first_page;
  while (page < first_page + release_pages) {
    while (page < first_page + release_pages && !region->mapped[page]) page++;
    if (page == first_page + release_pages) break;
    size_t run_end = page + 1;
    while (run_end < first_page + release_pages && region->mapped[run_end]) run_end++;
    void* run_address = (void*)(region->address + page * HORIZON_PAGE_SIZE);
    const size_t run_size = (run_end - page) * HORIZON_PAGE_SIZE;
    Result result = svcUnmapPhysicalMemory(run_address, run_size);
    if (R_FAILED(result)) {
      set_raw(raw_result, result);
      virtmemUnlock();
      js_horizon_alias_split_rollback(split);
      free_region(left);
      free_region(right);
      mutexUnlock(&g_horizon_memory_lock);
      return JS_HORIZON_MEMORY_SYSTEM;
    }
    memset(region->mapped + page, 0, run_end - page);
    memset(region->permissions + page, JS_HORIZON_MEMORY_NONE, run_end - page);
    page = run_end;
  }

  HorizonMemoryRegion** link = &g_horizon_memory_regions;
  while (*link != region) link = &(*link)->next;
  *link = region->next;
  if (left != NULL) {
    left->next = g_horizon_memory_regions;
    g_horizon_memory_regions = left;
  }
  if (right != NULL) {
    right->next = g_horizon_memory_regions;
    g_horizon_memory_regions = right;
  }
  virtmemUnlock();
  js_horizon_alias_split_commit(split,
      left != NULL ? &left->reservation : NULL,
      right != NULL ? &right->reservation : NULL);
  free_region(region);
  mutexUnlock(&g_horizon_memory_lock);
  return JS_HORIZON_MEMORY_OK;
}

static int is_user_backed_memory(u32 type) {
  switch (type) {
    case MemType_Unmapped:
    case MemType_Io:
    case MemType_Reserved:
    case MemType_KernelStack:
    case MemType_CodeReadOnly:
    case MemType_CodeWritable:
    case MemType_Coverage:
    case MemType_Insecure:
      return 0;
    case MemType_Normal:
    case MemType_CodeStatic:
    case MemType_CodeMutable:
    case MemType_Heap:
    case MemType_SharedMem:
    case MemType_WeirdMappedMem:
    case MemType_ModuleCodeStatic:
    case MemType_ModuleCodeMutable:
    case MemType_IpcBuffer0:
    case MemType_MappedMemory:
    case MemType_ThreadLocal:
    case MemType_TransferMemIsolated:
    case MemType_TransferMem:
    case MemType_ProcessMem:
    case MemType_IpcBuffer1:
    case MemType_IpcBuffer3:
      return 1;
    default:
      return 0;
  }
}

js_horizon_memory_status js_horizon_memory_committed_in_range(
    void* start_address, size_t size, void** committed_start,
    size_t* committed_size, uint32_t* raw_result) {
  if (committed_start != NULL) *committed_start = NULL;
  if (committed_size != NULL) *committed_size = 0;
  set_raw(raw_result, 0);
  if (committed_start == NULL || committed_size == NULL ||
      !valid_page_range((uintptr_t)start_address, size)) {
    return JS_HORIZON_MEMORY_INVALID;
  }

  const uintptr_t request_start = (uintptr_t)start_address;
  const uintptr_t request_end = request_start + size;
  mutexLock(&g_horizon_memory_lock);
  for (HorizonMemoryRegion* region = g_horizon_memory_regions;
       region != NULL; region = region->next) {
    if (!region->heap_backed || request_start < region->address ||
        request_start >= region->address + region->size) continue;
    size_t page = (request_start - region->address) / HORIZON_PAGE_SIZE;
    const size_t limit = region->size / HORIZON_PAGE_SIZE;
    size_t request_limit = (size_t)((request_end - region->address) /
                                    HORIZON_PAGE_SIZE);
    if (request_limit > limit) request_limit = limit;
    while (page < request_limit && !region->mapped[page]) page++;
    if (page == request_limit) {
      mutexUnlock(&g_horizon_memory_lock);
      return JS_HORIZON_MEMORY_STATE;
    }
    size_t end_page = page + 1;
    while (end_page < request_limit && region->mapped[end_page])
      end_page++;
    uintptr_t run_start = region->address + page * HORIZON_PAGE_SIZE;
    uintptr_t run_end = region->address + end_page * HORIZON_PAGE_SIZE;
    mutexUnlock(&g_horizon_memory_lock);
    *committed_start = (void*)run_start;
    *committed_size = (size_t)(run_end - run_start);
    return JS_HORIZON_MEMORY_OK;
  }
  mutexUnlock(&g_horizon_memory_lock);
  uintptr_t cursor = request_start;
  uintptr_t run_start = 0;
  uintptr_t run_end = 0;
  int found_run = 0;

  mutexLock(&g_horizon_memory_lock);
  while (cursor < request_end) {
    MemoryInfo info;
    u32 page_info = 0;
    Result result = svcQueryMemory(&info, &page_info, (u64)cursor);
    if (R_FAILED(result)) {
      set_raw(raw_result, result);
      mutexUnlock(&g_horizon_memory_lock);
      *committed_start = NULL;
      *committed_size = 0;
      return JS_HORIZON_MEMORY_SYSTEM;
    }
    if (info.size == 0 || info.addr > cursor ||
        info.addr > UINTPTR_MAX || info.size > UINTPTR_MAX ||
        (info.addr & (HORIZON_PAGE_SIZE - 1)) != 0 ||
        (info.size & (HORIZON_PAGE_SIZE - 1)) != 0 ||
        info.addr > UINTPTR_MAX - (uintptr_t)info.size) {
      mutexUnlock(&g_horizon_memory_lock);
      return JS_HORIZON_MEMORY_INVALID;
    }
    const uintptr_t info_end = (uintptr_t)(info.addr + info.size);
    if (info_end <= cursor) {
      mutexUnlock(&g_horizon_memory_lock);
      return JS_HORIZON_MEMORY_INVALID;
    }
    const uintptr_t span_end = info_end < request_end ? info_end : request_end;
    if (is_user_backed_memory(info.type)) {
      if (!found_run) {
        found_run = 1;
        run_start = cursor;
      }
      run_end = span_end;
    } else if (found_run) {
      break;
    }
    cursor = span_end;
  }
  mutexUnlock(&g_horizon_memory_lock);

  if (!found_run) {
    return JS_HORIZON_MEMORY_STATE;
  }
  *committed_start = (void*)run_start;
  *committed_size = (size_t)(run_end - run_start);
  return JS_HORIZON_MEMORY_OK;
}
