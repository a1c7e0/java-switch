/* SPDX-License-Identifier: GPL-2.0-only */
// Actual libnx ownership/reaping diagnostic, no HotSpot Thread or JVM instance.
#include "runtime/os.hpp"
#include "runtime/atomic.hpp"
#include "horizon_thread_bridge.h"
#include "park_horizon.hpp"
#include <new>
extern "C" void sc_log(const char*, ...);

struct ReaperProbeState {
  HorizonParkPermit ready, release;
  js_horizon_thread* handle = nullptr;
  volatile int phase = 0; // 0=idle, 1=worker owns handle, 2=retired, 3=transfer failed
  volatile int transfer_status = JS_HORIZON_THREAD_STATE;
  volatile uint32_t transfer_raw = 0;
  bool observed_pending = false;
};

static void reaper_probe_worker(void* context) {
  auto* state = static_cast<ReaperProbeState*>(context);
  auto* handle = state->handle;
  // Once start succeeded, this worker exclusively owns the transfer decision.
  // The UI never touches the handle while phase == 1.
  uint32_t raw = 0;
  auto status = js_horizon_thread_retire(handle, &raw);
  Atomic::store(&state->transfer_status, static_cast<int>(status));
  Atomic::store(&state->transfer_raw, raw);
  if (status == JS_HORIZON_THREAD_OK) {
    state->handle = nullptr; // Only this worker can touch it before ready.
    Atomic::store(&state->phase, 2);
  } else {
    Atomic::store(&state->phase, 3); // Caller receives ownership back after ready.
  }
  state->ready.unpark();
  // Keep the native stack alive until the UI has observed the pending item.
  // The actual reaper must join, even though ownership has already moved.
  state->release.park(5000000000);
}

static bool finish_reaper_probe(ReaperProbeState* state) {
  int phase = Atomic::load(&state->phase);
  if (phase == 1 && state->ready.park(2000000000) != OS_OK) {
    sc_log("Reaper probe worker has not reported; context retained, B retries\n");
    return false;
  }
  phase = Atomic::load(&state->phase);
  if (phase == 3) {
    sc_log("Worker retire failed: status=%d Result=0x%x; recovering caller ownership\n",
           Atomic::load(&state->transfer_status), Atomic::load(&state->transfer_raw));
    state->release.unpark();
    uint32_t raw = 0;
    auto status = js_horizon_thread_join(state->handle, 2000000000, &raw);
    if (status != JS_HORIZON_THREAD_OK) {
      sc_log("Failed-retire worker join: status=%d Result=0x%x; retained\n", status, raw);
      return false;
    }
    status = js_horizon_thread_close(state->handle, &raw);
    if (status != JS_HORIZON_THREAD_OK) {
      sc_log("Failed-retire close: status=%d Result=0x%x; retained\n", status, raw);
      // A joined handle cannot be joined a second time. Phase 4 retries close.
      Atomic::store(&state->phase, 4); return false;
    }
    state->handle = nullptr; Atomic::store(&state->phase, 0); return false;
  }
  if (phase == 4) {
    uint32_t raw = 0;
    auto status = js_horizon_thread_close(state->handle, &raw);
    sc_log("Retry retained close: status=%d Result=0x%x\n", status, raw);
    if (status == JS_HORIZON_THREAD_OK) { state->handle = nullptr; Atomic::store(&state->phase, 0); }
    return false;
  }
  uint32_t raw = 0, pending = 0;
  js_horizon_thread_reaper_status(&raw, &pending);
  sc_log("Retired worker still alive: queued/inflight=%u async Result=0x%x\n", pending, raw);
  state->observed_pending |= pending != 0;
  state->release.unpark();
  const uint64_t start = static_cast<uint64_t>(os::javaTimeNanos());
  HorizonParkPermit delay;
  do {
    js_horizon_thread_reaper_status(&raw, &pending);
    if (pending == 0) break;
    delay.park(10000000);
  } while (static_cast<uint64_t>(os::javaTimeNanos()) - start < UINT64_C(2500000000));
  sc_log("Deferred join/close: pending=%u async Result=0x%x; %s\n", pending, raw,
         state->observed_pending && pending == 0 && raw == 0 ? "PASS" : "FAIL");
  if (pending == 0) Atomic::store(&state->phase, 0);
  else sc_log("B retries pending reaper cleanup before a new worker\n");
  return pending == 0;
}

extern "C" void js_zero_reaper_probe() {
  alignas(ReaperProbeState) static unsigned char storage[sizeof(ReaperProbeState)];
  static auto* state = ::new (storage) ReaperProbeState;
  if (Atomic::load(&state->phase) != 0) { finish_reaper_probe(state); return; }
  uint32_t raw = 0;
  auto status = js_horizon_thread_ensure_reaper_started(&raw);
  sc_log("Actual process-lifetime native reaper startup: status=%d Result=0x%x\n", status, raw);
  if (status != JS_HORIZON_THREAD_OK) return;
  state->ready.reset(); state->release.reset();
  state->observed_pending = false;
  status = js_horizon_thread_create(reaper_probe_worker, state, 0x20000, &state->handle, &raw);
  if (status != JS_HORIZON_THREAD_OK) {
    sc_log("Retire worker create: status=%d Result=0x%x\n", status, raw); return;
  }
  Atomic::store(&state->phase, 1);
  status = js_horizon_thread_start(state->handle, &raw);
  if (status != JS_HORIZON_THREAD_OK) {
    // No callback ran. Native bridge retains an unstarted handle.
    sc_log("Retire worker start: status=%d Result=0x%x\n", status, raw);
    auto closed = js_horizon_thread_close(state->handle, &raw);
    if (closed == JS_HORIZON_THREAD_OK) { state->handle = nullptr; Atomic::store(&state->phase, 0); }
    else { Atomic::store(&state->phase, 4); sc_log("Unstarted close Result=0x%x; retained\n", raw); }
    return;
  }
  finish_reaper_probe(state);
}
