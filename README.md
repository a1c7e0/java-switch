# java-switch

Native Java runtime engineering for Nintendo Switch 1 / Horizon / libnx.
Extracted from [Switchcraft](https://github.com/a1c7e0/switchcraft/tree/1.12.2)
at `feb2f4dd076d453d4f2db2be45096aecd4f2c2e7`.

The current working baseline is an interpreter-only miniJVM Java 8 diagnostic:
real JAR class loading, exceptions, reflection, threads, file I/O, allocation
and a statically registered native method. Cross-compilation and host tests
are verified; **the R2 Java8 diagnostic also passed on a physical Switch**. This baseline cannot run
modern Minecraft and does not implement standard JNI Invocation.

The main runtime target is **Java 25**, required by Minecraft
`26.4-snapshot-6`. A genuine OpenJDK 25 Zero VM, eight base native archives
and optional `java.instrument`/manifest archives now cross-build and link into
`java25-test.nro`. The instrument profile includes real HotSpot JVMTI. Its runtime image
contains target-built classfile-69 classes, configuration and data. **R2 VM
startup fails during heap initialization; Java execution, JNI, FFM and startup
agent execution on physical Switch remain NOT TESTED.** Minecraft's
Horizon title screen has not been demonstrated. R3 corrects the uncommitted
no-access guard request and protects early fatal heap reporting; these changes
require a fresh physical retest. See [the new feedback](docs/DEVICE_FEEDBACK_2026_11_09.md).

`java25-test.nro` A attempts the Java25 Hello JAR. X attempts the real JNI,
thread, NIO and FFM downcall/upcall suite on that VM. The optional instrument
profile adds Y for a real startup javaagent/Class-File API transformation.
Relaunch the application in a fresh process before selecting another mode. It records status, stdout and stderr under
`switch/mc-runtime/logs/`. Install the complete generated SD tree, including
`runtimes/openjdk25`; the NRO alone does not contain the class image.

The public runtime SDK remains **ABI 1**. Its ordinary SDK archive contains no
VM, so its Java25 backend reports unavailable until explicitly linked with the
real VM and native provider. A separate experimental static-runtime package
contains those integration inputs. The unchanged adapter also passes real
host Java25 JNI/FFM tests. See [the handoff](docs/SWITCHCRAFT_HANDOFF.md),
[port status](docs/JAVA25_PORT.md) and [hardware checklist](docs/HARDWARE_TEST_MATRIX.md).

`ffi-test.nro` preserves native AAPCS64 and libnx W^X tests. The independent
`zero-services-test.nro` links actual HotSpot time, synchronization, TLS, thread,
SafeFetch and Alias-memory services. It has no Java VM. Host mocks, ARM64
instruction emulation, cross-linking and physical-device results are recorded
separately. Source packages include the pinned OpenJDK archive and port notices.

```sh
bash scripts/build-all.sh
# Optional complete experimental VM build, including startup agents:
bash scripts/build-openjdk25-runtime.sh --with-instrument
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

Original SDK/diagnostic code is MIT; the shared Alias coordinator retains its
per-file GPL-2.0-only notice. The OpenJDK port overlay and its component
tests are GPL-2.0-only or retain their upstream per-file license. miniJVM C is MIT,
its Java class library retains GPL-2.0
and per-file exception notices. Complete class-library and modified C source
accompany distributions. No game JARs or account credentials are included.
