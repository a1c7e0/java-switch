# Horizon service capability matrix

All native behavior below is **SWITCH HARDWARE NOT TESTED**. CROSS-BUILT means
devkitA64 compilation/linking only. HOST-PASSED means a Linux reference or mock.

| OpenJDK service | Implemented code / evidence | Status | Remaining dependency |
| --- | --- | --- | --- |
| Address-space reservation | `src/vmem_horizon.c`, `js_vmem_reserve_aligned`; libnx find/reserve under virtmem lock | IMPLEMENTED, CROSS-BUILT, HOST-PASSED | Device address-space/heap limits; fixed-address reservation not exposed |
| Physical backing | `js_vmem_commit_range` / `js_vmem_uncommit_range`, one syscall per range | IMPLEMENTED, CROSS-BUILT, HOST-PASSED | Actual 0x2C/0x2D permissions and resource limits |
| R/RW/NONE access | `js_vmem_protect_range`, `svcSetMemoryPermission` | IMPLEMENTED, CROSS-BUILT, HOST-PASSED | The kernel may reject permission changes for this memory state; raw Result is decisive |
| Release | Page-run unmap with retry; entire address reservation freed after all runs succeed | IMPLEMENTED, CROSS-BUILT, HOST-PASSED | Partial address-space release/splitting, needed by HotSpot alignment strategies |
| Executable pages | `src/ffi_horizon.c`, libnx JIT RW/RX allocation and transitions | IMPLEMENTED, CROSS-BUILT, HOST-PASSED lifecycle mocks | Real process JIT capabilities; actual AAPCS64 closure execution |
| General executable protection | vmem returns `JS_VMEM_UNSUPPORTED` | BLOCKED | No arbitrary RWX or mmap alias; use JIT allocator only |
| HotSpot clocks | `ports/openjdk25/src/hotspot/os/horizon/os_horizon_time.cpp`, opaque libnx counter C bridge, newlib wall clock | IMPLEMENTED, CROSS-BUILT, HOST-PASSED with mocked clocks | Native accuracy; VM initial elapsed-counter state |
| HotSpot counting semaphore | `HorizonSemaphore`, opaque libnx semaphore bridge | IMPLEMENTED, CROSS-BUILT | Native token/contended execution; allocation failure aborts through HotSpot guarantee |
| HotSpot Atomic/OrderAccess | Horizon Zero headers using JDK25 AArch64 fences and GCC Zero atomics | IMPLEMENTED, CROSS-BUILT, four-thread HOST-PASSED | Native contention/ordering validation |
| Native threads/TLS | libnx `threadCreate/Start/WaitForExit/Close`, `threadTls*`, `threadGetSelf` provide candidate primitives; miniJVM baseline uses its own glue | BLOCKED for HotSpot | JDK25 `OSThread`, `JavaThread` attach/detach, monitors, Parker, safepoint/handshake coordination |
| Recoverable faults | libnx exception entry copies `ThreadExceptionDump` and redirects to the callback; ordinary callback return calls `svcBreak` | BLOCKED | Resumable per-thread recovery and reentrant context handling; cannot substitute a POSIX signal handler |
| Files/stdout/time/random | Existing libnx/newlib services and Java8 host diagnostic | BLOCKED for complete JDK25 | OS properties, image lookup and `java.base` native services still need wiring |
| Static JNI lookup | SDK `on_vm_ready` can call real `RegisterNatives`; weak `JNI_CreateJavaVM` import | HOST-PASSED, Horizon VM BLOCKED | HotSpot built-in native-library/symbol resolver, java.base natives, image; no host dlopen fallback |
| Java FFM | Host Java25 down/upcall tests; native FFI A/X probes | HOST-PASSED; Horizon Java FFM BLOCKED | Real Zero startup and `libfallbackLinker` native integration |
| Instrumentation/retransformation | Disabled in bring-up configuration | BLOCKED | JVMTI source port and actual workload validation |

## Mapping contract and footprint

Page size is 4096. Alignment must be a power of two at least one page. The
allocator reserves `size + alignment - 4096` virtual bytes to choose an aligned
interior start, then keeps that complete reservation until release. Physical
allocation happens only in commit. Metadata costs one byte per requested page
plus one small control object and libnx reservation metadata. A 256 MiB region
therefore needs 64 KiB of page-state metadata, in addition to committed backing.
This cost is separate from Java heap, VM metadata, native stacks and FFI memory.

Mixed mapped/unmapped commit/uncommit requests fail locally with JS_VMEM_STATE;
they never partially apply. A valid range uses one syscall and updates state
only after its successful return. The underlying kernel operation's error
semantics still need hardware verification. Protection never makes an executable
mapping. JS_VMEM_NONE stays physically committed. Release visits contiguous
mapped runs; if one unmap fails, earlier successful unmaps remain applied and
the caller must retain/retry the handle. No reservation is discarded while any
known mapping remains.

The internal mutex serializes state/syscall operations. It does not protect
callers dereferencing mapped addresses, and does not make concurrent release
safe. Callers must quiesce all accesses before uncommit/protect/release. The NRO
retains failed-release handles and retries before creating another region.
No successful memory permission call proves that a fault can be recovered.

The experimental semaphore bridge uses newlib malloc/free for its small opaque
control object. It is not yet accounted through HotSpot NMT; full VM memory
accounting and startup integration remain required.
