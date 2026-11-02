# Horizon SafeFetch fault islands

Status: actual AArch64 assembly cross-built and instruction-emulated; Horizon
exception transfer and resume are **SWITCH HARDWARE NOT TESTED**. This is a
narrow native read primitive, not a general Java exception or safepoint handler.

OpenJDK25 Zero normally selects POSIX sigsetjmp/siglongjmp for SafeFetch.
`patches/openjdk25/0006-horizon-safefetch.patch` instead selects the Horizon
implementation. SafeFetch is also used by OopStorage/JNI handle classification;
returning a constant error or checking a mapping before reading is insufficient.

`safefetch_horizon.S` contains two real loads, for 32-bit and pointer-width
values. The early libnx exception entry accepts only descriptor 0x101, an AArch64
lower-EL read Data Abort, and a saved PC exactly equal to one of these loads.
It replaces saved x0 with the caller's fallback, advances the PC to that load's
RET, and calls the real libnx `svcReturnFromException`. All other frame fields,
x9..29 and vector registers are preserved. No process-wide jump buffer, fault
callback return or signal emulation is used. The public frame offsets and size
are compile-time asserted in `horizon_fault_abi.c`.

The entry retains libnx's GetInfo/debugger policy and its small native-stack
save sequence. Failed GetInfo or a debugged process without
`__nx_exception_ignoredebug` follows the original fatal path. Unmatched faults
also branch to `exception_fallback_horizon.S`: the pinned libnx exception source
with entry labels renamed, its abort label exported and its complete ISC notice
retained. That path logs
through the diagnostic callback, then calls svcBreak. It does not recover.
Source: switchbrew/libnx `b156b4774fb2df7b93402169509266da7547fc4c`,
`nx/source/runtime/exception.s`; see `licenses/LIBNX-LICENSE.md`.

This requires owning the strong `__libnx_exception_entry` override. A consumer
with its own strong exception entry must explicitly integrate the dispatcher;
it cannot link two independent definitions. These objects are private to the
experimental OpenJDK component/probe and do not alter SDK ABI v1 or the Java8
and FFI NROs. Keep fault/continuation labels as distinct assembly instructions;
do not replace them with compiler-generated loads or LTO-folded code.

## Reproduce the instruction tests

On Linux x86_64, after the ordinary toolchain bootstrap:

```sh
python3 scripts/bootstrap-openjdk-emulator.py
python3 scripts/test-openjdk25-faults.py
```

The optional Unicorn 2.1.4 wheel is SHA256-pinned in `dependencies.lock.json`
and installed only under `.deps/`. No emulator library enters the SDK or SD
package. The test links the actual assembly and real libnx SVC wrappers into an
AArch64 ELF. It executes valid reads, unmapped and protected reads, both return
widths, debugger/error cases, unmatched PC/ESR/descriptor/PSTATE and null-frame
dispatch. Sixty cases check saved-context changes and register preservation.
One ELF uses a boundary marker; another links the actual production fallback
and validates the exact dump, immediate query/debug-policy abort, callback and
svcBreak sequence. Test-only globals/callbacks/markers are outside
`ports/openjdk25/src/` and never enter a native artifact. Kernel frame delivery
and SVC return are mocked; a passing
instruction test does not establish Horizon behavior.

## Physical-device probe

Build `zero-services-test.nro` using `components` and `probe`. In application-mode
hbmenu, exercise A and X first, then Y. Y queries address zero only to confirm an
unreadable diagnostic target, checks valid loads, and attempts both real faulting
loads. Expected results are -12345 and -67890. Capture the complete SD log and
any crash report. An unsupported recovery path can abort or stall the process;
this action is explicitly marked experimental in the UI. Repeat X after a
successful Y and repeat after relaunch. No kernel permission change is needed.

Stack guard/overflow recovery, implicit Java null traps, thread suspension,
fault-driven safepoints, reentrant general exception dispatch and HotSpot OS
startup remain separate work. Their capabilities are not advertised by this
SafeFetch implementation. No Java VM, JNI or Java FFM has run on Horizon.
