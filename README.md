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

Original code is MIT; miniJVM C is MIT, its Java class library retains GPL-2.0
and per-file exception notices. Complete class-library and modified C source
accompany distributions. No game JARs or account credentials are included.
