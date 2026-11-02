# Java 25 Horizon port: measured status

Target: Minecraft `26.4-snapshot-6`, Java classfile major 69, Java 25.
The Java 8 interpreter is retained as an independent working baseline; it must
reject a Java 25 launch rather than claiming compatibility.

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

## Source-build investigation

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

This is a missing Horizon OS port, not a completed compiler build. No Java 25
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
