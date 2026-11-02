/* SPDX-License-Identifier: GPL-2.0-only */
#include "park_horizon.hpp"
#include "runtime/atomic.hpp"
#include "runtime/os.hpp"

bool HorizonParkPermit::try_consume() { return Atomic::xchg(&_permit, 0) != 0; }
int HorizonParkPermit::park(uint64_t nanos) {
  _monitor.lock();
  guarantee(!_parked, "Horizon permit has more than one parker");
  _parked = true;
  const bool infinite = nanos == UINT64_MAX;
  if (!infinite && nanos > INT64_MAX) nanos = INT64_MAX;
  const uint64_t start = infinite ? 0 : static_cast<uint64_t>(os::javaTimeNanos());
  int outcome = OS_TIMEOUT;
  while (Atomic::load(&_permit) == 0) {
    uint64_t remaining = UINT64_MAX;
    if (!infinite) {
      const uint64_t elapsed = static_cast<uint64_t>(os::javaTimeNanos()) - start;
      if (elapsed >= nanos) break;
      remaining = nanos - elapsed;
    }
    if (_monitor.wait_nanos(remaining) == OS_TIMEOUT) break;
    // Ignore spurious condition wakes and recompute the remaining budget.
  }
  if (Atomic::load(&_permit) != 0) {
    Atomic::store(&_permit, 0);
    outcome = OS_OK;
  }
  _parked = false;
  _monitor.unlock();
  return outcome;
}
void HorizonParkPermit::unpark() {
  _monitor.lock();
  Atomic::store(&_permit, 1);
  if (_parked) _monitor.notify();
  _monitor.unlock();
}
void HorizonParkPermit::reset() { Atomic::release_store(&_permit, 0); }
int HorizonParkPermit::fired() { return Atomic::load_acquire(&_permit); }

void PlatformEvent::park() { _permit.park(UINT64_MAX); }
int PlatformEvent::park(jlong millis) {
  guarantee(millis > 0, "PlatformEvent timed wait must be positive");
  return _permit.park(millis > INT64_MAX / 1000000 ? INT64_MAX : millis * 1000000);
}
int PlatformEvent::park_nanos(jlong nanos) {
  guarantee(nanos > 0, "PlatformEvent timed wait must be positive");
  return _permit.park(nanos);
}
