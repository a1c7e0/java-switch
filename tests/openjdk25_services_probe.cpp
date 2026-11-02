/* SPDX-License-Identifier: GPL-2.0-only */
// Standalone diagnostic for real port components, not a JNI VM implementation.
#include "runtime/os.hpp"
#include "semaphore_horizon.hpp"
#include "horizon_bridge.h"
#include "runtime/atomic.hpp"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

extern "C" void sc_log(const char*, ...);
[[noreturn]] void report_vm_error(const char* file, int line, const char* error,
                                 const char* detail, ...) {
  sc_log("HotSpot service diagnostic fatal: %s:%d: %s\n", file, line, error);
  va_list args; va_start(args, detail); vfprintf(stderr, detail, args); va_end(args);
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
  return monotonic && counts && atomics;
}
