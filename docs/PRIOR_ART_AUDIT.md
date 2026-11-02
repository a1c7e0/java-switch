# Prior art audit for the OpenJDK 25 Zero port

Audit date: 2026-11-02. Repositories were shallow-cloned and the files below
inspected. These are candidate implementations, not independently verified
working Java 25 ports. No implementation from the historical Switch JVM/game
projects was copied. CPU-only JDK25 code and narrowly scoped libnx assembly are
incorporated below, retaining their notices.

| Reference | Inspected commit | License and build evidence |
| --- | --- | --- |
| MiaouMeowMiaou/openjdk-switch | `9fb602f2db93ba8870fcaa30dc5fb2a60b783d36` | OpenJDK GPLv2; Classpath exception only where designated. `do_make.sh` builds a Linux server configuration, ignores make failure with `|| true`, then archives `*.o`. No reproducible successful build or hardware run established in this audit. |
| XorTroll/javm-nx-poc | `b746b2327a4ca5050a846365df3855f7ed466889` | MIT root license. README explicitly discontinued. `javm/Makefile` requires libjavm submodule `14ee2a2c728eb27cfb3f6214cdb3970cb4a47e83`; submodule implementation/license not audited or incorporated. Not built here. |
| SwitchGDX/switch-gdx | `77691794eed0d5e88f5a5498220b7036a9de2302` | GPLv2 root license; bundled libraries have individual licenses. Gradle/transpiler/CMake project, not a HotSpot build. Not built here. |
| SwitchGDX/clearwing-vm | `49a72945803c976298ceb262886866dd87261a96` | GPLv2 root; per-file notices/exceptions must be retained. Generated C++ runtime, not a dynamic classfile-69 VM. Not built here. |
| openjdk/jdk25u | `275848b087112429235f4f235822e294a1a288ef` | GPLv2, with Classpath exception only in designated files; `ASSEMBLY_EXCEPTION` and third-party notices apply. SHA256-pinned upstream is the source of the maintained patches. |

## Concrete subsystem mapping

Paths in this table are relative to the indicated repository and pinned commit
above. "Incorporated" refers to this session's code, not the existing baseline.

