/* SPDX-License-Identifier: GPL-2.0-only */
// Standalone diagnostic for real port components, not a JNI VM implementation.
#include "runtime/os.hpp"
#include "semaphore_horizon.hpp"
#include "horizon_bridge.h"
#include "mutex_horizon.hpp"
#include "park_horizon.hpp"
#include "safefetch_horizon.hpp"
#include "runtime/atomic.hpp"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

extern "C" void sc_log(const char*, ...);
extern "C" void js_zero_probe_error(const char*, va_list);
[[noreturn]] void report_vm_error(const char* file, int line, const char* error,
                                 const char* detail, ...) {
  sc_log("HotSpot service diagnostic fatal: %s:%d: %s\n", file, line, error);
  va_list args; va_start(args, detail); js_zero_probe_error(detail, args); va_end(args);
  abort();
}

extern "C" int js_zero_services_probe() {
  sc_log("Genuine OpenJDK25 os:: timing and HorizonSemaphore primitives\n");
  sc_log("Counter frequency=%llu Hz\n", (unsigned long long)js_horizon_clock_frequency());
  jlong first = os::javaTimeNanos(), last = first;
  int monotonic = 1;
  for (unsigned i = 0; i < 10000; i++) {
    jlong current = os::javaTimeNanos();
    if (current < last) monotonic = 0;
    last = current;
  }
  sc_log("os::javaTimeNanos samples=10000 span=%lld ns monotonic=%s\n",
         (long long)(last - first), monotonic ? "PASS" : "FAIL");
  sc_log("os::javaTimeMillis=%lld (Horizon/newlib wall clock)\n", (long long)os::javaTimeMillis());
  HorizonSemaphore semaphore(0);
  int counts = !semaphore.trywait();
  semaphore.signal(2);
  counts &= semaphore.trywait();
  counts &= semaphore.trywait();
  counts &= !semaphore.trywait();
  semaphore.signal();
  semaphore.wait();
  counts &= !semaphore.trywait();
  sc_log("HorizonSemaphore token accounting=%s; contention not exercised\n", counts ? "PASS" : "FAIL");
  volatile uint64_t value = 0;
  Atomic::store(&value, UINT64_C(20));
  int atomics = Atomic::add(&value, UINT64_C(22)) == 42;
  atomics &= Atomic::cmpxchg(&value, UINT64_C(42), UINT64_C(69)) == 42;
  atomics &= Atomic::load(&value) == 69;
  sc_log("HotSpot Atomic 64-bit add/CAS/load=%s; contention not exercised\n", atomics ? "PASS" : "FAIL");
  sc_log("No Java VM startup, classfile execution, JNI, or Java FFM was tested.\n");
  PlatformMonitor monitor;
  int locks = monitor.try_lock();
  if (locks) {
    locks &= !monitor.try_lock();
    int result = monitor.wait(2);
    sc_log("PlatformMonitor wait(2ms)=%d expected timeout=%d; mutex reacquired\n", result, OS_TIMEOUT);
    locks &= result == OS_TIMEOUT;
    locks &= !monitor.try_lock();
    monitor.notify(); monitor.notify_all(); monitor.unlock();
  }
  sc_log("PlatformMutex/Monitor accounting/timeout=%s; contention not exercised\n", locks ? "PASS" : "FAIL");
  alignas(PlatformEvent) static unsigned char event_storage[sizeof(PlatformEvent)];
  static auto* event = ::new (event_storage) PlatformEvent;
  event->unpark(); event->unpark();
  int permits = event->park(1) == OS_OK;
  permits &= event->park_nanos(1000000) == OS_TIMEOUT;
  sc_log("PlatformEvent coalesced permits and real timed wait=%s\n", permits ? "PASS" : "FAIL");
  return monotonic && counts && atomics && locks && permits;
}

extern "C" void js_zero_fault_probe() {
  int narrow = 0x12345678; intptr_t wide = static_cast<intptr_t>(UINT64_C(0x123456789abcdef0));
  int passed = SafeFetch32_impl(&narrow, -12345) == narrow && SafeFetchN_impl(&wide, -67890) == wide;
  sc_log("SafeFetch valid 32/64-bit loads=%s\n", passed ? "PASS" : "FAIL");
  sc_log("Attempt SafeFetch32 at unreadable address 0; recovery is hardware NOT TESTED\n");
  int result32 = SafeFetch32_impl(nullptr, -12345);
  sc_log("SafeFetch32 fault result=%d expected=-12345\n", result32);
  sc_log("Attempt SafeFetchN at unreadable address 0\n");
  intptr_t resultN = SafeFetchN_impl(nullptr, -67890);
  sc_log("SafeFetchN fault result=%lld expected=-67890\n", (long long)resultN);
  passed &= result32 == -12345 && resultN == -67890;
  sc_log("SafeFetch fault-island recovery=%s; no general Java exception/VM claim\n", passed ? "PASS" : "FAIL");
}
