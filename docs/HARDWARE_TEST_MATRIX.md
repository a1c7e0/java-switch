# Physical Switch 1 test matrix

The new R3 binaries below are **SWITCH HARDWARE NOT TESTED**. Historical R2
logs establish Java8 diagnostic and private memory L passes, and Java25 startup
failure at heap initialization; see [the evidence](DEVICE_FEEDBACK_2026_11_09.md).
Use Atmosphere and
application-mode hbmenu. The package contains diagnostics and source, no game
assets or credentials. Copy `dist/COPY_TO_SD/` to the SD root.

| Artifact / action | Expected observation | Log on SD | Current evidence |
| --- | --- | --- | --- |
| `java-test.nro`, A | Bundled Java8 JAR: class loading, reflection, exceptions, threads, files, allocation and registered native | `switch/mc-runtime/logs/java-test.log` | IMPLEMENTED, CROSS-BUILT; equivalent interpreter workload HOST-PASSED |
| `java-test.nro`, X | Trusted optional `UserMain` Java8 JAR from `switch/mc-runtime/tests/user.jar` | Same | IMPLEMENTED, CROSS-BUILT; not a Java25 launcher |
| `java-test.nro`, X, trusted `UserMain` compiled for Java25 | Version 69.0 rejection before main executes, followed by failed launch and clean teardown | Same | Loader guard IMPLEMENTED, CROSS-BUILT, real JAR failures HOST-PASSED |
| `ffi-test.nro`, A | Integer downcall 42; mixed integer/FP downcall 41.5 | `switch/mc-runtime/logs/ffi-test.log` | IMPLEMENTED, CROSS-BUILT; AAPCS64 execution unverified |
| `ffi-test.nro`, X | Integer closure 42; mixed integer/FP closure 41.5, then successful close; or exact JIT allocation/transition Result | Same | IMPLEMENTED, CROSS-BUILT; lifecycle/error mocks HOST-PASSED |
| `ffi-test.nro`, Y | 64 KiB reserve/commit, edge readback, uncommit/recommit/release; or exact raw syscall Result | Same | IMPLEMENTED, CROSS-BUILT; state/error mocks HOST-PASSED |
| `ffi-test.nro`, B | 64 KiB-aligned reservation; middle two pages mapped; R/NONE/RW Results; first mapped page uncommit/recommit; release | Same | IMPLEMENTED, CROSS-BUILT; range/error/concurrency mocks and ASan/UBSan HOST-PASSED |
| `zero-services-test.nro`, A | Actual HotSpot clocks, semaphore token accounting, sequential Atomics, monitor timeout/reacquisition and coalesced event permits | `switch/mc-runtime/logs/zero-services-test.log` | IMPLEMENTED, CROSS-BUILT; clock/monitor/permit mocks and four-thread host Atomics HOST-PASSED |
| `zero-services-test.nro`, X | Real libnx worker ID/stack bounds, compiler/library TLS isolation, 64 permit exchanges, checked join/close; failed cleanup retains state for retry | Same | IMPLEMENTED, CROSS-BUILT; thread/TLS failure/state/isolation mocks HOST-PASSED |
| `zero-services-test.nro`, Y | Confirm address zero unreadable, valid SafeFetch32/N loads, then actual faulting reads returning -12345/-67890; may abort or stall if recovery fails | Same; retain any crash report | EXPERIMENTAL, CROSS-BUILT; 60 ARM64 instruction cases EMULATOR-PASSED with mocked kernel transfer/return |
| `zero-services-test.nro`, B | Worker retires while still alive; reaper joins only after exit, then closes; pending cleanup can be retried | Same | IMPLEMENTED, CROSS-BUILT, reaper/start-gate failure tests HOST-PASSED |
| `zero-services-test.nro`, L | Actual private reserve, uncommitted no-access guard and empty committed-range query, then commit/protect/recommit/query; release middle page; retained canaries and SDK/VM reservation separation | Same | IMPLEMENTED, CROSS-BUILT, shared-ownership/error tests HOST-PASSED |
| `java25-test.nro`, A | Real VM prints Hello from Horizon Java 25 and Java major 25; status JS_OK / completed | `java25-test.log`, `java25-vm-stdout.log`, `java25-vm-stderr.log` in the same logs directory | Genuine VM/provider/image CROSS-BUILT; startup/classfile69 execution NOT TESTED |
| Fresh `java25-test.nro`, X | Actual JNI nativeAdd=42, record/reflection, threads, NIO/TCP/selector, FFM strlen=10, pow=32, integer upcall=42 and mixed integer/FP down/upcall=14.75; final JAVA25 JNI FFM SUITE PASS | All three Java25 logs | Workloads IMPLEMENTED, host reference PASS; execution on Horizon NOT TESTED |
| Fresh instrument-profile `java25-test.nro`, Y | Standard -javaagent runs premain once, transforms AgentSmoke before definition with the Class-File API, forwards arguments/property and prints JAVA25_AGENT_SMOKE_PASS | All three Java25 logs | Genuine JVMTI/libinstrument/image/NRO CROSS-BUILT; positive and no-agent negative controls HOST-PASSED; execution on Horizon NOT TESTED |

