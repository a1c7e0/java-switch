# OpenJDK 25 startup instrumentation on Horizon

The experimental runtime has an optional profile for Java startup agents. It
connects the real OpenJDK 25 `java.instrument` implementation to HotSpot JVMTI
and the Horizon static native resolver. The default runtime profile remains the
existing base profile; instrumentation changes are selected explicitly.

## Build profiles

Run `scripts/build-openjdk25-runtime.sh` for the base profile. It configures
services and JVMTI off, builds the upstream Zero VM and `java.base`, and packages
the base runtime. This is the reproducible default.

Run `scripts/build-openjdk25-runtime.sh --with-instrument` for the optional
profile. It uses `python3 scripts/openjdk25.py configure-agent`, which enables
both `jvm-feature-services` and `jvm-feature-jvmti`; OpenJDK requires JVMTI to
have services enabled. The build then compiles `java.instrument`, builds its
native archive, runs the host agent test, adds the module to the runtime image,
and links the instrument-enabled runtime. The separate
`python3 scripts/build-openjdk25-image.py --with-instrument` option includes
the target-built module in the image.

The runtime linker records the exact JVMTI/services feature set from the full
upstream `libjvm.a` proof. In the instrument profile it also checks the recorded
hashes and members of `libinstrument.a` and `libjli_manifest.a`, then checks the
linked ELF for `JNI_OnLoad_instrument`, `Agent_OnLoad_instrument`, and
`JLI_ManifestIterate`. `libjli_manifest.a` contains only upstream
`parse_manifest.o`, which implements the manifest parser used to find
`Premain-Class`. The full JLI launcher is not linked. The resolver is generated
from those actual archives and registered as the static `instrument` and
`jli_manifest` providers; this does not add dynamic ELF loading.

The shared Horizon native resolver remains in use for existing JNI and FFM
lookups. The agent path adds the real Instrumentation/JPLIS native provider and
its manifest parser. It does not claim support for attaching to a running VM:
`DisableAttachMechanism` defaults to true, and explicitly enabling it with
`-XX:-DisableAttachMechanism` or `-XX:+StartAttachListener` fails VM
initialization. Dynamic agent loading through the attach mechanism is therefore
unavailable.

## Smoke test and hardware procedure

The host smoke uses the pinned OpenJDK 25.0.5. Its agent's `premain` registers a
`ClassFileTransformer` that uses the Java 25 Class-File API to change a string
constant before `AgentSmoke` is initially defined. The application also checks
that the agent argument, system property, and main argument arrived, and that
`premain` and the transformer each ran once. It reports the VM's redefine,
retransform, and native-prefix observations without requiring those capabilities.

`scripts/test-openjdk25-agent-host.py` records both sides of the test in
`build/openjdk25-agent-host.log`. The positive `-javaagent` run prints
`JAVA25_AGENT_SMOKE_PASS`. The no-agent control fails at the transformed-string
assertion as expected, proving that the application requires the transformation.
This is a host reference-JDK result, not execution of the Horizon VM.

For a later physical-device run, build the instrument profile and copy the
contents of `dist/COPY_TO_SD/` to the SD card. The instrument-enabled
`switch/java25-test/java25-test.nro` provides the Y menu action. It launches the
agent JAR at
`sdmc:/switch/mc-runtime/tests/java25-agent-smoke-agent.jar` and the app JAR at
`sdmc:/switch/mc-runtime/tests/java25-agent-smoke-app.jar`; the package script
copies these exact generated JARs from `build/` into those paths. Select Y and
check all three files:

- `sdmc:/switch/mc-runtime/logs/java25-test.log` for startup status and the
  selected agent mode.
- `sdmc:/switch/mc-runtime/logs/java25-vm-stdout.log` for
  `JAVA25_AGENT_SMOKE_PASS`.
- `sdmc:/switch/mc-runtime/logs/java25-vm-stderr.log` for VM errors.

Use a fresh application process for each attempt. This diagnostic is one-shot:
the VM and its worker threads live for the process lifetime, so close the
application and reopen it from application-mode hbmenu before repeating. A
successful cross-build, package, host test, or resolver link does not establish
Switch startup or execution. Physical-device execution remains unverified.

## Current status

The host positive and negative controls pass on the pinned JDK, as recorded in
`docs/evidence/openjdk25-agent-host.log`. The genuine Horizon JVMTI/services archive contains 577 ARM64 objects;
`java.instrument`, the upstream-only manifest parser, the 7,336-class two-module
image and the final Horizon PIE/NRO cross-build all pass. The final ELF has zero
missing symbols and contains the real static agent entries. See
`docs/evidence/openjdk25-runtime-link.json` and the archive/module proofs.
Horizon VM startup, premain, JNI, FFM and transformations are **NOT TESTED**.
No Minecraft or title execution is claimed.
