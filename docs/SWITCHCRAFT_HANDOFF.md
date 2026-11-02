# Switchcraft runtime handoff

Published runtime/SDK source pin on `a1c7e0/java-switch`, branch `main`:
**`aea0bab1cf18d4948650dffbfff64766298c2737`**.
Later documentation/example commits may accompany the package; use the full
`source_revision` in `dist/manifest.json` for exact package reproduction. The
final packaging run is performed from a clean committed tree. Never pin a
moving branch in the downstream lock. No Switchcraft source or lock was edited.

## Availability and ABI contract

`include/javaswitch/runtime.h` remains byte-for-byte unchanged from the incoming
baseline. **JS_ABI_VERSION = 1**. Existing structures, entry points and codes
remain: `js_runtime_query`, `js_runtime_launch`,
`js_runtime_launch_with_options`, `JsRuntimeInfo`, `JsLaunchRequest`,
`JsLaunchResult`, JS_OK (0), JS_UNAVAILABLE (-100), JS_INCOMPATIBLE (-101),
JS_BAD_ARGUMENT (-102), and JS_JAVA_EXCEPTION (-103).

| Backend | Reported Java major | Availability / JNI |
| --- | --- | --- |
| `minijvm8` | 8 | Available Java8 diagnostic; custom native registry, not standard JNI Invocation |
| `openjdk25` | 25 | Unavailable in the supplied Horizon SDK; no genuine VM symbol is linked |

`query("openjdk25")` returns JS_OK to describe the backend with `available=0`
and `standard_jni=0`. Launch returns JS_UNAVAILABLE with
`stage="runtime-not-linked"`. A Java25 request against miniJVM returns
JS_INCOMPATIBLE; there is no Java8 fallback or classfile-version spoofing.
The loader itself now rejects versions above 52.0, including direct JAR/native
defineClass routes. Parser rejection is logged and propagated through the
existing NoClassDefFoundError path with safe cleanup, rather than promising
exact UnsupportedClassVersionError compatibility.

Registration strategy: `src/runtime_openjdk.c` has a **weak** standard
`JNI_CreateJavaVM` import. A future real static VM must supply that symbol;
when linking an archive the consumer must force extraction, for example
`-Wl,-u,JNI_CreateJavaVM` plus the genuine VM archive. Weak references alone do
not extract archive members. No dummy provider is supplied. Symbol presence
makes query report available; launch then checks actual VM initialization and
the real `java.specification.version` (exactly 25). Availability is a linkage
observation, not proof that a VM initialized or ran bytecode.

The additive `include/javaswitch/vmem.h` extension preserves previous enum values
and whole-region calls. It adds aligned reservation, size query, page-range
commit/uncommit, NONE/R/RW protection and an explicit UNSUPPORTED result for
executable protection. These experiments are not advertised as complete mmap or
HotSpot memory services; partial reservation release remains absent.

## Artifacts and linking

| Path | Actual contents |
| --- | --- |
| `dist/java-test.nro` | Preserved Java8 miniJVM diagnostic |
| `dist/ffi-test.nro` | AAPCS64 integer/FP downcalls and closures; whole/partial VM probes |
| `dist/zero-services-test.nro` | Real OpenJDK25 port time/semaphore/atomic components; **no Java VM** |
| `dist/javaswitch-horizon-sdk.tar.gz` | `include/javaswitch/{runtime,vmem}.h`, ffi headers, `lib/libjavaswitch.a`, runtime contract and notices |
| `build/sdk/libjavaswitch.a` | AArch64 SDK, miniJVM C, libffi and native memory/closure helpers; no OpenJDK VM |
| `build/openjdk25-horizon/components/*.o` | Experimental HotSpot service/utility objects only; not included as a VM in the SDK |
| `dist/java-switch-source.tar.gz` | Corresponding repository, miniJVM/libffi source and complete pinned OpenJDK archive |
| `dist/COPY_TO_SD/`, `dist/java-switch-test-package.zip` | Installable diagnostics, JAR tests, source and notices |
| `dist/manifest.json`, `dist/SHA256SUMS` | Source revision, truthful verification status and complete SHA256 values |

Raw SDK archive SHA256:
`ccfaee3eeff02df2906bb8dd1cc1dc2214acb6b7f79eed4ce20777447da3108d`.
The final NRO/package hashes are recorded in `dist/SHA256SUMS`; the probe's
input/ELF/NRO hashes are also retained in `docs/evidence/openjdk25-probe.json`.
There is **no** `java25-test.nro`, usable `libjvm.a`, or Java25 runtime image.

Use devkitA64 GCC 15.2.0 with the locked devkitPro image. Extract the SDK and
compile with its `include/` directory plus libnx headers. Baseline flags:

```sh
-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE -D__SWITCH__
-specs=$DEVKITPRO/libnx/switch.specs -L$DEVKITPRO/libnx/lib
-L/path/to/sdk/lib -Wl,--start-group -ljavaswitch -lnx -lm -Wl,--end-group
```

Keep objects before libraries. The SDK is C; a C++ launcher may use the C++
driver. The experimental HotSpot component build additionally uses
`-mno-outline-atomics`; it uses upstream JDK25 headers, not host Linux VM code.
`scripts/openjdk25.py` stages the actual libnx W^X libffi archive/allocator for
configure. Do not link libffi's intermediate generic ELF closure allocator or
label Linux Temurin's `libjvm.so` as a Horizon provider.

