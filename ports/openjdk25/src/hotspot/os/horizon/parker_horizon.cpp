/* SPDX-License-Identifier: GPL-2.0-only */
// JavaThread integration is compiled by the full VM build, not the standalone
// permit probe. It needs actual interrupt and safepoint machinery.
#include "runtime/park.hpp"
#include "runtime/javaThread.inline.hpp"
#include "runtime/interfaceSupport.inline.hpp"
#include "runtime/osThread.hpp"
#include "runtime/os.hpp"

void Parker::park(bool absolute, jlong time) {
  if (_permit.try_consume()) return;
  JavaThread* thread = JavaThread::current();
  if (thread->is_interrupted(false) || time < 0 || (absolute && time == 0)) return;
  uint64_t nanos = UINT64_MAX;
  if (absolute) {
    const jlong now = os::javaTimeMillis();
    if (time <= now) return;
    // A bounded real wait allows rechecking wall-clock changes. JUC permits
    // spurious returns; callers recheck their absolute deadline after return.
    const uint64_t remaining = static_cast<uint64_t>(time) - static_cast<uint64_t>(now);
    nanos = remaining > 1000 ? UINT64_C(1000000000) : remaining * UINT64_C(1000000);
  } else if (time > 0) nanos = time;
  // Never hold the permit's mutex while entering/leaving the safepoint region.
  ThreadBlockInVM blocked(thread);
  OSThreadWaitState waiting(thread->osthread(), false);
  _permit.park(nanos);
}
void Parker::unpark() { _permit.unpark(); }
