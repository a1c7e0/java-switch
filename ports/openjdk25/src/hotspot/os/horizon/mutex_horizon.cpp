/* SPDX-License-Identifier: GPL-2.0-only */
#include "mutex_horizon.hpp"
#include "runtime/os.hpp"

PlatformMutex::PlatformMutex() : _native(js_horizon_monitor_create()) {
  guarantee(_native != nullptr, "Horizon monitor allocation failed");
}
PlatformMutex::~PlatformMutex() { js_horizon_monitor_destroy(_native); }
void PlatformMutex::lock() { js_horizon_monitor_lock(_native); }
void PlatformMutex::unlock() { js_horizon_monitor_unlock(_native); }
bool PlatformMutex::try_lock() { return js_horizon_monitor_trylock(_native) != 0; }

int PlatformMonitor::wait(uint64_t millis) {
  // Horizon interprets timeouts as signed nanoseconds; never let multiplication
  // overflow into a negative timeout/infinite wait. HotSpot uses zero=infinite.
  const uint64_t maximum = INT64_MAX;
  const uint64_t nanos = millis == 0 ? UINT64_MAX :
      (millis > maximum / 1000000 ? maximum : millis * 1000000);
  return wait_nanos(nanos);
}
int PlatformMonitor::wait_nanos(uint64_t nanos) {
  if (nanos != UINT64_MAX && nanos > INT64_MAX) nanos = INT64_MAX;
  const uint32_t result = js_horizon_monitor_wait(native(), nanos);
  const bool timed_out = (result & UINT32_C(0x3fffff)) == UINT32_C(0xea01);
  guarantee(result == 0 || timed_out, "Horizon condition wait failed: Result=0x%x", result);
  return timed_out ? OS_TIMEOUT : OS_OK;
}
void PlatformMonitor::notify() { js_horizon_monitor_notify(native(), 0); }
void PlatformMonitor::notify_all() { js_horizon_monitor_notify(native(), 1); }
