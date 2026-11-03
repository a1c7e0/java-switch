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
Latest optional JVMTI/services build: 577 AArch64 objects and the genuine upstream libjvm.a target
complete. A real Horizon PIE link containing JNI_CreateJavaVM now passes with
zero missing symbols (not VM startup). See openjdk25-static-archive.json and
openjdk25-vm-link.log/json in evidence. Eight native java.base archives, the target class/data image and a genuine
java25-test.nro now build; no classfile69 or Minecraft execution on
Horizon has been verified.
HotSpot create/attach/detach/TLS/startup handshake and deferred native cleanup
are implemented and host-mocked. Safepoint/managed execution remains unverified. Runtime ABI v1 remains unchanged.
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
not establish syscall availability. The public SDK does not expose partial reservation release. The private HotSpot
bridge implements it; both use the shared Alias coordinator. See
`docs/OPENJDK25_MEMORY.md` for syscall eligibility, budget and ownership rules. Serialize release against all accesses to the same region.

Export reusable runtime APIs and package the Java class library with its source
and GPL notices. Do not imply that binary-only distribution avoids GPL terms.
Host Linux JDKs are build/reference tools, never Horizon runtime artifacts.

Full VM link diagnostics use `python3 scripts/openjdk25.py link-vm`; the result
is an actual AArch64 Horizon ELF, not an NRO or execution proof. It checks the
upstream archive hash and records exact source/SDK/ELF hashes. `java-base` runs
actual target javac and copies resources, while `native-base` continues native
compilation after errors to expose remaining platform dependencies. Image
construction uses host jmod/jlink as tools only; never bundle a host VM/native
library. Keep native JNI symbol registration derived from actual archives.

Current checkpoint has classfile-69 Hello (A) and real JNI/thread/NIO/TCP/selector/
FFM suite (X) on the genuine VM NRO. These are CROSS-BUILT, not Switch-PASSED.
Use `scripts/test-openjdk25-classlib-host.py` for maintained class-library tests.
Property bootstrap must supply non-null user.name/home: '?' denotes unknown
identity and sdmc:/switch/mc-runtime is the explicit app-home default; actual
Unix ID getters return their documented unavailable value -1. Horizon IPv6
transport stays unavailable because SDK IPv6 address declarations are only
compatibility definitions; do not infer ABI support from a socket probe.

Normal/emergency VM exits use actual svcExitProcess through the C system bridge.
NRO libc exit can return to hbloader while VM/daemon/reaper threads survive; do
not unload their code. Native-services and completed VM diagnostics close the
application process. Reopen application-mode hbmenu for the next test. Emergency
termination avoids stdio locks. The public SDK still permits one VM per process.

Optional standard startup agents now genuinely cross-build and link. Run
scripts/build-openjdk25-runtime.sh --with-instrument for JVMTI/services plus
java.instrument; the default base profile still disables these features.
The actual extra archives are libinstrument.a and parse_manifest.o-only
libjli_manifest.a. The image contains 7,336 classfile-69 classes across two modules.
Y attempts the host-validated premain/Class-File API initial-definition transform;
all Horizon execution is NOT TESTED. Attach is disabled and explicit enabling
fails; unsupported CPU timer capabilities stay false, and invariant-bypassing
void hooks fail loudly. Async signal-context frame sampling returns unavailable.
Direct javac avoids cloud compiler-server startup failures. Source prepare now
publishes reconstructed trees only after every patch succeeds; its rollback test
runs in make host-test. Keep all port inputs tracked before packaging.

Minecraft also needs downstream LWJGL Horizon platform/native-loader adaptation:
3.4.3 Platform currently rejects os.name=Horizon before Library JNI loading.
Do not spoof Linux or edit Switchcraft. The handoff contains exact source paths.
Management and other game modules still need workload evidence. Next priorities:
actual device startup/A/X/Y logs, genuine JNI/FFM/class-transform failures, and
composition of the downstream native providers through the immutable SDK API.

Published instrumentation implementation checkpoint:
`db7d32d59968088c1575ccf70b216f9ee5446831`. Full `bash scripts/build-all.sh`,
14 host tests, class-library tests, actual JVMTI/native/image/NRO build and the
complete package verifier pass. The verifier reconstructs the full pinned source
with zero-fuzz patches and overlays, checks all integration/SD bytes and class69.
The Java25 X suite now includes real pow and mixed long/double/int/float FFM
callback/downcall results, host-PASSED; no Horizon execution is established.
`dist/manifest.json` records the exact package source commit; docs-only commits
can follow the implementation pin. Keep source bundles and manifests coherent
by repackaging from the clean final tree. Read the device matrix before testing;
all A/X/Y VM sessions require a fresh application process.

Build issue fixes (source pin b7c2ac7fd422bc2543e5c698db4e33a01cc0312d):
#1 creates configure/libffi parent directories before archive preparation;
#2 follows valid host-JDK cache symlinks, rejects absent/empty caches clearly,
and cleans test temporary files. The 17-test host suite and full class-library
suite pass. A detached worktree at /tmp/java-switch-clean-issues completed the
entire --with-instrument workflow with no initial build/ and a symlinked JDK25
cache. Its package/source verifier passes; evidence is in
`docs/evidence/issues-1-2-clean-instrument.json`. No hardware execution occurred.
Both fixes are pushed and issue comments contain the candidate pin. Issues stay
open pending final base-profile validation. Next run: preserve instrument proofs,
remove only that isolated worktree's build/openjdk25-horizon directory, assert it
is absent, and run scripts/build-openjdk25-runtime.sh without --with-instrument.
Do not remove or modify the main workspace's existing VM artifacts.
