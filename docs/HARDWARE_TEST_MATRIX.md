# Physical Switch 1 test matrix

Current status for every row: **SWITCH HARDWARE NOT TESTED**. Use Atmosphere and
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
| `java25-hello.jar` on Horizon | No supported launcher/VM exists yet; do not run it through miniJVM | No Java25 hardware log yet | Classfile69 IMPLEMENTED and HOST-PASSED only; Horizon execution BLOCKED |

Press + to exit. Relaunch each app and repeat once to check cleanup. The Y/B
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
