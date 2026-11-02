/* SPDX-License-Identifier: GPL-2.0-only */
#include "runtime/os.hpp"
#include "runtime/globals.hpp"
#include "runtime/java.hpp"
#include "nmt/memTracker.hpp"
#include "logging/log.hpp"
#include "utilities/align.hpp"
#include "horizon_memory_bridge.h"
#include <errno.h>

static bool memory_result(js_horizon_memory_status status, uint32_t raw, const char* operation) {
  if (status == JS_HORIZON_MEMORY_OK) return true;
  switch (status) {
    case JS_HORIZON_MEMORY_NO_MEMORY: errno = ENOMEM; break;
    case JS_HORIZON_MEMORY_UNSUPPORTED: errno = ENOTSUP; break;
    case JS_HORIZON_MEMORY_SYSTEM: errno = EIO; break;
    default: errno = EINVAL; break;
  }
  log_warning(os, map)("Horizon %s failed: status=%d Result=0x%x", operation, status, raw);
  return false;
}

char* os::pd_reserve_memory(size_t bytes, bool executable) {
  if (executable) { errno = ENOTSUP; return nullptr; }
  void* result = nullptr; uint32_t raw = 0;
  auto status = js_horizon_memory_reserve(bytes, 4096, &result, &raw);
  return memory_result(status, raw, "reserve") ? static_cast<char*>(result) : nullptr;
}

char* os::reserve_memory_aligned(size_t bytes, size_t alignment, MemTag tag, bool executable) {
  if (executable) { errno = ENOTSUP; return nullptr; }
  void* result = nullptr; uint32_t raw = 0;
  auto status = js_horizon_memory_reserve(bytes, alignment, &result, &raw);
  if (!memory_result(status, raw, "aligned reserve")) return nullptr;
  MemTracker::record_virtual_memory_reserve(static_cast<address>(result), bytes, CALLER_PC, tag);
  return static_cast<char*>(result);
}

char* os::pd_attempt_reserve_memory_at(char*, size_t, bool) {
  // The public libnx finder does not offer collision-safe fixed placement.
  // Never overwrite a live mapping to satisfy an address hint.
  errno = ENOTSUP;
  return nullptr;
}

bool os::pd_commit_memory(char* addr, size_t bytes, bool executable) {
  if (executable) { errno = ENOTSUP; return false; }
  uint32_t raw = 0;
  auto status = js_horizon_memory_commit(addr, bytes, &raw);
  return memory_result(status, raw, "commit");
}

bool os::pd_commit_memory(char* addr, size_t bytes, size_t alignment_hint, bool executable) {
  // alignment_hint is advisory, not a request for unsupported huge pages.
  return pd_commit_memory(addr, bytes, executable);
}

void os::pd_commit_memory_or_exit(char* addr, size_t bytes, bool executable, const char* message) {
  if (!pd_commit_memory(addr, bytes, executable)) vm_exit_out_of_memory(bytes, OOM_MMAP_ERROR, "%s", message);
}

void os::pd_commit_memory_or_exit(char* addr, size_t bytes, size_t hint, bool executable, const char* message) {
  if (!pd_commit_memory(addr, bytes, hint, executable)) vm_exit_out_of_memory(bytes, OOM_MMAP_ERROR, "%s", message);
}

bool os::pd_uncommit_memory(char* addr, size_t bytes, bool executable) {
  if (executable) { errno = ENOTSUP; return false; }
  uint32_t raw = 0;
  auto status = js_horizon_memory_uncommit(addr, bytes, &raw);
  return memory_result(status, raw, "uncommit");
}

bool os::pd_release_memory(char* addr, size_t bytes) {
  uint32_t raw = 0;
  auto status = js_horizon_memory_release(addr, bytes, &raw);
  return memory_result(status, raw, "release");
}

bool os::protect_memory(char* addr, size_t bytes, ProtType protection, bool committed) {
  if (!committed) { errno = ENOTSUP; return false; }
  js_horizon_memory_access access;
  switch (protection) {
    case MEM_PROT_NONE: access = JS_HORIZON_MEMORY_NONE; break;
    case MEM_PROT_READ: access = JS_HORIZON_MEMORY_READ; break;
    case MEM_PROT_RW: access = JS_HORIZON_MEMORY_READ_WRITE; break;
    default: errno = ENOTSUP; return false;
  }
  uint32_t raw = 0;
  auto status = js_horizon_memory_protect(addr, bytes, access, &raw);
  return memory_result(status, raw, "protect");
}

bool os::guard_memory(char* addr, size_t bytes) { return protect_memory(addr, bytes, MEM_PROT_NONE); }
bool os::unguard_memory(char* addr, size_t bytes) { return protect_memory(addr, bytes, MEM_PROT_RW); }
bool os::pd_create_stack_guard_pages(char*, size_t) { errno = ENOTSUP; return false; }

bool os::committed_in_range(address start, size_t size, address& found, size_t& found_size) {
  void* result = nullptr; uint32_t raw = 0;
  auto status = js_horizon_memory_committed_in_range(start, size, &result, &found_size, &raw);
  found = static_cast<address>(result);
  if (status == JS_HORIZON_MEMORY_STATE) return false; // A successfully queried hole.
  return memory_result(status, raw, "committed-range query");
}

size_t os::pd_pretouch_memory(void*, void*, size_t page_size) {
  // Return the real page size so shared os::pretouch_memory performs atomic
  // stores. This does not claim a platform pretouch operation was performed.
  return page_size;
}

void os::pd_realign_memory(char*, size_t, size_t) {
  // Optional huge-page promotion hint: this target has only 4 KiB pages.
}
void os::pd_disclaim_memory(char*, size_t) {
  // Optional reclaim-on-touch hint is unavailable. Keep valid committed bytes;
  // decommitting would violate the caller's no-explicit-recommit contract.
}

void os::large_page_init() {
  if (UseLargePages) {
    warning("Horizon large pages are unavailable; using 4 KiB physical pages");
    FLAG_SET_DEFAULT(UseLargePages, false);
  }
}
size_t os::large_page_size() { return 0; }
bool os::can_commit_large_page_memory() { return false; }
char* os::pd_reserve_memory_special(size_t, size_t, size_t, char*, bool) { errno = ENOTSUP; return nullptr; }
bool os::pd_release_memory_special(char*, size_t) { errno = ENOTSUP; return false; }
bool os::can_trim_native_heap() { return false; }
bool os::trim_native_heap(size_change_t*) { errno = ENOTSUP; return false; }

// File-backed shared mappings and executable memory are separate services.
// The current bridge must not pretend anonymous physical pages are MAP_SHARED.
char* os::pd_map_memory(int, const char*, size_t, char*, size_t, bool, bool) { errno = ENOTSUP; return nullptr; }
bool os::pd_unmap_memory(char* addr, size_t bytes) { return pd_release_memory(addr, bytes); }
char* os::map_memory_to_file(char*, size_t, int) { errno = ENOTSUP; return nullptr; }
char* os::map_memory_to_file_aligned(size_t, size_t, int, MemTag) { errno = ENOTSUP; return nullptr; }
char* os::pd_attempt_map_memory_to_file_at(char*, size_t, int) { errno = ENOTSUP; return nullptr; }
int os::create_file_for_heap(const char*) { errno = ENOTSUP; return -1; }
