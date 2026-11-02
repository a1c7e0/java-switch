/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef OS_HORIZON_MUTEX_HORIZON_HPP
#define OS_HORIZON_MUTEX_HORIZON_HPP
#include "memory/allocation.hpp"
#include "horizon_bridge.h"

class PlatformMutex : public CHeapObj<mtSynchronizer> {
  NONCOPYABLE(PlatformMutex);
  js_horizon_monitor* _native;
protected:
  js_horizon_monitor* native() { return _native; }
public:
  // libnx mutexes need per-object initialization only; no global attributes.
  static void init() {}
  PlatformMutex();
  ~PlatformMutex();
  void lock();
  void unlock();
  bool try_lock();
};

class PlatformMonitor : public PlatformMutex {
  NONCOPYABLE(PlatformMonitor);
public:
  PlatformMonitor() = default;
  ~PlatformMonitor() = default;
  int wait(uint64_t millis);
  int wait_nanos(uint64_t nanos);
  void notify();
  void notify_all();
};
#endif
