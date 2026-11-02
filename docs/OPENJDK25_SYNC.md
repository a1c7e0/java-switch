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

The monitor milestone passed the header and stopped at `park_horizon.hpp`.
Its evidence remains in `docs/evidence/openjdk25-monitor-build.log` and
`openjdk25-monitor-host.log`.

## Permits, parking and native threads

`HorizonParkPermit` coalesces repeated unpark calls into one permit, accepts a
permit issued before park, ignores condition spurious wakes, and recomputes
finite waits against a monotonic budget. `PlatformEvent` preserves the shared
immortal-event lifecycle. `Parker::park` uses the real JavaThread interrupt check
and `ThreadBlockInVM` transition, without holding the permit lock over a state
transition. Absolute-time waits are bounded to one second, allowing a permitted
spurious return so Java callers can recheck a changed wall clock. The host test
checks 5000 two-thread permit exchanges, coalescing/reset and spurious wakes.

The separate C thread bridge implements create/start/timed-join/close with raw
Results, current identity and stack bounds. Finite join values above INT64_MAX
are rejected; UINT64_MAX alone means infinite. Requested native stacks are
limited to 256 MiB (not a memory availability guarantee). Lifecycle calls are
caller-serialized. Started threads must join before close; failures retain the
handle. libnx threadClose discards svcCloseHandle's Result internally, a known
upstream limitation. Host failure injection covers allocation and each returned
syscall error. This is not yet HotSpot os::create_thread/attach/detach/reaper glue.

`zero-services-test.nro` A checks timers, semaphore/mutex/monitor and event
permits. X creates a real libnx worker, checks its ID and local stack address,
performs 64 permit handoffs, then joins/closes it. Failed cleanup retains both
handle and callback context across X retries; callback memory is never a returned
stack frame. Unexpected condition Results reach the SD log before abort.
All Switch behavior remains NOT TESTED.

The full upstream Zero build now passes the precompiled header and has generated
133 shared/Zero AArch64 VM objects. The current blocker is
`runtime/safefetch.hpp` selecting POSIX `safefetch_sigjmp.hpp` for Zero. No static
VM archive is complete. Configuration is narrowed to Serial GC, JNI checks and
Zero. Conservative OS capability flags decline implicit null traps and stack
guards until actual recoverable faults exist; fixed libnx stacks are fully backed
before start. This does not establish VM startup or correct stack overflow
recovery. JNI, Java FFM and classfile69 execution on Horizon remain unavailable.
