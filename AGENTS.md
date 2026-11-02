# java-switch engineering guide

This repository owns Horizon Java runtime work extracted from Switchcraft
commit feb2f4dd076d453d4f2db2be45096aecd4f2c2e7. Write documentation in English
and commit/push meaningful milestones. Never claim Horizon execution from a
host test or cross-link. Minecraft integration lives in a1c7e0/switchcraft.

`bash scripts/build-all.sh` bootstraps pinned dependencies, cross-builds the
interpreter diagnostic and SDK, runs real host JAR tests and packages artifacts.
Default toolchain: `.toolchain/opt/devkitpro`; override DEVKITPRO for reuse.
The source patch is `patches/minijvm-horizon.patch`; do not leave fixes only in
generated `.deps/`. Use application-mode hbmenu on physical Switch 1.

miniJVM is a Java 8 interpreter diagnostic with a custom native registry, not
a complete Java 25 runtime or standard JNI Invocation implementation. Java 25
is the required mainline target. Work on a real OpenJDK/Zero backend and preserve
the working interpreter test while recording exact cross-build blockers.
The miniJVM source patch now rejects classfile versions above 52.0 in the actual
loader (including direct JAR/defineClass paths), invalid magic and short headers.
Keep rejection cleanup null-safe and propagate failed definitions as exceptions;
`tests/test_java.py` exercises real JAR loading and VM teardown on these failures.
No fake successful mmap/dlopen/process/JIT implementations, game binaries,
credentials, kernel patches, overclocking or proprietary Nintendo libraries.

OpenJDK25 experimental build: `python3 scripts/bootstrap-openjdk-tools.py`,
then `python3 scripts/openjdk25.py configure` and `python3 scripts/openjdk25.py build`.
Patches live in `patches/openjdk25/`; pristine `.deps/openjdk25-source` must not
be edited. Horizon is its own OS family, not POSIX/Linux. Keep build failures
visible and logs in `docs/evidence/`. Configuration success is not VM startup.
Tracked native files are in `ports/openjdk25/`. Follow a failed full static build
with `components`, `test-host`, and `probe` actions to build the standalone
`zero-services-test.nro`. It has no Java VM. Package its full pinned upstream
archive plus overlay/notices with the NRO. Never include libnx headers directly
in HotSpot C++: u8/s8/Handle/Mutex conflict; use the opaque C bridge. Current
full build passes the PCH, mutex/monitor, OSThread/Parker headers and the POSIX
SafeFetch selection. Read the latest build evidence for its next exact blocker.
Native-thread primitives are implemented; HotSpot create/attach/detach/TLS/reaper
integration and safepoints remain unfinished. Runtime ABI v1 remains unchanged.
`docs/OPENJDK25_SYNC.md` and `docs/OPENJDK25_FAULTS.md` describe the private port.
The strong exception entry recovers only exact SafeFetch load PCs; unmatched
faults retain the libnx fatal path and debugger policy. Never extend that to a
general success stub. Run optional `bootstrap-openjdk-emulator.py` then
`test-openjdk25-faults.py` for actual ARM64 instruction tests with mocked kernel
transfer/return; these are not hardware evidence. C bridges are excluded from
the HotSpot C++ PCH by patch 0007. Keep all native source inputs staged before
packaging, regenerate components/probe after port changes, and serialize helper
actions through its existing flock.

`javaswitch/vmem.h` reserves aligned address space and maps/unmaps page ranges
using real physical-memory syscalls. Protect uses svcSetMemoryPermission for
NONE/R/RW; executable requests fail as unsupported. ffi-test Y keeps the original
whole-region test; B tests partial mapping and permissions. Mocked host tests do
not establish syscall availability. Partial release and full JVM integration
remain unimplemented. Serialize release against all accesses to the same region.

Export reusable runtime APIs and package the Java class library with its source
and GPL notices. Do not imply that binary-only distribution avoids GPL terms.
Host Linux JDKs are build/reference tools, never Horizon runtime artifacts.
