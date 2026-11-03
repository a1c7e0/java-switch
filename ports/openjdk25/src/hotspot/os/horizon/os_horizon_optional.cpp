/* SPDX-License-Identifier: GPL-2.0-only */
#include "runtime/os.hpp"
#include "runtime/globals.hpp"
#include "runtime/thread.hpp"
#include "utilities/ostream.hpp"
#include "jvm_io.h"
#include "jvm.h"
#include "horizon_thread_bridge.h"
#include <errno.h>
#include <stdio.h>

// Native priority changes are disabled in init_2 and explicitly fail in the
// system backend. These logical priorities retain libnx's default native level.
static_assert(CriticalPriority == 11, "review native priority table");
int os::java_to_os_priority[CriticalPriority + 1] = {
  -1, 0x2c, 0x2c, 0x2c, 0x2c, 0x2c, 0x2c, 0x2c, 0x2c, 0x2c, 0x2c, 0x2c
};

bool os::getTimesSecs(double* real, double* user, double* system) {
  if (real) *real = 0;
  if (user) *user = 0;
  if (system) *system = 0;
  errno = ENOTSUP;
  return false; // No invented process CPU accounting.
}
jlong os::thread_cpu_time(Thread*) { return -1; }
jlong os::thread_cpu_time(Thread*, bool) { return -1; }

int os::get_signal_number(const char*) { errno = ENOTSUP; return -1; }
int os::sigexitnum_pd() { return -1; }
bool os::signal_sent_by_kill(const void*) { return false; } // No Unix kill-origin signals.
bool os::signal_thread(Thread*, int, const char*) { errno = ENOTSUP; return false; }
void os::signal_notify(int) {
  guarantee(false, "Horizon process signals unavailable; ReduceSignalUsage must remain enabled");
}
int os::signal_wait() {
  guarantee(false, "Horizon has no POSIX signal dispatcher");
  return -1;
}

bool os::start_debugging(char* buffer, int length) {
  if (buffer != nullptr && length > 0)
    os::snprintf(buffer, size_t(length), "Horizon VM debugger attachment is unavailable");
  errno = ENOTSUP; return false;
}
void os::wait_for_keypress_at_exit() {
  jio_fprintf(stderr, "Waiting for a character on stdin; homebrew hosts may not provide keyboard input\n");
  if (::getchar() == EOF) jio_fprintf(stderr, "Horizon stdin returned EOF; no keypress was observed\n");
}
void os::run_periodic_checks(outputStream* stream) {
  uint32_t raw = 0, pending = 0;
  js_horizon_thread_reaper_status(&raw, &pending);
  if (raw != 0 && stream != nullptr)
    stream->print_cr("Horizon native reaper: pending=%u last Result=0x%x", pending, raw);
}

// Signal.java treats these error results as unsupported registration/raising.
// No handler is installed and no process signal is reported as delivered.
extern "C" JNIEXPORT void* JNICALL JVM_RegisterSignal(jint, void*) {
  errno = ENOTSUP;
  return reinterpret_cast<void*>(intptr_t(-1));
}
extern "C" JNIEXPORT jboolean JNICALL JVM_RaiseSignal(jint) {
  errno = ENOTSUP;
  return JNI_FALSE;
}
