# Switch device feedback — 2026-11-09

This report records the supplied physical Switch results for the R2 diagnostic
package. It separates the Java 8, native memory and Vulkan passes from the Java
25 startup failures. Java 25 execution, JNI/FFM workloads and startup-agent
execution remain unachieved. No combined-launcher logs were supplied, so that
application is NOT TESTED in this upload.

## Package and environment

The supplied archive was `log1109.zip`, SHA-256
`f42dd77d27c97a14f079f966fd8d90cb7be7e60220d8769d39213a9fb4d409f4`. The
`java25-test.nro` inside it has SHA-256
`a3c25fd79c6e929ce23d3e8b3daeee3f719411f794c573d97e44e5227496f0ef`; the NRO
and original archive are not committed here. The app logs identify Switchcraft
source `0dc4c1377f81b849e4366c167f0a20177e1fce98` and java-switch runtime
`bff51f38e218af147ca0c9f255fcf1b43be72daf`.

The logs identify firmware 21.2.0, Horizon application mode (`Applet type=0`),
and an AArch64 Switch environment. The crash reports identify a 3-core device
with 3 GiB, an OpenJDK 25.0.5 Zero VM, and a 4 MiB worker stack. The graphics
profile identifies an NVIDIA Tegra X1 using NVK; SDL reported a “Switch
Controller.” Atmosphere and hbmenu versions, console revision, handheld/docked
state, and the application launch method were not recorded. The JVM crash
reports have a device wall-clock date of Jan 27, 2025; use the supplied
2026-11-09 upload context and monotonic ticks for this report, not that clock
for session dating.

The supplied text evidence is preserved in
[`evidence/device-feedback-2026-11-09/`](evidence/device-feedback-2026-11-09/).
It includes the Java 8 and Java 25 app logs, private-memory probe, unique VM
stdout/stderr captures, three JVM `hs_err` reports, Vulkan profile, presentation
log, and both presentation result JSON files.

## Results

| Test | Result | Evidence |
| --- | --- | --- |
| Java 8 miniJVM diagnostic suite | PASS; `DIAGNOSTIC SUITE PASS`, return 0, JVM teardown completed | [java-test.log](evidence/device-feedback-2026-11-09/java-test.log) |
| Private-memory `zero-services-test` L probe | PASS; 16 KiB reserve/commit, protection round-trip, canaries, uncommit/recommit, partial release, SDK/VM reservation ownership, and cleanup | [zero-services-test.log](evidence/device-feedback-2026-11-09/zero-services-test.log) |
| Vulkan base profile and SDL3/VI presentation | PASS; 300 frames, 5232.742 ms elapsed, 16.525 ms average turnaround, 0.651–26.849 ms range, Vulkan result 0, clean teardown | [vulkan-present.log](evidence/device-feedback-2026-11-09/vulkan-present.log), [vulkan-present-result.json](evidence/device-feedback-2026-11-09/vulkan-present-result.json), [snapshot-vulkan.json](evidence/device-feedback-2026-11-09/snapshot-vulkan.json) |
| Java 25 A, Hello mode (PID 143) | FAIL during VM initialization, before Java output | [java25-test.log](evidence/device-feedback-2026-11-09/java25-test.log), [hs_err_pid143.log](evidence/device-feedback-2026-11-09/hs_err_pid143.log) |
| Java 25 X, Java25Smoke mode (PID 145) | FAIL during VM initialization, before the workload ran | [java25-test.log](evidence/device-feedback-2026-11-09/java25-test.log), [hs_err_pid145.log](evidence/device-feedback-2026-11-09/hs_err_pid145.log) |
| Java 25 Y, startup-agent mode (PID 147) | FAIL during VM initialization, before agent execution | [java25-test.log](evidence/device-feedback-2026-11-09/java25-test.log), [hs_err_pid147.log](evidence/device-feedback-2026-11-09/hs_err_pid147.log) |
| Combined `switchcraft-java25` | NOT TESTED in this upload; no combined-app logs were supplied | — |

The private-memory probe exercises a 16 KiB range. Its pass establishes those
native operations on this package and process; it does not establish that the
VM can reserve or protect its much larger heap.

## Java 25 failure analysis

A, X and Y were separate Java 25 launches. Each reached `sdk-launch-entry` and
`jni-create-entry`, then recorded a fatal error. The three `hs_err` reports say
`Internal Error (memoryReserver.cpp:521): cannot protect protection page`;
the fatal report identifies OpenJDK 25.0.5. The app log captured the same stage
for every attempt, with relative PC `0x3f8be0` and FAR zero. The Java process
reported no Java output, no completed JNI/FFM suite and no agent result. Horizon
did not produce a POSIX core file; retain the supplied `hs_err` reports and
relative-PC/register captures.

Source review of the pinned R2 runtime traces the primary failure to HotSpot's
`ReservedSpace::special()` compressed-oops heap setup. Its no-access prefix
page is not committed, but the R2 Horizon `os::protect_memory` path rejects
the `committed=false` protection request. HotSpot then reports that it cannot
protect its protection page. The crash-report path also faults while printing
the heap: relative PC
`0x3f8be0` resolves to `DefNewGeneration::print_on` in that secondary report
path. That print fault is not the initiating protection failure.

The R3 uncommitted guard-page and early fatal-heap reporting fixes are
implemented, and 21 runtime host tests pass. The genuine target build is still
running. These fixes have not been validated on this hardware; no R3 device pass
is claimed here.

## Focused retest

For the next hardware session, run private-memory `zero-services-test` L first
and export its log. Then close the app, launch a fresh `java25-test` process,
and make one A attempt. If it prints `Hello from Horizon Java 25`, continue in
fresh processes with standalone X, then Y after X passes, followed by combined
`switchcraft-java25` A and—only after combined A prints Hello—combined X. If A
fails before Hello, stop the Java 25 sequence and do not repeat the VM attempt.
Record the R3 package manifest revision and environment versions with the new
logs. These steps are a retest plan, not a report of R3 results.

## Runtime reproduction and acceptance

The reviewed failing source is runtime commit
`bff51f38e218af147ca0c9f255fcf1b43be72daf`. Build it with
`bash scripts/build-openjdk25-runtime.sh --with-instrument`, copy its complete
SD tree, use application-mode hbmenu and select standalone Java25 A. The
uploaded matching NRO hash and exact native/VM logs above identify the physical
reproducer without game assets or credentials. The original Hello request is
`-Xmx128M`; loader heap override takes precedence over the configured 256 MiB
newlib arena. The 4 MiB worker stack and one-VM-per-process policy are unchanged.

For R3, run `make host-test`, the genuine instrument-enabled build, class-library
host controls, and `scripts/verify-package.py`. The new C++ wrapper regression
calls the actual maintained `os::protect_memory` with `committed=false` and
verifies host guard faults, unchanged committed state and recommit access.
The source patch regression checks zero-fuzz reconstruction and guarded early
heap/GC printers. These checks cannot establish Horizon Java execution.
Physical acceptance requires the extended L probe and actual Hello output;
JNI/FFM and startup-agent workloads remain separate later tests. If another
startup fatal occurs, its primary error and report footer must survive without
the previous `DefNewGeneration::print_on` secondary fault.

The fresh-cache reproduction also exposed a build-order prerequisite: host FFM
controls read the pinned OpenJDK source before the configure step. `make host-test`
now prepares the verified source first, so both documented build scripts can
run their host checks before a VM configure without relying on an old source
cache. The original empty-cache host invocation failed with the missing-source
message; source preparation followed by all 21 tests passed. This build-order
fix changes no VM/native archive inputs or SDK ABI.
