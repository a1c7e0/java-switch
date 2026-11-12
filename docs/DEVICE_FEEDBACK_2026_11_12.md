# Switch device feedback — 2026-11-12

This report records physical Switch 1 feedback for the R3 test package. It
updates the earlier R3 status using the supplied logs and the accompanying
operator observations. The evidence archive itself is preserved under
[`docs/evidence/device-2026-11-12/`](evidence/device-2026-11-12/).

## Device and build identification

- Device: Switch 1, application-mode homebrew.
- Firmware: 21.2.0, reported by the diagnostic NRO.
- Atmosphere and hbmenu versions: not recorded.
- Runtime source: `4d362d4830cfa4e88313dadfcd3a865e826609a5`.
- Switchcraft executable/source checkpoint: `f715f05ac90080a795f78382ba68e5bb03318e0d`, as recorded by the two other NRO logs.
- Uploaded archive: `log-r3.zip`, SHA-256 `55ea652dd0febf196964185f1a6ce07fcca65edcfd2f39fe830960abfb9cbc6e`.

The raw log files are unmodified copies from the uploaded ZIP. Individual SHA-256
hashes are listed in `SHA256SUMS` in the evidence directory. Log timestamps are
preserved as device ticks; no conversion to wall-clock seconds is asserted.

## Java 25 startup and execution

The standalone Java 25 diagnostic recorded `jni-create-entry`,
`jni-create-return`, then `vm-ready` for the selected attempt. The SDK's
`vm-ready` hook was reached. This is direct evidence that
`JNI_CreateJavaVM` returned and the VM entered Java execution.

The `classfile-69-Hello` attempt then failed at main-class loading with status
`-103` (`main-class`). Its stderr contains
`NoClassDefFoundError: HelloHorizon25` caused by
`ClassNotFoundException: HelloHorizon25`. The actual OpenJDK `java.base`
`BuiltinClassLoader` and `ClassLoader` stack frames show Java library execution;
Hello itself did not execute and did not print its success message.

The `Java25Smoke` attempt likewise reached `vm-ready`, but its native SDK
callback could not find `org/javaswitch/test/Java25Smoke`. The diagnostic
reported status `-103` at `vm-ready-hook`; stderr records the corresponding
`NoClassDefFoundError` and `ClassNotFoundException`. The Java25Smoke suite did
not run. This is startup and Java library execution evidence, not a JNI or FFM
suite pass.

The startup-agent (`Y`) attempt stopped after `jni-create-entry`. Its VM stderr
reports `ClassNotFoundException: AgentSmokeAgent`, followed by
`java.instrument` assertion failures and a fatal error processing `-javaagent`.
No premain, JVMTI transformation, or agent test passed. The NRO's log has no
`vm-ready` marker for this attempt.

No Hello success output, Java25Smoke suite pass, JNI/FFM application smoke
pass, startup-agent transformation pass, or game startup was established. The
standalone diagnostic displayed its own clean application exit after each
attempt; that does not change the failed/missing Java test outcomes.

## Other application observations

- `minecraft-launcher` opened in application mode and recorded a clean exit.
  No functional launcher tests were established by its log.
- The combined `switchcraft-java25` app opened and recorded a clean exit without
  selecting or starting a VM. This provides no combined VM-startup result.
- The `zero-services-test` app could not be launched. No execution log was
  produced, so there is no result to classify for that test.
- No Atmosphere/hbmenu versions were captured.

## Results by scope

| Area | Device result |
| --- | --- |
| Diagnostic native preflight | 4 MiB allocation/write/read/free passed; process and allocator figures are recorded in the raw log. |
| Java VM creation | `JNI_CreateJavaVM` returned; SDK `vm-ready` hook was reached for Hello and Java25Smoke selections. |
| Java library execution | `java.base` class loading and missing-class exception paths executed. |
| Hello diagnostic | Did not pass; `HelloHorizon25` was not found. |
| Java25Smoke suite | Did not pass or run; the requested class was not found. |
| Startup Java agent / JVMTI | Failed at missing `AgentSmokeAgent`; no transformation result. |
| JNI/FFM application behavior | NOT TESTED. |
| Zero-services test | No execution; app could not be launched and no log exists. |
| Combined launcher VM path | NOT TESTED; app exited without VM selection. |
| Minecraft title / renderer / native game libraries | NOT TESTED. |

Successful VM creation and Java class-loading activity are meaningful progress,
but this feedback does not establish a working Java application or a playable
title. The missing main/agent classes need investigation in the package and
runtime integration. Preserve the rule to make no R4 pass claim until new
physical evidence supports one.
