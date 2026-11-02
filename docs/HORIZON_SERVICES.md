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
| HotSpot mutex/monitor | `mutex_horizon.{hpp,cpp}`, opaque libnx Mutex/CondVar bridge; raw Results and reacquisition guarantees | IMPLEMENTED, CROSS-BUILT, HOST-PASSED | Native timed waits/contention; HotSpot startup/NMT |
| Parking/event permits | `park_horizon.{hpp,cpp}`, binary permits and monotonic wait budgets; genuine `parker_horizon.cpp` JavaThread transition | IMPLEMENTED, CROSS-BUILT, HOST-PASSED permits | Native contention/interrupt/safepoint integration; Parker is compiled but no JavaThread has run |
| Native thread primitives | Checked opaque create/start/timed-join/close, identity and stack bounds in `horizon_thread_bridge.c`; X probe | IMPLEMENTED, CROSS-BUILT, HOST-PASSED failure/state mocks | HotSpot create/attach/detach/reaper and safepoint/handshake glue remain BLOCKED |
| HotSpot library TLS | `threadLocalStorage_horizon.cpp`, libnx slot through C bridge, restore-before-native-detach callback | IMPLEMENTED, CROSS-BUILT, HOST-PASSED; compiler accessor linked | Native isolation, actual Thread::initialize_thread_current, JNI attach/detach and native-destructor order remain NOT TESTED |
| SafeFetch | Real 32/64-bit loads, exact-PC early exception dispatcher, real svcReturnFromException; stock fatal fallback retained | CROSS-BUILT; ARM64 instruction EMULATOR-PASSED | Actual kernel frame transfer/resume NOT TESTED; general null/stack/Java faults remain BLOCKED |
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
