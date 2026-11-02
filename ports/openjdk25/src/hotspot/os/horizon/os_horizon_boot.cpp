/* SPDX-License-Identifier: GPL-2.0-only */
#include "runtime/os.hpp"
#include "runtime/osInfo.hpp"
#include "runtime/arguments.hpp"
#include "runtime/globals.hpp"
#include "runtime/perfMemory.hpp"
#include "jvm_io.h"
#include "utilities/defaultStream.hpp"
#include "utilities/permitForbiddenFunctions.hpp"
#include "utilities/vmError.hpp"
#include "horizon_system_bridge.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

extern "C" void js_horizon_os_elapsed_time_init();
extern "C" int __system_argc;
extern "C" char** __system_argv;

void os::init() {
  OSInfo::set_vm_page_size(4096);
  OSInfo::set_vm_allocation_granularity(4096);
  _page_sizes.add(4096);
  js_horizon_os_elapsed_time_init();
  uint64_t mask = 0, total = 0, used = 0; uint32_t raw = 0;
  auto status = js_horizon_system_core_mask(&mask, &raw);
  guarantee(status == JS_HORIZON_SYSTEM_OK && mask != 0,
            "Horizon CPU mask query failed: status=%d Result=0x%x", status, raw);
  int count = 0;
  while (mask != 0) { count += int(mask & 1); mask >>= 1; }
  set_processor_count(count);
  status = js_horizon_system_memory(&total, &used, &raw);
  guarantee(status == JS_HORIZON_SYSTEM_OK && total != 0,
            "Horizon process memory query failed: status=%d Result=0x%x", status, raw);
  setup_fpu();
}

jint os::init_2() {
  // This configuration does not have POSIX process signals, shared PerfData,
  // or native priority changes. Reject explicit incompatible requests.
  if ((!ReduceSignalUsage && !FLAG_IS_DEFAULT(ReduceSignalUsage)) ||
      (!PerfDisableSharedMem && !FLAG_IS_DEFAULT(PerfDisableSharedMem)) ||
      (UseThreadPriorities && !FLAG_IS_DEFAULT(UseThreadPriorities)) || UseNUMA) {
    jio_fprintf(stderr, "Horizon requires reduced signal usage, private PerfData, no native priorities/NUMA\n");
    return JNI_ERR;
  }
  FLAG_SET_DEFAULT(ReduceSignalUsage, true);
  FLAG_SET_DEFAULT(PerfDisableSharedMem, true);
  FLAG_SET_DEFAULT(UseThreadPriorities, false);
  // Shared os::init_before_ergo has already initialized the initial CPU count.
  if (set_minimum_stack_sizes() != JNI_OK) return JNI_ERR;
  // The directory policy is explicit; creation may fail, and actual file
  // operations continue to report the filesystem's errno rather than success.
  const char* tmp = get_temp_directory();
  if (::mkdir(tmp, 0777) != 0 && errno != EEXIST)
    warning("Horizon temporary directory is unavailable: %s (errno=%d)", tmp, errno);
  return JNI_OK;
}

void os::init_system_properties_values() {
  const char* home = ::getenv("JAVA_HOME");
  if (home == nullptr || *home == '\0') home = "sdmc:/switch/mc-runtime/runtimes/openjdk25";
  Arguments::set_java_home(home);
  char libraries[4096];
  int count = os::snprintf(libraries, sizeof(libraries), "%s/lib", home);
  if (count < 0 || size_t(count) >= sizeof(libraries))
    vm_exit_during_initialization("Horizon JAVA_HOME is too long");
  Arguments::set_dll_dir(libraries);
  Arguments::set_library_path(libraries);
  Arguments::set_ext_dirs(libraries);
  if (!set_boot_path('/', ';'))
    vm_exit_during_initialization("Missing Horizon Java25 runtime image", home);
}

const char* os::get_temp_directory() {
  const char* configured = ::getenv("TMPDIR");
  return configured != nullptr && *configured != '\0' ? configured : "sdmc:/switch/mc-runtime/tmp";
}

void os::jvm_path(char* buffer, jint length) {
  if (buffer == nullptr || length <= 0) return;
  // libjvm is statically linked; report the actual executable, never invent a
  // libjvm.so pathname. Image discovery uses the explicit JAVA_HOME policy.
  const char* path = __system_argc > 0 && __system_argv != nullptr ? __system_argv[0] : nullptr;
  os::snprintf(buffer, size_t(length), "%s", path != nullptr ? path : "");
}

bool os::have_special_privileges() { return false; } // Horizon has no Unix setuid mode.
void os::setup_fpu() { __asm__ volatile("msr fpcr, xzr\nmsr fpsr, xzr" ::: "memory"); }
void os::os_exception_wrapper(java_call_t function, JavaValue* result,
                             const methodHandle& method, JavaCallArguments* args, JavaThread* thread) {
  // Zero's managed exceptions use VM state; native faults use the libnx entry.
  // This wrapper claims no SEH/POSIX recovery around arbitrary native calls.
  function(result, method, args, thread);
}

void os::exit(int code) { permit_forbidden_function::exit(code); }
void os::_exit(int code) { permit_forbidden_function::_exit(code); }
void os::shutdown() {
  perfMemory_exit();
  ostream_abort();
  if (Arguments::abort_hook() != nullptr) Arguments::abort_hook()();
}
void os::abort(bool dump_core, const void*, const void*) {
  if (dump_core) jio_fprintf(stderr, "Horizon core dumps unavailable; preserve SD logs and Atmosphere crash report\n");
  shutdown();
  os::_exit(1);
}
void os::die() { permit_forbidden_function::_exit(1); }
void os::breakpoint() { __builtin_trap(); }
void os::check_core_dump_prerequisites(char* buffer, size_t size, bool check_only) {
  os::snprintf(buffer, size, "Horizon does not generate POSIX core files; preserve SD/crash logs");
  if (!check_only) VMError::record_coredump_status(buffer, false);
}

int os::fork_and_exec(char const*) { errno = ENOTSUP; return -1; }
int os::loadavg(double*, int) { errno = ENOTSUP; return -1; }
bool os::get_host_name(char* buffer, size_t size) {
  if (buffer != nullptr && size != 0) buffer[0] = '\0';
  errno = ENOTSUP; return false;
}
size_t os::numa_get_groups_num() { return 1; } // One address domain; UseNUMA is rejected.
void os::set_native_thread_name(const char*) {
  // libnx exposes no kernel thread-name setter; HotSpot retains Thread::name.
}
