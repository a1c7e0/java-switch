# Horizon Java 25 class library

The genuine upstream `java.base-java`, copy/gendata and static-native-library
targets build with the tracked Horizon port. The minimal image contains 7,325
classfile-69 classes, generated security configuration, timezone/currency data,
CA roots and notices. Host jmod/jlink/jimage are build/format tools. The image
has no host native VM or shared library. Switch startup and execution are
NOT TESTED.

## Platform behavior

| Area | Implementation | Explicit limitation |
| --- | --- | --- |
| Platform identity | Distinct HORIZON enum and `os.name=Horizon`, target AArch64, UTF-8 | Unqueried firmware is `unknown`; user name `?` and SD application home are explicit bootstrap defaults |
| Paths | `sdmc:/` roots in java.io/NIO, semicolon classpaths, actual newlib getcwd | Other device-root Java path handling needs separate validation |
| Files | Real open/read/write/seek/stat/directory/truncate/sync and cursor-preserving positional/vector I/O | mmap, file locks, at-family, owners/xattrs, symlinks and direct I/O are unavailable |
| Shared file cursors | libjava-owned libnx monitor used by java.io and NIO | Native code bypassing these adapters must coordinate shared FD use |
| Image reading | Read-only Alias-backed index snapshot, actual offset reads and ZIP_InflateFully | Runtime image must remain immutable while open; no general file mmap |
| JNI libraries | Immutable symbol tables generated from actual target archives | Dynamic ELF loading is unavailable; unknown modules/symbols fail |
| Network | Real libnx BSD initialization, socket/DNS operations, stream/datagram vectors | IPv6 transport, interface enumeration/details and reachability probes are unavailable |
| Selector wakeup | Actual checked TCP loopback pair, nonblocking poll/write/drain | Kernel socket permissions and loopback operation need hardware tests |
| Native I/O interruption | NativeThread reports signaling unsupported | Blocking I/O cannot be woken by a POSIX signal; close/interruption needs further work |
| Random seed | Real checked CSRNG IPC, private temporary buffer, raw Result in Java failure | Service availability/entropy execution unverified on Switch |
| FFM | Horizon selects configured libffi fallback; actual native layouts and bounded libc/libm catalogue | Downcalls/upcalls and W^X permissions need physical validation |
| Processes / Unix facilities | Process creation, Unix-domain sockets and absent terminal operations fail explicitly | No fork/exec, Unix identity or signal success is invented |
| Instrumentation | Separate Horizon path/encoding adapter sources prepared | JVMTI/module/static-agent build and execution still required |

The SDK/native resolver registration remains immutable for process lifetime.
The standard module keys are `java`, `zip`, `jimage`, `verify`, `net`, `nio`,
`fallbackLinker` and `syslookup`. The last contains only explicitly selected real
C and math exports, including strlen; the generator checks each definition.
libnx must precede newlib archives at final link to retain its actual fcntl
implementation. Native symbol lookup is not Java RegisterNatives: the smoke
program separately registers and calls its JNI method through the SDK hook.

The `java25-test.nro` worker has a 4 MiB stack. Its native heap policy leaves
physical budget for VM Alias mappings. The SDK supports one successful VM per
process, and a static native provider must be installed before VM creation.

## Reproduction and evidence

```sh
python3 scripts/openjdk25.py java-base
python3 scripts/openjdk25.py native-base
python3 scripts/build-openjdk25-image.py
python3 scripts/openjdk25.py link-runtime
python3 scripts/test-openjdk25-classlib-host.py
```

The helper checks exact upstream archive hashes, applies zero-fuzz patches and
records real build commands. Horizon archive rebuilds track the complete object
set and recreate the archive so excluded Unix members cannot survive `ar -r`.
Logs and hash inventories are under `build/openjdk25-*`; published snapshots
are in `docs/evidence/`. Host tests exercise actual maintained source using
Linux syscalls and explicitly mocked JNI/Horizon services. They establish no
physical-device behavior.

Horizon normal/emergency VM exits use actual svcExitProcess, with normal log
flushing and an emergency path that avoids stdio locks. Ordinary NRO libc exit
returns through a loader and is not equivalent to terminating VM workers.
The native services and completed VM diagnostics therefore end the application
process rather than leave workers executing code that a loader may unmap.
