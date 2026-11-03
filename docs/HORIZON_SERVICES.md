# Horizon service capability matrix

All Switch behavior is **NOT TESTED**. HOST-PASSED means Linux execution or
explicit mocks; CROSS-BUILT means actual target compilation/linking.

| Service | Actual implementation | Remaining evidence / limitation |
| --- | --- | --- |
| VM archive / entry | Genuine 553-object upstream Zero archive and real JNI_CreateJavaVM PIE/NRO link | VM startup/classfile execution need device logs |
| Address reservation | Shared SDK/VM Alias coordinator, checked kernel spans and real libnx reservations | Process permissions/resources and third-party direct Alias users |
| Physical memory | Partial commit/uncommit, NONE/R/RW, retryable cleanup and private partial release | Hardware map/protect/unmap behavior; no arbitrary executable mappings |
| Memory budget | Actual process total/used queries and logged native heap policy | Loader heap override can consume physical budget before VM creation |
| Code policy | Zero code cache is data-only; libffi uses real libnx JIT RW/RX | Closure execution/cache permissions unverified |
| Time / CPU / process | Actual clocks, elapsed origin, core mask, stack/process identity | Native accuracy; Unix CPU accounting unsupported |
| Atomics / ordering | Actual AArch64/GCC primitives and copy/spin code | Host contention and ARM64 instruction checks pass; hardware ordering unverified |
| Mutex / permits / Parker | libnx monitor/semaphore, wait budgets and genuine JavaThread transitions | VM safepoint/interrupt workloads unverified |
| Thread lifecycle | Checked create/start handshake, borrowed attachments, process-lifetime reaper | Native Thread TLS/destructor/shutdown interaction unverified |
| TLS | Actual libnx slot plus compiler TLS and restore-before-detach | Native Java attachment/destructor ordering unverified |
| Fault handling | Exact SafeFetch load PCs and real exception-return syscall; original fatal path | Kernel transfer/return unverified; no generic native-fault recovery |
| Files / properties | Actual newlib IO, UTF-8, Horizon identity/device roots and shared FD cursor monitor | Some Unix attributes, locks, mmap and at-family APIs explicitly unsupported |
| Runtime image | Target classes/data; immutable read-only index snapshot and genuine ZIP inflater | File/image initialization and process memory permissions unverified |
| Native lookup / JNI | Versioned immutable resolver generated from actual archives; real ready-hook RegisterNatives | Hardware JNI execution and dependent modules need tests |
| BSD network / selector | Real socket-service initialization, IPv4 DNS/socket/vector IO and TCP loopback wakeup | IPv6 transport, interface enumeration and reachability unavailable |
| Native I/O interruption | NativeThread reports unsupported signaling | Blocking native IO cannot be interrupted with POSIX signals |
| Random seed | Checked libnx CSRNG IPC and Java error with raw Result | Real service/entropy execution unverified |
| Java FFM | Configured libffi fallback and real selected target C/math symbols | Native down/upcall, closure and thread-attachment tests unverified |
| Instrumentation | Horizon path/encoding sources prepared | JVMTI/java.instrument/static-agent build and workload still required |

See `OPENJDK25_MEMORY.md` for exact mapping, splitting, rollback, locks and
process-budget contracts. SDK metadata costs one byte per requested page plus
its control/token. The private bridge uses two bytes per page plus region and
reservation metadata. Alignment selects a real eligible Alias span without
reserving a larger ASLR padding region. Physical allocation occurs only at
commit. Calls never make an executable mapping through the generic memory API.

Callers must quiesce all mapped-byte accesses before protection, uncommit or
release. The locks protect native metadata, not arbitrary accesses. Failed
cleanup retains the owner; the diagnostic retries it before creating another
region. A successful syscall, correct readback and recoverable fault are three
separate observations. None is inferred from a host mock or a compiled object.
