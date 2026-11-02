# java-switch

Native Java runtime engineering for Nintendo Switch 1 / Horizon / libnx.
Extracted from [Switchcraft](https://github.com/a1c7e0/switchcraft/tree/1.12.2)
at `feb2f4dd076d453d4f2db2be45096aecd4f2c2e7`.

The current working baseline is an interpreter-only miniJVM Java 8 diagnostic:
real JAR class loading, exceptions, reflection, threads, file I/O, allocation
and a statically registered native method. Cross-compilation and host tests
are verified; **Horizon execution is NOT TESTED**. This baseline cannot run
modern Minecraft and does not implement standard JNI Invocation.

The active runtime target is **Java 25**, required by Minecraft
`26.4-snapshot-6`. OpenJDK/Zero investigation and exact platform/build blockers
are recorded in `docs/`. A Linux JDK is not shipped as a Switch runtime.

The SDK provides a versioned C runtime interface and a standard JNI adapter.
The adapter passes Java 25 JNI/FFM downcall/upcall tests on the host. Its Horizon
Java 25 backend reports unavailable until a real VM is linked; the supplied
miniJVM backend supports Java 8 only. See [JAVA25_PORT.md](docs/JAVA25_PORT.md).

`ffi-test.nro` and the SDK also contain native AAPCS64 libffi support and real
libnx W^X upcall allocation, prerequisites for Zero's fallback FFM linker. These
are cross-built CPU/ABI tests; hardware execution is still NOT TESTED.

The experimental OpenJDK25 build passes configure as a distinct Horizon target
and compiles genuine shared/Zero VM sources beyond the precompiled header.
Monitors, parking permits, native-thread primitives and narrow SafeFetch fault
islands are implemented; the complete OS layer and VM remain unfinished.
Tracked patches/overlay and exact logs are in `patches/openjdk25/`,
`ports/openjdk25/` and `docs/evidence/`. An optional `zero-services-test.nro`
links actual port synchronization/thread/fault components, but has **no Java VM**.
Its source bundle includes the full pinned OpenJDK archive and GPL notices.
See [the port instructions](ports/openjdk25/README.md) and
[the downstream handoff](docs/SWITCHCRAFT_HANDOFF.md).
Host mock tests and ARM64 instruction emulation are separate from physical
Switch evidence. All hardware behavior, including fault recovery, is NOT TESTED.

```sh
bash scripts/build-all.sh
```

Requires Linux x86_64, Python 3.12+, GCC/G++, Make, patch, Git and HTTPS access.
Set `DEVKITPRO=/path/to/devkitpro` to reuse a toolchain. The same digest-pinned
toolchain extraction fallback is available for small disks. Generated files are
under `build/` and `dist/`; dependencies are hash-locked.

Copy the generated SD package to the card root and run `java-test.nro` using
application-mode hbmenu. Press A for bundled tests; X runs a trusted Java 8 JAR
named `UserMain` from `sdmc:/switch/mc-runtime/tests/user.jar`. The runtime SD
root remains shared with Switchcraft for compatibility. Logs are at
`sdmc:/switch/mc-runtime/logs/java-test.log`. A synchronous Java call runs once
per process and may require HOME/relaunch if a custom program does not return.

Original SDK/diagnostic code is MIT; the OpenJDK port overlay and its component
tests are GPL-2.0-only or retain their upstream per-file license. miniJVM C is MIT,
its Java class library retains GPL-2.0
and per-file exception notices. Complete class-library and modified C source
accompany distributions. No game JARs or account credentials are included.
