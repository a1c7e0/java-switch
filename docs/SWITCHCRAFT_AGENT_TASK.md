# Task for the existing Switchcraft agent

Continue work in the existing `a1c7e0/switchcraft` repository and task. Read its
live `AGENTS.md`, branch, dependency lock and current changes first. Preserve
the project's current conventions and the existing Java8/native diagnostics.
The previous source audit used Switchcraft commit
`7ce851b3c4fca4f85a6fee64fd9b0a40ef5d2255`; recheck findings against current HEAD.
Do not overwrite subsequent work or edit the java-switch runtime repository.
Write documentation in English, commit meaningful milestones and push them.

## Objective and pinned runtime

Integrate the genuine Horizon Java25 runtime into Switchcraft, adapt the actual
LWJGL/SDL3/Vulkan native path, and progress toward Minecraft
`26.4-snapshot-6` reaching its title screen on physical Switch 1. Keep the
Vulkan-only target and the existing SDL3 architecture. Do not introduce GLFW,
a host Linux runtime, streaming, kernel modifications or proprietary SDKs.

Pin `a1c7e0/java-switch` to this verified package source revision:

```text
b7c2ac7fd422bc2543e5c698db4e33a01cc0312d
```

This pin includes the clean-build directory fix (#1) and symlinked host-JDK
discovery fix (#2), with 17 host regressions. Independent instrument and base
workflows and their full package verification pass using a symlinked JDK cache
and initially absent native build outputs. See `docs/BUILD_REPRODUCIBILITY.md`.

The underlying instrumentation implementation checkpoint is
`db7d32d59968088c1575ccf70b216f9ee5446831`. Read these files at the package pin:

- `docs/SWITCHCRAFT_HANDOFF.md`: exact static integration, ABI and ownership.
- `docs/NATIVE_RESOLVER.md`: provider composition and lifetime contracts.
- `docs/OPENJDK25_INSTRUMENTATION.md`: genuine startup agent support.
- `docs/HARDWARE_TEST_MATRIX.md`: actions, observations and log locations.
- `docs/HORIZON_SERVICES.md`: implemented and unavailable services.

This is a real cross-built OpenJDK25 Zero VM, with 577 ARM64 archive members,
HotSpot JVMTI/services, ten native provider archives and a 7,336-class
`java.base,java.instrument` image. The final Horizon PIE/NRO link has zero
missing symbols. Host JNI/FFM integer and mixed floating-point tests, startup
agent transformation with a no-agent negative control, 14 baseline tests and
full package/source reconstruction verification pass.

**VM startup, classfile69, JNI, Java FFM and instrumentation on Horizon remain
NOT TESTED. No Horizon Minecraft/title execution has been established.**
Keep host tests, cross-builds and physical execution as separate evidence.

## Integration work

1. Update the runtime dependency to the exact package pin using Switchcraft's
   existing dependency mechanism. Generated `dist/` binaries are not committed
   to java-switch and are not automatically shared between task workspaces.
   Build them from the pin with:

   ```sh
   bash scripts/build-openjdk25-runtime.sh --with-instrument
   ```

   Consume `dist/javaswitch-horizon-sdk.tar.gz` and
   `dist/openjdk25-horizon-runtime.tar.gz`. Install the full runtime image,
   configuration, data, source and notices using the generated SD tree.

2. Preserve runtime ABI 1 and the versioned native-resolver extension. The
   baseline SDK alone remains VM-free and must report unavailable. Link the
   genuine VM and real native archives with the documented flags; retain
   `JNI_CreateJavaVM` despite section garbage collection. Compose the generated
   JDK resolver with Switchcraft's actual native providers before installing
   and sealing the single provider. Preserve exact module keys, genuine symbol
   addresses and process-lifetime callback/context storage. Unknown functions
   must remain unavailable. Follow the header and handoff for exact contracts.

3. Adapt LWJGL 3.4.3's real Horizon platform and loading paths. Upstream
   `modules/lwjgl/core/src/main/java/org/lwjgl/system/Platform.java` rejects
   `os.name=Horizon` with `LinkageError` during class initialization.
   `Library.java` invokes that detection before JNI loading; adding an enum
   alone does not implement the static loading path. Maintain truthful
   `os.name=Horizon`, AArch64 and explicit capability decisions. Connect native
   lookup to actual linked implementations rather than selecting Linux `.so`
   files or substituting successful stubs. The audited downstream pin has no
   LWJGL Horizon Java patch; recheck current HEAD before implementing one.

4. Inventory and implement the actual startup bindings incrementally. Beyond
   SDL3/Vulkan, remaining families include LWJGL core, VMA, shaderc,
   SPIRV-Cross, FreeType, OpenAL, STB and jemalloc. The strict host trace also
   loads `lwjgl_opengl` incidentally: inspect those initialization dependencies
   before omitting them while keeping rendering Vulkan-only. Record missing
   module/symbol names and exact native errors. Do not add speculative JDK
   modules merely to silence optional paths; follow actual exceptions and
   source dependencies. The confirmed LWJGL path uses `jdk.internal.misc.Unsafe`
   in java.base, not its legacy sun.misc.Unsafe fallback.

5. Preserve the actual launch requirements: Java25/classfile69, main class
   `net.minecraft.client.main.Main`, the existing pinned classpath/arguments,
   the genuine startup `-javaagent` bridge,
   `--enable-native-access=ALL-UNNAMED`, and
   `--add-exports=java.base/jdk.internal.misc=ALL-UNNAMED`. Classpaths use `;`
   so `sdmc:/` device roots survive. Default JAVA_HOME is
   `sdmc:/switch/mc-runtime/runtimes/openjdk25`. Verify the existing strict
   Vulkan transform still runs through real Instrumentation/ClassFileLoadHook.

6. Keep native memory and lifetime policies explicit. The diagnostic uses a
   checked 4 MiB worker stack and a 256 MiB native heap reservation to leave
   physical memory for Alias mappings; these are smoke settings, not a
   validated Minecraft memory budget. Log the loader heap override and actual
   process total/used memory. Preserve one VM per process. After a VM session,
   terminate the Horizon application process using the supported process-exit
   path; do not unload resident VM/reaper code back into hbloader.

## Verification and deliverables

First reproduce Java25 A (Hello), X (JNI/thread/NIO/TCP/selector and integer/FP
FFM), and Y (premain/Class-File API transformation) in fresh application-mode
processes. Then validate the composed Switchcraft provider and native graphics
path before attempting Minecraft. Keep each first failure visible and fix the
measured blocker. Without hardware, continue independent source/build work and
deliver a genuine combined NRO and precise test plan; do not promote a host
title trace or cross-link to a Switch execution result.

Preserve existing licensing/source distribution and game-input conventions.
Do not commit game JARs/assets, credentials or keys. Finish each milestone with
an immutable source pin, actual build/link evidence, artifact hashes, SD layout
and updated device checklist. Title-screen completion requires physical-device
evidence, not a successful native link or an availability query.

For a VM/runtime failure, return the complete `java25-test.log`,
`java25-vm-stdout.log`, `java25-vm-stderr.log`, relevant Switchcraft/native logs
and any Atmosphere crash report, with firmware/Atmosphere versions, application
or applet mode, artifact hash, launch options, last action and observed wait
time. The java-switch runtime task remains responsible for VM/class-library
fixes; the Switchcraft task owns launcher, LWJGL and graphics/native integration.
