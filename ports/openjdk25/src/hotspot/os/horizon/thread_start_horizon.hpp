/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef OS_HORIZON_THREAD_START_HORIZON_HPP
#define OS_HORIZON_THREAD_START_HORIZON_HPP
#include "mutex_horizon.hpp"
#include "horizon_thread_bridge.h"

// Child reports actual stack/identity/TLS readiness, then waits until the
// fully constructed VM Thread is released by os::start_thread. Ownership of
// this object remains with OSThread; no child may access it after call_run.
class HorizonThreadStartup {
  NONCOPYABLE(HorizonThreadStartup);
  PlatformMonitor _monitor;
  bool _ready = false;
  bool _released = false;
  js_horizon_thread_status _status = JS_HORIZON_THREAD_STATE;
  uint32_t _raw = 0;
public:
  HorizonThreadStartup() = default;
  void report_and_wait(js_horizon_thread_status status, uint32_t raw) {
    _monitor.lock();
    guarantee(!_ready, "Horizon thread reported readiness twice");
    _status = status; _raw = raw; _ready = true;
    _monitor.notify_all();
    while (_status == JS_HORIZON_THREAD_OK && !_released) _monitor.wait(0);
    _monitor.unlock();
  }
  js_horizon_thread_status wait_until_ready(uint32_t* raw) {
    _monitor.lock();
    while (!_ready) _monitor.wait(0);
    auto result = _status;
    *raw = _raw;
    _monitor.unlock();
    return result;
  }
  void release() {
    _monitor.lock();
    guarantee(_ready && _status == JS_HORIZON_THREAD_OK && !_released,
              "Horizon thread start before readiness or repeated start");
    _released = true; _monitor.notify_all();
    _monitor.unlock();
  }
};
#endif // OS_HORIZON_THREAD_START_HORIZON_HPP
