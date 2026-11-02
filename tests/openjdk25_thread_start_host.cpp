/* SPDX-License-Identifier: GPL-2.0-only */
#include "runtime/os.hpp"
#include "thread_start_horizon.hpp"
#include <atomic>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
[[noreturn]] void report_vm_error(const char*, int, const char* error, const char* format, ...) {
  fprintf(stderr, "%s: ", error); va_list args; va_start(args, format);
  vfprintf(stderr, format, args); va_end(args); abort();
}
static void check(bool ok) { if (!ok) abort(); }
struct State { HorizonThreadStartup gate; std::atomic<int> phase{0}; };
static void* worker(void* opaque) {
  auto* state = static_cast<State*>(opaque);
  state->phase.store(1);
  state->gate.report_and_wait(JS_HORIZON_THREAD_OK, 0);
  state->phase.store(2);
  return nullptr;
}
int main(int argc, char** argv) {
  if (argc == 2 && !strcmp(argv[1], "early-start")) { State state; state.gate.release(); return 1; }
  if (argc == 2 && !strcmp(argv[1], "double-report")) {
    State state; state.gate.report_and_wait(JS_HORIZON_THREAD_SYSTEM, 0xf601);
    state.gate.report_and_wait(JS_HORIZON_THREAD_OK, 0); return 1;
  }
  for (unsigned iteration = 0; iteration < 100; iteration++) {
    State states[4]; pthread_t threads[4];
    for (unsigned i = 0; i < 4; i++) check(pthread_create(&threads[i], nullptr, worker, &states[i]) == 0);
    for (auto& state : states) {
      uint32_t raw = 1;
      check(state.gate.wait_until_ready(&raw) == JS_HORIZON_THREAD_OK && raw == 0);
      check(state.phase.load() == 1); // Child cannot execute the published body yet.
    }
    for (int i = 3; i >= 0; i--) states[i].gate.release();
    if (argc == 2 && !strcmp(argv[1], "double-start")) states[0].gate.release();
    for (unsigned i = 0; i < 4; i++) { check(pthread_join(threads[i], nullptr) == 0); check(states[i].phase.load() == 2); }
  }
  State failed;
  failed.gate.report_and_wait(JS_HORIZON_THREAD_SYSTEM, 0xf601);
  uint32_t raw = 0;
  check(failed.gate.wait_until_ready(&raw) == JS_HORIZON_THREAD_SYSTEM && raw == 0xf601);
  puts("HOST-PASSED: actual Horizon startup gate, 400 ordered workers, failed readiness/raw Result, publication before release");
}
