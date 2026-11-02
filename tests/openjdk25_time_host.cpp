/* SPDX-License-Identifier: GPL-2.0-only */
// Host-only deterministic inputs and fatal error sink for the real time port.
#include "runtime/os.hpp"
#include "horizon_bridge.h"
#include "jvmti.h"
#include "runtime/atomic.hpp"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <initializer_list>

static uint64_t ticks, frequency = 19200000;
static int clock_failure;
extern "C" uint64_t js_horizon_clock_ticks() { return ticks; }
extern "C" uint64_t js_horizon_clock_frequency() { return frequency; }
extern "C" int __wrap_gettimeofday(timeval* now, void*) {
  if (clock_failure) { errno = EIO; return -1; }
  now->tv_sec = 1700000000; now->tv_usec = 123456; return 0;
}
[[noreturn]] void report_vm_error(const char*, int, const char* error, const char* detail, ...) {
  fprintf(stderr, "%s: ", error);
  va_list args; va_start(args, detail); vfprintf(stderr, detail, args); va_end(args);
  abort();
}
static void check(bool ok) { if (!ok) abort(); }
static volatile uint64_t counter;
static void* increment(void*) {
  for (int i = 0; i < 20000; i++) Atomic::add(&counter, UINT64_C(1));
  return nullptr;
}
int main(int argc, char** argv) {
  if (argc > 1 && !strcmp(argv[1], "zero-frequency")) { frequency = 0; os::javaTimeNanos(); return 1; }
  if (argc > 1 && !strcmp(argv[1], "clock-error")) { clock_failure = 1; os::javaTimeMillis(); return 1; }
  for (uint64_t rate : {UINT64_C(19200000), UINT64_C(1000000000), UINT64_C(7)}) {
    frequency = rate;
    for (uint64_t value : {UINT64_C(0), UINT64_C(1), UINT64_C(19199999),
                          UINT64_C(19200000), UINT64_C(0x7fffffffffffffff), UINT64_MAX}) {
      ticks = value;
      uint64_t expected = (unsigned __int128)value * 1000000000 / rate;
      check((uint64_t)os::javaTimeNanos() == expected);
    }
  }
  jlong seconds, nanos; os::javaTimeSystemUTC(seconds, nanos);
  check(seconds == 1700000000 && nanos == 123456000);
  check(os::javaTimeMillis() == INT64_C(1700000000123));
  jvmtiTimerInfo info; os::javaTimeNanos_info(&info);
  check(info.max_value == all_bits_jlong && !info.may_skip_backward &&
        !info.may_skip_forward && info.kind == JVMTI_TIMER_ELAPSED);
  pthread_t workers[4];
  for (auto& worker : workers) check(!pthread_create(&worker, nullptr, increment, nullptr));
  for (auto& worker : workers) check(!pthread_join(worker, nullptr));
  check(Atomic::load(&counter) == 80000);
  check(Atomic::cmpxchg(&counter, UINT64_C(80000), UINT64_C(69)) == 80000);
  check(Atomic::load(&counter) == 69);
  puts("real os:: Horizon time conversion, overflow and metadata: HOST-PASSED (mocked clocks)");
  puts("HotSpot Atomic add/CAS with four host threads: HOST-PASSED (not AArch64 execution)");
  return 0;
}
