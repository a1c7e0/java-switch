/* SPDX-License-Identifier: GPL-2.0-only */
// Standalone native thread/permit probe; no JavaThread or JNI VM is created.
#include "runtime/os.hpp"
#include "runtime/atomic.hpp"
#include "park_horizon.hpp"
#include "horizon_thread_bridge.h"
#include <new>
extern "C" void sc_log(const char*, ...);
struct NativeProbeState {
  HorizonParkPermit ping, pong;
  js_horizon_thread* handle = nullptr;
  bool started = false, joined = false;
  volatile int stop = 0, completed = 0, stack_valid = 0;
  volatile uint64_t child_id = 0;
  volatile uint32_t stack_result = 0, id_result = 0;
};
static void worker(void* opaque) {
  auto* state = static_cast<NativeProbeState*>(opaque);
  uintptr_t low = 0, high = 0; uint32_t raw = 0; int marker;
  auto status = js_horizon_thread_current_stack_bounds(&low, &high, &raw);
  Atomic::store(&state->stack_result, raw);
  Atomic::store(&state->stack_valid, status == JS_HORIZON_THREAD_OK &&
      reinterpret_cast<uintptr_t>(&marker) >= low && reinterpret_cast<uintptr_t>(&marker) < high ? 1 : 0);
  uint64_t id = 0;
  status = js_horizon_thread_current_id(&id, &raw);
  Atomic::store(&state->id_result, raw);
  if (status == JS_HORIZON_THREAD_OK) Atomic::store(&state->child_id, id);
  for (int i = 0; i < 64; i++) {
    if (state->ping.park(1000000000) != OS_OK || Atomic::load(&state->stop)) return;
    Atomic::add(&state->completed, 1); state->pong.unpark();
  }
}
static bool cleanup(NativeProbeState* state, uint64_t timeout) {
  uint32_t raw = 0;
  if (state->started && !state->joined) {
    auto status = js_horizon_thread_join(state->handle, timeout, &raw);
    sc_log("native join status=%d Result=0x%x\n", status, raw);
    if (status != JS_HORIZON_THREAD_OK) return false;
    state->joined = true;
  }
  auto status = js_horizon_thread_close(state->handle, &raw);
  sc_log("native close status=%d Result=0x%x (libnx cannot report svcCloseHandle Result)\n", status, raw);
  if (status != JS_HORIZON_THREAD_OK) return false;
  state->handle = nullptr; state->started = state->joined = false;
  return true;
}
extern "C" void js_zero_thread_probe() {
  // Retain callback context and handles across failed joins/closes. Never let
  // a worker reference a returned stack frame or an object destructed at exit.
  alignas(NativeProbeState) static unsigned char storage[sizeof(NativeProbeState)];
  static auto* state = ::new (storage) NativeProbeState;
  if (state->handle != nullptr) {
    sc_log("Retry pending native thread cleanup before another probe\n");
    cleanup(state, 0); return;
  }
  state->ping.reset(); state->pong.reset();
  Atomic::store(&state->stop, 0); Atomic::store(&state->completed, 0);
  Atomic::store(&state->stack_valid, 0); Atomic::store(&state->child_id, UINT64_C(0));
  uint64_t parent_id = 0; uint32_t raw = 0;
  auto status = js_horizon_thread_current_id(&parent_id, &raw);
  sc_log("native parent identity status=%d Result=0x%x id=%llu\n", status, raw, (unsigned long long)parent_id);
  if (status != JS_HORIZON_THREAD_OK) return;
  status = js_horizon_thread_create(worker, state, 0x20000, &state->handle, &raw);
  sc_log("native create stack=131072 status=%d Result=0x%x\n", status, raw);
  if (status != JS_HORIZON_THREAD_OK) return;
  status = js_horizon_thread_start(state->handle, &raw);
  sc_log("native start status=%d Result=0x%x\n", status, raw);
  if (status != JS_HORIZON_THREAD_OK) { cleanup(state, 0); return; }
  state->started = true;
  bool passed = true;
  for (int i = 0; i < 64; i++) {
    state->ping.unpark();
    if (state->pong.park(1000000000) != OS_OK) { passed = false; break; }
  }
  Atomic::store(&state->stop, 1); state->ping.unpark();
  if (!cleanup(state, 2000000000)) {
    sc_log("Native context retained; X retries cleanup; no successful probe claimed\n"); return;
  }
  const uint64_t child = Atomic::load(&state->child_id);
  passed &= Atomic::load(&state->completed) == 64 && Atomic::load(&state->stack_valid) &&
            child != 0 && child != parent_id;
  sc_log("native child id=%llu stack Result=0x%x identity Result=0x%x completed=%d\n",
      (unsigned long long)child, Atomic::load(&state->stack_result), Atomic::load(&state->id_result),
      Atomic::load(&state->completed));
  sc_log("native thread / stack bounds / 64 permit handoffs: %s; no VM/JNI test\n", passed ? "PASS" : "FAIL");
}
