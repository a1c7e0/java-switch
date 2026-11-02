/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef OS_HORIZON_PARK_HORIZON_HPP
#define OS_HORIZON_PARK_HORIZON_HPP
#include "mutex_horizon.hpp"

// One associated parker, any number of unparkers. Permits coalesce to one.
// All blocking waits release the mutex atomically through the libnx condition.
class HorizonParkPermit : public CHeapObj<mtSynchronizer> {
  NONCOPYABLE(HorizonParkPermit);
  PlatformMonitor _monitor;
  volatile int _permit;
  bool _parked;
public:
  HorizonParkPermit() : _permit(0), _parked(false) {}
  bool try_consume();
  int park(uint64_t nanos);
  void unpark();
  void reset();
  int fired();
};

class PlatformEvent : public CHeapObj<mtSynchronizer> {
  HorizonParkPermit _permit;
protected:
  // The shared ParkEvent free list requires immortal/type-stable events.
  ~PlatformEvent() { guarantee(false, "PlatformEvent is immortal"); }
public:
  PlatformEvent() = default;
  void park();
  int park(jlong millis);
  int park_nanos(jlong nanos);
  void unpark() { _permit.unpark(); }
  void reset() { _permit.reset(); }
  int fired() { return _permit.fired(); }
};

class PlatformParker {
  NONCOPYABLE(PlatformParker);
protected:
  HorizonParkPermit _permit;
public:
  PlatformParker() = default;
  ~PlatformParker() = default;
};
#endif