For the focused R3 retest, run **L first**, then **one fresh Java25 A**. Only
if A prints Hello, continue with X and then Y, each in a fresh process. Stop
dependent Java tests after a failure and retain every log and hs_err report.
Press + to exit. Repeat only native probes when checking cleanup. The Y/B
actions in ffi-test retain failed-release handles; another Y/B retries release
before making another reservation. The ffi-test actions never deliberately
touch unmapped/NONE pages. The separate experimental zero-services Y action
deliberately faults: run A and X first, then Y, and X again only if Y returns.

Return complete logs from fresh launches, including firmware, applet type, raw
Result codes, all PASS/FAIL lines, and any crash report. Note if X or B does not
return. Do not change kernel permissions to turn a reported failure into a pass.
An accepted syscall and successful readback are separate observations; retain
both. Host mock success does not predict that physical-memory or JIT permissions
are available to this process.

The service probe does not test semaphore contention, JavaThread safepoints,
general Java exceptions, JNI or Java FFM. Native permit contention and narrow
SafeFetch recovery have dedicated X/Y actions; all remain hardware NOT TESTED.
A successful native closure is a libffi prerequisite,
not evidence for Java FFM. No row can be promoted to SWITCH-HARDWARE-PASSED until
physical-device logs are reviewed.


## Recommended first device session

1. Verify package checksums and copy the entire COPY_TO_SD tree, including
   runtimes/openjdk25/lib/modules, configuration and generated data. Keep the
   bundled source/notices with the binary package. Use application-mode hbmenu.
2. Run Java8 A, FFI A/X/Y/B and native services A/X/B/L first. Retain every raw
   Result, including a permission/resource failure. These establish the unsafe
   prerequisites before entering the real VM.
3. Run native services Y only after the ordinary probes. It deliberately faults
   and may abort/stall. If it returns, run X again; then relaunch the application.
4. After L passes, launch java25-test in a fresh process and select A. The VM runs on a 4 MiB worker and pauses UI
   updates while joined. Its stdout/stderr go to SD; an unchanged screen is not
   proof of success. On return use +, then relaunch before selecting X.
5. Select X only after A prints Hello and only in the new process. Keep all three complete logs, including the
   final suite marker or the first exception/fatal error. A successful VM cannot
   be initialized again in the same process.
6. Only after X passes, with the instrument-profile package, relaunch the application in a fresh
   process and select Y. Confirm the agent and app JARs are installed in tests/
   and java.instrument is in the image. Keep the premain/transform observations,
   actual capability flags and final JAVA25_AGENT_SMOKE_PASS or first failure.
   No agent mode can be selected a second time in an existing VM process.
7. If startup stalls, preserve the logs after closing the application through
   the system UI. Include the last button/action, approximate wait time,
   firmware/Atmosphere version, application/applet mode and any crash report.
   Do not change syscall privileges or heap policies to hide a failing result.

The expected clean VM log is actual Java output plus SDK stage completed; VM
linking alone cannot satisfy that row. The optional current image contains
java.base and java.instrument; it is not the Minecraft launcher. Minecraft/title
validation follows real runtime startup and the downstream native-binding and
truthful Horizon platform adaptation milestones.

Native services and completed Java25 sessions close the entire application
process on +. libnx's ordinary NRO exit can return to a loader while VM/reaper
threads remain resident. Reopen application-mode hbmenu in a fresh process
before the next session; do not unload a VM session back into the loader.
