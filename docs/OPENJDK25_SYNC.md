# Horizon HotSpot synchronization bring-up

## Mutexes and monitors

`ports/openjdk25/src/hotspot/os/horizon/mutex_horizon.{hpp,cpp}` implements the
real JDK25 `PlatformMutex`/`PlatformMonitor` interface. An opaque C bridge owns a
non-recursive libnx Mutex and CondVar; libnx typedefs never enter HotSpot C++.
No POSIX suspension or signal delivery is assumed. The bridge currently allocates
small control objects with newlib malloc, outside HotSpot NMT accounting.

`wait(0)` means infinite. Finite millisecond waits saturate at INT64_MAX
nanoseconds, so large values cannot wrap to negative/infinite kernel timeouts.
Result 0 means a possibly spurious wake; masked Horizon Result 0xEA01 means
timeout. libnx reacquires the mutex on either accepted outcome. Unexpected wait
errors abort through a HotSpot guarantee with the original Result because mutex
ownership is not established. The wake syscall has no Result return.
Destruction requires quiescent, unlocked objects with no remaining waiters.

The host test compiles the actual C++ implementations and the actual bridge,
replacing only libnx headers/functions with pthread-backed test code. It checks
80000 protected increments, non-recursive try-lock, predicate/broadcast wakeups,
infinite waits, timeout ownership, saturated conversion, allocation failures and
raw unexpected syscall errors. This establishes host mock semantics, not Switch
kernel behavior. `zero-services-test.nro` A adds native try-lock and timed monitor
wait checks. Native contention is not yet tested.

The full upstream static Zero build now passes the monitor header and stops at
`park_horizon.hpp`. Preserve `docs/evidence/openjdk25-monitor-build.log` and
`openjdk25-monitor-host.log`. Follow-on work must implement actual permit/event
semantics and the JavaThread interrupt/safepoint transition in Parker, rather
than treating an unconditional return as a parking implementation.
