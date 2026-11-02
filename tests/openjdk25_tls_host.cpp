/* SPDX-License-Identifier: GPL-2.0-only */
// Uses opaque identities, not constructed VM Thread/JavaThread instances.
#include "runtime/os.hpp"
#include "runtime/threadLocalStorage.hpp"
#include "horizon_bridge.h"
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern "C" void js_mock_set(unsigned, unsigned);
extern "C" void js_mock_tls_exit(void);
[[noreturn]] void report_vm_error(const char*, int, const char* error, const char* format, ...) {
  fprintf(stderr, "%s: ", error); va_list args; va_start(args, format);
  vfprintf(stderr, format, args); va_end(args); abort();
}
static void check(bool ok) { if (!ok) abort(); }
static THREAD_LOCAL uintptr_t compiler_value = 0;
static pthread_barrier_t barrier;
static void* worker(void* opaque) {
  auto value = reinterpret_cast<uintptr_t>(opaque);
  check(compiler_value == 0 && ThreadLocalStorage::thread() == nullptr);
  compiler_value = value;
  ThreadLocalStorage::set_thread(reinterpret_cast<Thread*>(opaque));
  pthread_barrier_wait(&barrier);
  for (unsigned i = 0; i < 10000; i++) {
    check(compiler_value == value && ThreadLocalStorage::thread() == reinterpret_cast<Thread*>(opaque));
  }
  // Native-exit restoration preserves association for later detach callbacks.
  js_mock_tls_exit();
  check(ThreadLocalStorage::thread() == reinterpret_cast<Thread*>(opaque));
  ThreadLocalStorage::set_thread(nullptr);
  check(ThreadLocalStorage::thread() == nullptr);
  return nullptr;
}
int main(int argc, char** argv) {
  check(!ThreadLocalStorage::is_initialized());
  if (argc == 2 && !strcmp(argv[1], "uninitialized")) ThreadLocalStorage::thread();
  if (argc == 2 && !strcmp(argv[1], "allocation-failure")) js_mock_set(4, 1);
  ThreadLocalStorage::init();
  if (argc == 2 && !strcmp(argv[1], "double-init")) ThreadLocalStorage::init();
  check(ThreadLocalStorage::is_initialized() && ThreadLocalStorage::thread() == nullptr);
  int parent;
  ThreadLocalStorage::set_thread(reinterpret_cast<Thread*>(&parent)); compiler_value = 123;
  check(pthread_barrier_init(&barrier, nullptr, 5) == 0);
  pthread_t threads[4]; int markers[4];
  for (unsigned i = 0; i < 4; i++) check(pthread_create(&threads[i], nullptr, worker, &markers[i]) == 0);
  pthread_barrier_wait(&barrier);
  for (auto thread : threads) check(pthread_join(thread, nullptr) == 0);
  check(compiler_value == 123 && ThreadLocalStorage::thread() == reinterpret_cast<Thread*>(&parent));
  ThreadLocalStorage::set_thread(nullptr);
  check(ThreadLocalStorage::thread() == nullptr);
  pthread_barrier_destroy(&barrier);
  puts("HOST-PASSED: real Horizon ThreadLocalStorage + compiler TLS; four isolated workers, restore/clear; libnx TLS mocked");
}
