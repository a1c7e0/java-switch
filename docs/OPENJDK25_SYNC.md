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
syscall error. The HotSpot lifecycle integration is described below.

`zero-services-test.nro` A checks timers, semaphore/mutex/monitor and event
permits. X creates a real libnx worker, checks its ID and local stack address,
performs 64 permit handoffs, then joins/closes it. Failed cleanup retains both
handle and callback context across X retries; callback memory is never a returned
stack frame. Unexpected condition Results reach the SD log before abort.
All Switch behavior remains NOT TESTED.

## Library and compiler TLS

`threadLocalStorage_horizon.cpp` supplies the actual JDK25 ThreadLocalStorage
interface using one process-lifetime libnx slot. Initialization is serialized
before VM workers; allocation failure, repeated initialization and uninitialized
access are fatal. There is no fake per-process Thread pointer. libnx clears the
slot before its native-exit callback; the callback restores the association so
a later native/JNI destructor can detach and clear it, following HotSpot's
existing restore design. Cross-library destructor order and actual JNI detach
remain unverified.

Host tests compile this implementation and its C bridge with mocked libnx TLS.
They check four concurrent isolated workers, compiler TLS, callback restoration,
explicit clear and fatal initialization failures. X now checks main/worker
compiler `__thread` and library TLS with distinct opaque markers across 64
handoffs, restoring prior values before returning. It constructs no Thread or
JavaThread. The AArch64 probe references `__aarch64_read_tp`; the final ELF
contains libnx's actual `MRS TPIDRRO_EL0; LDR [X0,#0x1F8]; RET` accessor.
Linking and host tests do not establish native isolation on Switch.

The next lifecycle implementation must handle two distinct cases: attached
main/native threads own no libnx handle/stack, while VM-created workers require
an INITIALIZED/start handshake and deferred native cleanup. JavaThread::post_run
can delete its Thread/OSThread on the exiting native thread, before libnx's
threadExit clears TLS and exits. `os::free_thread` must transfer native ownership
to a surviving reaper; it cannot join/unmap the stack it is still using. Keep
that ownership alive independently of a deleted OSThread. The existing checked
native bridge is the primitive for this work, not a substitute for it.

The parking milestone passed the precompiled header and generated 133 shared/Zero
AArch64 objects before Zero's POSIX SafeFetch selection stopped compilation.
The dedicated Horizon fault islands now cross that barrier; see
`docs/OPENJDK25_FAULTS.md`. Patches 0008/0009 cross the POSIX-only permitted
function include and preserve full native IDs in shared diagnostic formats.
The TLS milestone generated 293 VM objects before newlib's missing SSIZE_MAX
macro stopped the NMT reporter (`openjdk25-tls-build.log`). Patch 0010 now uses
the actual `std::numeric_limits<ssize_t>::max()` bound. The full build generates
330 AArch64 objects and stops at `runtime/mutex.cpp` including the missing
`threadCrashProtection_horizon.hpp`. See `openjdk25-native-build.log` and the
object inventory in `openjdk25-native-objects.json`. Broader crash-protection
semantics are not supplied by the narrow SafeFetch dispatcher; do not create an
unconditional-success callback wrapper to cross this barrier.
Patch 0007 keeps libnx C bridges out of the C++ PCH; their real C objects now
compile in the upstream build, with inherited C++-only flag warnings recorded.
No static VM archive is complete. Configuration is narrowed to Serial GC, JNI checks and
Zero. Conservative OS capability flags decline implicit null traps and stack
guards until their actual recovery exists; fixed libnx stacks are fully backed
before start. This does not establish VM startup or correct stack overflow
recovery. JNI, Java FFM and classfile69 execution on Horizon remain unavailable.


## Owned-worker lifecycle (2026-11-02)

`os_horizon_thread.cpp` now implements actual HotSpot create/start/attach/free.
VM-created workers install Thread TLS, record their real libnx stack and native
ID, report INITIALIZED, and wait on `HorizonThreadStart` until shared HotSpot
construction completes. A failed child readiness report is joined before its
OSThread and startup gate are freed. Main and JNI-attached native threads borrow
the existing libnx stack/handle and never close it.

A process-lifetime libnx reaper starts before the first owned worker. Exiting
workers transfer their opaque native handle through an allocation-free queue;
the reaper waits for actual native exit before closing/unmapping the stack.
Timeouts retain ownership, and close failures are retried without joining twice.
The reaper owns all accesses after transfer. The bridge publishes RUNNING before
threadStart because a fast worker may retire and be reclaimed before threadStart
returns. Unexpected condition Results abort before any unsafe mutex unlock.

Host tests inject creation/start/wait/close failures and immediate worker exit,
including a mocked threadStart that reclaims its own wrapper before returning.
The real startup gate passes 400 concurrent workers and rejects early release,
double release and duplicate readiness reports. These checks use libnx mocks.
`zero-services-test.nro` B exercises native retirement while a worker remains
alive, then verifies deferred cleanup; device execution is NOT TESTED.

The old 330-object header barrier is crossed by a JFR-disabled crash-protection
capability predicate. No crash-protected callback can be constructed; enabling
JFR is a compile error until real recovery exists. This does not widen SafeFetch
or claim recovery from arbitrary native faults. Zero native copy helpers and
non-hour timezone formatting have separate host/instruction checks. A complete
archive and genuine VM link are being checked independently; none of these
lifecycle tests proves Java execution on Horizon.
