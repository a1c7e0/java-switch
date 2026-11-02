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
| `zero-services-test.nro`, A | Actual HotSpot monotonic samples and wall clock; semaphore token accounting; sequential Atomic add/CAS/load | `switch/mc-runtime/logs/zero-services-test.log` | IMPLEMENTED, CROSS-BUILT; mocked clock/error and four-thread host Atomic tests HOST-PASSED |
| `java25-hello.jar` on Horizon | No supported launcher/VM exists yet; do not run it through miniJVM | No Java25 hardware log yet | Classfile69 IMPLEMENTED and HOST-PASSED only; Horizon execution BLOCKED |

Press + to exit. Relaunch each app and repeat once to check cleanup. The Y/B
probes retain failed-release handles; another Y/B retries release before making
another reservation. They never deliberately touch unmapped/NONE pages.

Return complete logs from fresh launches, including firmware, applet type, raw
Result codes, all PASS/FAIL lines, and any crash report. Note if X or B does not
return. Do not change kernel permissions to turn a reported failure into a pass.
An accepted syscall and successful readback are separate observations; retain
both. Host mock success does not predict that physical-memory or JIT permissions
are available to this process.

The service probe does not test semaphore contention, safepoints, faults, Java
exceptions, JNI or Java FFM. A successful native closure is a libffi prerequisite,
not evidence for Java FFM. No row can be promoted to SWITCH-HARDWARE-PASSED until
physical-device logs are reviewed.
