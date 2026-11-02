/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef OS_HORIZON_OSTHREAD_HORIZON_HPP
#define OS_HORIZON_OSTHREAD_HORIZON_HPP
#include "runtime/osThreadBase.hpp"
#include "horizon_thread_bridge.h"

class OSThread : public OSThreadBase {
  uint64_t _thread_id;
  js_horizon_thread* _native;
public:
  OSThread() : _thread_id(0), _native(nullptr) { set_state(ALLOCATED); }
  // The VM free-thread/reaper path must detach ownership after native cleanup.
  // Never free a libnx stack from its still-running thread's C++ destructor.
  ~OSThread() override { guarantee(_native == nullptr, "native thread ownership still attached"); }
  uint64_t thread_id() const { return _thread_id; }
  void set_thread_id(uint64_t value) { _thread_id = value; }
  js_horizon_thread* native_thread() const { return _native; }
  void set_native_thread(js_horizon_thread* value) { _native = value; }
  uintx thread_id_for_printing() const override { return static_cast<uintx>(_thread_id); }
};
#endif
