# Java 25 Horizon port: measured status

Target: Minecraft `26.4-snapshot-6`, Java classfile major 69, Java 25.
The Java 8 interpreter is retained as an independent working baseline; it must
reject a Java 25 launch rather than claiming compatibility.

The incoming miniJVM parser recorded classfile versions without enforcing them;
a host regression reproduced execution of a Java8 diagnostic relabeled major69.
The tracked miniJVM patch now enforces 45.0..52.0 in the loader itself, covering
direct JAR/defineClass entry points as well as the SDK's existing major gate.
Invalid magic and short headers also fail. Failed definitions propagate through
the existing NoClassDefFoundError path with an explicit parser log; exact
UnsupportedClassVersionError compatibility is not implemented. Rejected classes
retain safe GC cleanup, the native defineClass bridge checks parse failure, and
the real JAR tests require clean teardown without executing the rejected body.

## Implemented and exercised

* Versioned C SDK, AArch64 static library, runtime availability/version checks.
  No Linux `dlopen` is used by the Horizon invocation adapter.
* Standard JNI Invocation adapter: `JNI_CreateJavaVM`, main-class/argument
  construction, native-registration hook, exceptions and `DestroyJavaVM`.
  The adapter checks the actual VM's Java version. On Horizon no VM is linked,
  so it returns `JS_UNAVAILABLE` rather than entering Java 8.
* The same adapter linked to hash-pinned host Temurin Java 25 executes a real
  classfile-69 JAR, JNI native method, records/reflection, threads/lambda/NIO,
  FFM downcall to strlen and an FFM upcall trampoline. Passing these tests proves
  the adapter on the host, not a Horizon JVM port.

## Source-build investigation (updated 2026-11-02)

**J1 configuration gate crossed.** The tracked patches in `patches/openjdk25/`
classify `aarch64-none-horizon` as Horizon/newlib (not POSIX/Linux) and select
devkitA64 GCC. The real upstream configure completed with Zero and the libffi
fallback linker using the actual W^X libffi archive. Horizon JNI/conf inputs,
compiler headers, default flags, AArch64 atomic/order primitives, actual `os::`
time functions, counting semaphores, mutexes/condition monitors, event permits,
Parker transitions, native-thread primitives, library TLS and narrow SafeFetch
fault islands are tracked. The full build passes the precompiled header and
compiles genuine shared/Zero VM sources; it is still an incomplete port.
See the latest evidence logs, `docs/OPENJDK25_SYNC.md` and
`docs/OPENJDK25_FAULTS.md` for the current exact barrier and validation boundaries.
The latest full target generates 330 AArch64 objects, then fails at missing
`threadCrashProtection_horizon.hpp` in `runtime/mutex.cpp`; broader crash
protection remains unimplemented. Evidence: `openjdk25-native-build.log`.
No `libjvm.a` or usable
Horizon Java 25 runtime has been produced.

Reproduce the experimental build separately from the preserved diagnostics:

```sh
bash scripts/build-all.sh
python3 scripts/bootstrap-openjdk-tools.py # optional if system autoconf/m4 exist
python3 scripts/openjdk25.py configure
python3 scripts/openjdk25.py build
python3 scripts/openjdk25.py components
python3 scripts/openjdk25.py test-host
python3 scripts/openjdk25.py probe
```

The helper verifies the upstream archive hash, preserves `.deps/openjdk25-source`,
copies it to `.deps/openjdk25-horizon`, and applies tracked patches with zero
fuzz and copies the tracked `ports/openjdk25/` overlay. Logs are
`build/openjdk25-configure.log` and `build/openjdk25-build.log`;
the generated configuration and compiler diagnostics are under
`build/openjdk25-horizon/`. The build target is `hotspot-zero-static-libs`.
Linux build tools are never included as Switch runtime artifacts.

`zero-services-test.nro` A links time/synchronization/event components and tests
HotSpot Atomics. X checks native ID/stack, compiler and library TLS isolation,
and 64 permit exchanges with checked join/close. Y is an experimental SafeFetch
fault-recovery test and may abort or stall. There is no VM entry point. Device
execution is NOT TESTED. Host clock/monitor/permit/TLS/failure tests and actual
ARM64 instruction tests with a mocked kernel are separate evidence.
The full source archive accompanies this GPL component
diagnostic. `java25-hello.jar` contains a minimal classfile-69 Hello World and
manifest; it can only be run on the host reference JDK at present.

