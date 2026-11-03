# Switchcraft runtime handoff

Repository: `a1c7e0/java-switch`, branch `main`. The native VM implementation pin is
`1427f6d05080ce0280373006ac04b99c8061c246`; `dist/manifest.json` records
the exact full package source revision, including later documentation/verifier fixes.
Pin an immutable SHA downstream.
No Switchcraft source or dependency lock was edited.

## Verified build and remaining runtime evidence

A real OpenJDK25 Zero archive (553 ARM64 objects), eight native java.base
archives, a static native resolver and a Horizon PIE/NRO now build. The minimal
java.base image contains target-built classfile-69 classes and generated data.
`java25-test.nro` A attempts Hello; X attempts actual JNI registration, threads,
NIO, FFM downcall and upcall. **Switch VM startup, Java execution and every
hardware workload are NOT TESTED. Minecraft/title execution is not established.**

The normal SDK remains VM-free. `include/javaswitch/runtime.h` is byte-identical
to the incoming ABI 1 header: all existing structures, entry points and status
codes remain. `native_resolver.h` is an additive, separately versioned extension;
see `docs/NATIVE_RESOLVER.md`.

| Link contents | openjdk25 query | Launch behavior |
| --- | --- | --- |
| Baseline libjavaswitch.a alone | Java major 25, available=0, standard_jni=0 | JS_UNAVAILABLE / runtime-not-linked |
| Actual VM archive forced into executable | available=1, standard_jni=1 | Attempts genuine JNI_CreateJavaVM; actual initialization/version/errors decide success |
| minijvm8 | Java major 8 diagnostic | Rejects Java25 requests; actual loader rejects classfile69 |

Query availability is symbol presence, not proof of startup. The SDK checks the
real `java.specification.version` after successful creation and supports one
successful VM per process. DestroyJavaVM does not make NRO code safe to unload;
terminate the Horizon application process after a VM session. Native provider callbacks, context and function
addresses must remain resident. Installing/sealing the provider does not execute
Java or imply that an optional native module exists.

## Artifacts and static integration

| Path | Contents |
| --- | --- |
| dist/java-test.nro | Preserved Java8 diagnostic |
| dist/ffi-test.nro | Native integer/FP downcalls, W^X closures, public memory probes |
| dist/zero-services-test.nro | Actual HotSpot service probes, no VM; A/X/Y/B/L actions |
| dist/java25-test.nro | Genuine experimental Zero VM and native providers; startup/execution unverified |
| dist/javaswitch-horizon-sdk.tar.gz | Baseline SDK ABI 1, public headers and libffi, no VM |
| dist/openjdk25-horizon-runtime.tar.gz | Actual libjvm.a, eight native archives, generated resolver, JNI headers, image and notices |
| dist/java-switch-source.tar.gz | Complete tracked source and full pinned OpenJDK archive plus miniJVM/libffi source |
| dist/COPY_TO_SD and test-package ZIP | Complete safely installable diagnostics/image/JAR/source tree |
| dist/manifest.json and SHA256SUMS | Exact source revision and generated artifact hashes |

The runtime integration archive depends on the baseline SDK. Compile
`native/native_resolver.c` with the provided headers. Its immutable exported
`const JsNativeResolver js_openjdk25_native_resolver` is generated from actual
archive definitions, never a JNI success stub. Include its object before libs.
Use the real devkitA64/libnx flags:

```sh
-march=armv8-a+crc+crypto -mtune=cortex-a57 -mno-outline-atomics -mtp=soft -fPIE
-specs=$DEVKITPRO/libnx/switch.specs -L$DEVKITPRO/libnx/lib
-Wl,--gc-sections -Wl,-u,JNI_CreateJavaVM
-Wl,--start-group libjava.a libzip.a libjimage.a libverify.a libnet.a libnio.a \
  libfallbackLinker.a libsyslookup.a libjvm.a libjavaswitch.a -lnx -lc -lm \
-Wl,--end-group
```

Use the C++ driver. Keep libnx before libc to retain libnx's actual fcntl.
The exact tested invocation and all archive hashes are recorded in
`build/openjdk25-runtime-link.log/json` and the integration manifest. No host VM
or dynamic native library belongs in this image. JNI header paths are
`include/` and `include/horizon/`.