| Missing JDK25 subsystem/function | Candidate source/function | Suitability and incorporated status | Precise remaining gap |
| --- | --- | --- | --- |
| Target OS/toolchain classification | JDK25 `make/autoconf/platform.m4:PLATFORM_EXTRACT_VARS_FROM_OS`, `build-aux/config.sub`, `toolchain.m4`; GPLv2(+CE where designated) | Incorporated as tracked patches. Explicit `aarch64-none-horizon`, `horizon` OS family, newlib, GCC; does not reinterpret all ELF targets as Horizon. | Selecting a platform does not supply its native services. |
| Headless Zero dependencies | JDK25 `make/autoconf/libraries.m4:LIB_DETERMINE_DEPENDENCIES`; GPLv2+CE | Incorporated: no CUPS/fontconfig/freetype requirement for the experimental Horizon Zero/java.base build; libzip mmap disabled. | java.desktop is not ported. Do not build desktop targets assuming these dependencies were satisfied. |
| Executable reserve/commit and pointer translation | JDK8 `hotspot/src/os/linux/vm/os_linux.cpp:os::reserve_memory_special`, `os::pd_commit_memory`; GPLv2 | Rejected for direct reuse. Calls `switch_jit_alloc`, described as implemented in `posix_stubs.cpp`, but that source is absent from this repository. Commit/uncommit paths explicitly assume permanent JIT mappings. | JDK25 reserved spaces, guard pages and partial uncommit require independent semantics. Zero does not need optimizing-JIT pointer rewriting. Existing libnx closure allocator remains separate. |
| Memory protection / polling page | JDK8 same file `linux_mprotect`, `os::protect_memory`; JDK25 `src/hotspot/share/runtime/safepointMechanism.cpp`; GPLv2 | Design comparison only. JDK8 delegates to POSIX `mprotect`; that does not establish Horizon behavior. | Must check `svcSetMemoryPermission` legality for mapped physical pages and verify native results. No unconditional-success mprotect. |
| Fault recovery | JDK8 README claims `__libnx_exception_handler`; `hotspot/src/os_cpu/linux_aarch64/vm/os_linux_aarch64.cpp`; GPLv2 | Claim not established by inspected implementation: no such handler definition found in HotSpot. Not incorporated. New SafeFetch islands use the public libnx frame/SVC ABI; preserved libnx fatal assembly retains ISC notice. | Exact-PC native reads are implemented/emulated, kernel resume NOT TESTED. General Java/null/stack faults remain unimplemented; Horizon is not POSIX ucontext. |
| Thread/safepoint signaling | JDK8 `os_linux.cpp` Switch branches for `sigprocmask`, `pthread_sigmask`, `sched_setaffinity`; GPLv2 | Rejected: these branches return success without performing the requested operation. | Must implement actual libnx lifecycle/TLS and cooperative suspension with HotSpot handshakes. No signal-based suspension assumption. |
| Thread attach/lifecycle | Clearwing `runtime/res/clearwing/src/Thread.cpp:threadEntrypoint`, `M_java_lang_Thread_start`, `Clearwing.cpp:attachThread`; GPLv2 | Useful lifecycle comparison; not incorporated. Its own GC context, generated frame roots and `std::thread` layout are unrelated to HotSpot. | JDK25 `JavaThread`/`OSThread`, native stack bounds, monitors, TLS, safepoints and cleanup. |
| Native launcher/JAR main | javm `javm/source/Main.cpp:main`, `machine.LoadJavaArchive`, `nx/native/nx_Java.hpp`; MIT | Useful SD/argv/error-applet example, not incorporated. Its VM method registration is not JNI. | Public SDK uses caller-supplied slash-separated main class. OpenJDK runtime image and manifest discovery still required. |
| Files/SD paths | Clearwing `runtime/res/clearwing/src/File.cpp:M_java_io_File_exists_R_boolean`; SwitchGDX `switch-gdx/res/switchgdx/SwitchGDX.cpp`, `project/DevkitA64Libnx.cmake`; GPLv2 | Design comparison only; toolchain correctly uses Generic, static libraries, libnx and `-mtp=soft`. | JDK25 `java.base` file/NIO natives and jimage lookup need Horizon implementations; C++ filesystem success in another runtime is not evidence for Java NIO. |
| FFM native calls | JDK25 `make/autoconf/jdk-options.m4:JDKOPT_SETUP_FALLBACK_LINKER`, `src/java.base/share/native/libfallbackLinker`; GPLv2+CE where designated | Retain real libffi fallback route; existing SDK downcall/closure code is not itself Java FFM. | Link class-library native entry points to real Zero, preserve arena lifetime and test down/upcalls on device. |
| Atomic ordering and RMW | JDK25 `src/hotspot/os_cpu/linux_aarch64/orderAccess_linux_aarch64.hpp`, `linux_zero/atomic_linux_zero.hpp`; GPLv2 | Incorporated in the Horizon Zero overlay: AArch64 compiler fences/ISB and generic GCC Zero atomics. These functions use no Linux services. Retained notices, explicit AArch64/Zero guard, no outline-atomic runtime discovery. | Native contention/ordering still NOT TESTED; sequential checks are in the new NRO, four-thread host checks pass. |
| Counting semaphore | libnx `nx/source/kernel/semaphore.c:semaphoreInit/Signal/Wait/TryWait`; ISC-style license | New GPL HotSpot adapter calls the installed libnx APIs through an opaque C bridge; no libnx code copied. | Native contention and integration with HotSpot safepoint checks remain unverified. |
| Clock/time | libnx `nx/include/switch/arm/counter.h:armGetSystemTick/Freq`; ISC-style license | New `os::javaTimeNanos`, `javaTimeMillis`, `javaTimeSystemUTC`, `javaTimeNanos_info`; C bridge avoids incompatible HotSpot/libnx typedefs. | VM startup timestamp/elapsed-counter initialization still required; device counter and wall-clock accuracy not tested. |
| Condition monitors/parking | libnx `condvar.c`, `mutex.c`; JDK25 PlatformMonitor/Parker interfaces | New adapters preserve raw wait Results, mutex reacquisition and binary permits; no POSIX signal suspension. | Native and JavaThread/safepoint behavior NOT TESTED. |
| Native TLS | libnx `thread.c:threadTlsAlloc/Get/Set`, `runtime/readtp.s`; JDK25 `threadLocalStorage_posix.cpp` restore design | New actual library TLS backend; C bridge, no copied POSIX thread implementation. Final probe ELF resolves `__aarch64_read_tp` to libnx's TPIDRRO_EL0 + 0x1F8 sequence. | Compiler/library isolation has native X probe and host tests, but no hardware result; JNI attach/detach and destructor ordering across other native libraries remain unverified. |

## Techniques that cannot be carried forward

JDK8 Linux `os::Linux`, signal contexts, polling pages, code-cache aliases and
C1/C2 assumptions are not a JDK25 Horizon platform. HotSpot interfaces, native
memory accounting, safepoints/handshakes and Java class-library layout changed.
JDK25 uses modular runtime images and the FFM fallback library; a JDK8 `rt.jar`
or a collection of server VM objects cannot provide those services. The pinned
JDK8 reference includes successful no-op signal/affinity implementations; none
are acceptable for this port.

libnx source was inspected at `switchbrew/libnx`
`b156b4774fb2df7b93402169509266da7547fc4c`. Its JIT and virtmem headers match
the installed toolchain headers byte-for-byte. The binary's complete source
revision was not inferred from that match: the toolchain OCI digest remains
the build pin. The installed `libnx.a` SHA256 is
`12a59cdfdceab83e39ee129b0275dd803e393e83f19de3922a3b6cc95c767edd`.
Its ISC-style notice remains in `licenses/LIBNX-LICENSE.md`.
`virtmemFindAslr`/`virtmemAddReservation` only reserve address
space; `svcMapPhysicalMemory` allocates backing pages. `jitCreate` and transition
functions are the supported executable-memory route. Process capability and
hardware behavior must be measured, not inferred from declarations.