J0 baseline/audit and J1 configure are complete. J2 has cross-built native
components and page-mapping probes, but no complete OS platform. J3.1 through
J3.6 remain BLOCKED: no VM startup, classfile-69 execution, JNI, FFM,
instrumentation or retransformation on Horizon has been established.

### Original observed barrier

OpenJDK source: `openjdk/jdk25u` `275848b087112429235f4f235822e294a1a288ef`.
Archive SHA-256 and host JDK are fixed in `dependencies.lock.json`. After
installing relocatable autoconf/m4, actual configuration with
`--openjdk-target=aarch64-none-elf --with-jvm-variants=zero
--enable-headless-only` reaches target detection and fails:

```text
checking host system type... aarch64-none-elf
checking target system type... aarch64-none-elf
configure: error: unsupported operating system elf
```

This was the original barrier; the tracked port now passes it. No Java 25
`libjvm.a`, runtime image, Java 25 NRO or Horizon title screen exists. See the
complete log in `docs/evidence/openjdk25-configure-horizon.log`.

Zero's `src/hotspot/cpu/zero/foreignGlobals_zero.cpp` disables the assembly
foreign linker. However, `make/autoconf/jdk-options.m4` enables a **libffi-based
fallback linker by default for Zero**. FFM is therefore not categorically
impossible on an interpreter: libffi downcalls and executable upcall closures
need real Horizon implementations. LWJGL 3.4.3 uses both in the reference launch.

## Required OS work

Port real virtual-memory reservation/commit/protection, executable RW/RX aliases
where needed, TLS/thread/safepoint/exception support, static JNI library/symbol
lookup and class-library native I/O/network services. Newlib/libnx do not provide
Linux mmap/dlopen/signal-context semantics automatically. Unsupported operations
must fail explicitly; unconditional-success stubs do not constitute a port.
Zero plus fallback FFM is the first source-port route; HotSpot AArch64 JIT follows
only after memory/fault/exception support works. No kernel patches or overclocking.

## Native FFI prerequisite delivered

libffi 3.8.0 cross-compiles for AAPCS64. Its generic ELF malloc-based closure
allocator was replaced with real libnx JIT RW/RX allocation, pointer translation,
cache/permission transition and teardown. `ffi-test.nro` separately tests integer
and mixed integer/FP downcalls (A), and an executable upcall closure (X). JIT
permission failure is reported as a libnx Result; no kernel change is attempted.
Both NRO and SDK compile/link, but these CPU/permission tests are NOT TESTED on
Horizon. They do not substitute for a Java 25 VM.

Closure preparation validates the RW/RX pair and transitions an executable
allocation back to writable before re-preparation. A concurrent free/preparation
attempt is rejected with EBUSY; callers must synchronize closure lifetime and
retry a free that reports EBUSY. Result codes are thread-local. A host test with
simulated libnx JIT calls covers these rules and injected transition failures;
it does not execute generated code or establish Switch memory permissions.

## Page-range memory experiment

The SDK exposes `javaswitch/vmem.h`: it reserves aligned, unmapped libnx virtual
address space and commits/uncommits page ranges with real Horizon physical-memory
syscalls. NONE/R/RW protection calls `svcSetMemoryPermission` and preserves its
raw Result; executable protection explicitly returns `JS_VMEM_UNSUPPORTED`.
Whole-region functions remain compatible. Release retries retain the reservation
and only the still-mapped page runs after partial failure. Release must be
synchronized against every other access to that region. Partial **release** and
complete HotSpot integration remain unimplemented. This is an experiment, not mmap.

In `ffi-test.nro`, Y reserves 64 KiB, attempts commit, writes and reads both
ends, uncommits, recommits and releases it; every syscall failure is logged.
Action B adds 64 KiB alignment, middle-page mappings, R/NONE/RW permission calls,
partial uncommit/recommit and readback without deliberately faulting. Failed
release retains the handle for retry on Y/B. Action X now tests both integer and
mixed integer/FP closures. The process may lack permission to map/protect memory;
that failure is an
expected diagnostic result, not a reason to patch the kernel. A host mock tests
alignment/overflow validation, allocation/system failures, permission failure,
partial release retry and concurrent overlapping commits. ASan/UBSan also pass.
The actual syscalls, readback and
memory limits remain NOT TESTED on Switch hardware.