```c
#include <javaswitch/runtime.h>
#include <javaswitch/native_resolver.h>
extern const JsNativeResolver js_openjdk25_native_resolver;

int launch_java25(void) {
    int status = js_native_resolver_install(&js_openjdk25_native_resolver);
    if (status != JS_OK) return status;
    JsLaunchRequest request = {
        .abi_version = JS_ABI_VERSION, .required_java_major = 25,
        .backend = "openjdk25", .heap_mib = 128,
        .classpath = "sdmc:/switch/mc-runtime/tests/java25-hello.jar",
        .main_class = "HelloHorizon25"
    };
    JsLaunchResult result = {0};
    return js_runtime_launch(&request, &result);
}
```

Call from a sufficiently sized real native stack; the diagnostic uses a checked
4 MiB libnx worker. Native libnx heap reservation must leave physical budget for
Alias mappings: its zero default normally commits almost all available memory.
The diagnostic uses `__nx_heap_size=256 MiB`; a loader heap override wins. This
is a smoke setting, not a Minecraft memory recommendation. Log process memory,
heap override and raw Results before diagnosing a VM allocation failure.

## SD layout and platform services

Default JAVA_HOME is `sdmc:/switch/mc-runtime/runtimes/openjdk25`; TMPDIR defaults
to `sdmc:/switch/mc-runtime/tmp`. Explicit environment values are respected.
The image's lib/modules, configuration, generated data and legal files must all
be copied. Classpath separation is `;` so device colons remain intact. Java
reports Horizon, AArch64 and UTF-8. Java and native path handling retain sdmc:/.

The SDK ready hook supports real RegisterNatives. The static module keys are
java, zip, jimage, verify, net, nio, fallbackLinker and syslookup. The syslookup
catalogue contains only selected real target libc/libm exports; missing symbols
return unavailable. Horizon uses the configured libffi fallback and its actual
AAPCS64/native sizes. The test image does not have java.instrument yet;
JVMTI, standard -javaagent and retransformation integration remain work.

Unsupported operations fail explicitly: fork/exec, dynamic ELF loading, Unix
domain sockets, file mappings/locks, unavailable filesystem attributes, process
signals and native I/O signaling. Socket/interface enumeration and reachability
also remain incomplete. Normal file I/O, IPv4 BSD sockets/DNS and selector wakeup
have native code and host tests; hardware behavior is unverified. See
`docs/OPENJDK25_CLASSLIB.md` and `docs/HORIZON_SERVICES.md`.

The private memory bridge implements partial release and real Alias selection.
It shares reservation ownership with the public SDK and never returns an
executable mapping. libffi closures use the distinct libnx JIT allocator.
SafeFetch recovery remains restricted to two exact load PCs; unmatched faults
retain the original fatal path. Hardware kernel return/permissions are unknown.

## Validation and downstream requests

Run the ordinary build, then the experimental commands in
`ports/openjdk25/README.md`. Evidence separates host JNI tests, mocked Horizon
services, ARM64 emulation, genuine cross-linking and physical execution.
`docs/HARDWARE_TEST_MATRIX.md` gives exact buttons and SD log names. Full VM
stdout/stderr and the status log are required; retain any Atmosphere crash report,
firmware, application/applet mode and the last action when startup stalls.

Switchcraft should consume the pinned runtime package and compose its existing
SDL/Vulkan/native bindings with the generated JDK resolver before sealing it.
Preserve exact module keys and lifetime. Unknown LWJGL functions must stay
unavailable. Required additional native families include SDL/Vulkan and the
remaining VMA/shaderc/SPIRV-Cross/FreeType/OpenAL/STB/jemalloc/LWJGL bindings.
The strict Vulkan host trace also loads lwjgl_opengl incidentally; audit those
initialization bindings before omitting them. These native modules are owned by
the graphics/launcher task. Keep Horizon platform detection
truthful, install the complete image, and retain the ABI/version availability
gate. The next runtime tasks are JVMTI/java.instrument, actual hardware startup
and JNI/FFM logs, then the pinned Minecraft title workload through Switchcraft.
