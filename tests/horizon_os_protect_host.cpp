/* SPDX-License-Identifier: GPL-2.0-only */
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

/* Reuse the real C bridge host fixture's libnx, Alias, and mprotect mocks. */
#define main bridge_fixture_main
extern "C" {
#include "horizon_memory_bridge_host.c"
}
#undef main

#include "runtime/os.hpp"
#include "horizon_memory_bridge.h"

bool UseLargePages = false;
void vm_exit_out_of_memory(size_t, int, const char*, ...) {
  assert(!"unexpected VM exit in memory protection regression");
}
void warning(const char*, ...) {}

static void assert_write_faults(void* address) {
  pid_t child = fork();
  assert(child >= 0);
  if (child == 0) {
    *(volatile unsigned char*)address = 0xa5;
    _exit(0);
  }
  int status = 0;
  assert(waitpid(child, &status, 0) == child);
  assert(WIFSIGNALED(status) &&
         (WTERMSIG(status) == SIGSEGV || WTERMSIG(status) == SIGBUS));
}

int main() {
  system_resource_total = 0;
  void* base = nullptr;
  uint32_t raw = 0;
  assert(js_horizon_memory_reserve(4 * 4096, 65536, &base, &raw) ==
         JS_HORIZON_MEMORY_OK);

  /* Exercise the maintained HotSpot OS wrapper and its committed=false
   * argument, then observe the real bridge-backed host page protection. */
  assert(os::protect_memory(static_cast<char*>(base), 4096, MEM_PROT_NONE,
                            false));
  assert(protect_calls == 2);  // initial eager NONE plus the wrapper's call
  assert_write_faults(base);

  void* committed = reinterpret_cast<void*>(1);
  size_t committed_size = 99;
  assert(js_horizon_memory_committed_in_range(base, 4 * 4096, &committed,
      &committed_size, &raw) == JS_HORIZON_MEMORY_STATE);
  assert(committed == nullptr && committed_size == 0);

  errno = 0;
  assert(!os::protect_memory(static_cast<char*>(base), 4096, MEM_PROT_READ,
                             false));
  assert(errno == ENOTSUP);

  assert(os::pd_commit_memory(static_cast<char*>(base), 4096, false));
  *(volatile unsigned char*)base = 0x5a;
  committed = nullptr;
  committed_size = 0;
  assert(js_horizon_memory_committed_in_range(base, 4 * 4096, &committed,
      &committed_size, &raw) == JS_HORIZON_MEMORY_OK);
  assert(committed == base && committed_size == 4096);
  assert(os::pd_release_memory(static_cast<char*>(base), 4 * 4096));
  assert(!virtmem_locked);
  return 0;
}