## Minimal consumer example

The complete compile-checked C example is `examples/sdk_java25.c`. Its core is:

```c
JsRuntimeInfo info;
js_runtime_query("openjdk25", &info); /* available == 0 in this SDK */
JsLaunchRequest q = {
    .abi_version = JS_ABI_VERSION, .required_java_major = 25,
    .backend = "openjdk25", .heap_mib = 256,
    .classpath = "sdmc:/switch/mc-runtime/tests/java25-hello.jar",
    .main_class = "HelloHorizon25"
};
JsLaunchResult r;
int code = js_runtime_launch(&q, &r); /* JS_UNAVAILABLE, runtime-not-linked */
```

The example process returns success only when its expected unavailable result
was observed; it never reports that Java executed. The JAR is a real classfile69
Hello World with a manifest, compiled by pinned host Java25. Its text was run
only on the Linux reference VM. The SDK requires an explicit slash-separated
main-class name and does not implement JAR manifest discovery.

## Runtime image, options and native methods

No Java25 image or implicit image-path resolver is implemented. Reserve the
future SD location `sdmc:/switch/mc-runtime/runtimes/openjdk25/` only after a real
VM/class library defines its layout; the SDK does not create or interpret that
directory today. miniJVM continues to use
`sdmc:/switch/mc-runtime/runtimes/minijvm/minijvm_rt.jar`. Test JARs/logs remain
under the shared `switch/mc-runtime/{tests,logs}` root.

The contract specifies `;` as Horizon's classpath separator to avoid the `sdmc:`
device-prefix colon. miniJVM consumes this; the adapter forwards the string
unchanged. A future Java25 class library must implement matching properties and
path parsing before multiple SD classpath entries are supported. Host tests use
their Linux paths. `boot_classpath` is used by miniJVM; the Java25 adapter does
not currently apply it as an image/boot override.

The adapter supplies `-Djava.class.path=...`, `-Xmx<heap_mib>M` and
`--enable-native-access=ALL-UNNAMED`, then forwards up to 128 extra `-` options
through `JavaVMOption` with `ignoreUnrecognized=JNI_FALSE`. Accepted `-X` options
are determined by the actual linked VM, not emulated by the SDK. The heap field
is validated at 32..1024 MiB; it is not a guarantee that process memory exists.
Java heap, VM metadata, native stacks, FFI backing and page-state metadata must
fit the application's real memory budget, which hardware logs must establish.

For statically linked custom natives, use `on_vm_ready` and the actual `JNIEnv`
to call `RegisterNatives` on real Java methods, as in `tests/jni_host.c`. Those
function addresses must remain live through VM execution. There is no automatic
Java25 built-in native library/symbol registry or general Horizon dlopen/dlsym
implementation yet. `JNI_OnLoad_*`, java.base's built-ins and fallbackLinker
static lookup still require integration. No LWJGL symbols are silently supplied.

After successful VM creation, exceptions and failures reach `DestroyJavaVM`.
One successfully created VM is allowed per process; relaunching in the same
process returns `JVM-reinitialization-unsupported`, including after destroy.
Callers must serialize invocation/lifetime; concurrent launches are not an SDK
guarantee. Unknown native thread/safepoint cleanup is not hidden by a success
return. No VM was created on Horizon in this delivery.

## Tests and next source work

`bash scripts/build-all.sh` passes cross-build, six host tests, NRO architecture
and container checks. Additional commands:

```sh
python3 scripts/openjdk25.py configure   # PASS: horizon-aarch64/newlib/Zero
python3 scripts/openjdk25.py build       # BLOCKED: mutex_horizon.hpp
python3 scripts/openjdk25.py components  # CROSS-BUILT genuine components
python3 scripts/openjdk25.py test-host   # HOST-PASSED, mocked clocks + real host atomics
python3 scripts/openjdk25.py probe       # CROSS-BUILT service NRO, no VM
python3 scripts/verify-package.py        # bytes/headers/notices/source reconstruction
```

See `docs/evidence/`, `docs/PRIOR_ART_AUDIT.md`, `docs/HORIZON_SERVICES.md` and
`docs/HARDWARE_TEST_MATRIX.md`. Physical Switch execution is **NOT TESTED** for
all NROs. Host Java25 JNI and FFM down/upcalls pass; JNI/FFM **on Horizon remain
BLOCKED**, as do classfile69 execution, JVMTI/instrumentation/retransformation,
Java25 NIO/networking and the complete class library.

Next high-value file: `src/hotspot/os/horizon/mutex_horizon.hpp` in the overlay,
implementing `PlatformMutex` / `PlatformMonitor` with the opaque C bridge.
libnx `condvarWaitTimeout` returns raw timeout `0xEA01`; preserve real error
semantics and reacquisition rules, then test contention/timeouts/failure paths.
Follow with OSThread/Parker/TLS/stack bounds, safepoints, recoverable fault design,
real virtual-memory binding and java.base native/image initialization. Returning
from libnx's ordinary exception callback breaks the process; it is not a
resumable POSIX signal handler. Do not skip these services to manufacture a VM.

Downstream request: retain the ABI v1 availability gate and pin this repository's
full SHA. Defer Java25/LWJGL execution until an actual VM/image arrives. Identify
required LWJGL FFM symbols and owning static libraries, then supply an explicit
native-symbol contract; SDL3/graphics/launcher work remains in Switchcraft.
