# Experimental OpenJDK 25 Horizon source overlay

Modified/created on 2026-11-02 and 2026-11-03. These files are copied into the patched upstream
tree by `scripts/openjdk25.py`; never maintain edits only in `.deps/`.
The source revision and archive hash remain in `dependencies.lock.json`.

New HotSpot port files are GPL-2.0-only as stated in their SPDX headers, except
the preserved libnx fatal-entry assembly which retains its ISC notice.
Upstream-derived files retain their copyright and license notices.
`licenses/openjdk25-LICENSE.txt` contains the upstream GPL/Classpath text; the
Classpath exception applies only to files designated by upstream, not to all
HotSpot code. The root project's MIT license does not replace these notices.

The overlay implements actual HotSpot wall/monotonic time functions,
a libnx counting semaphore, mutex/monitor and binary event permits, a real Parker
transition, native-thread primitives, library TLS, narrow SafeFetch fault islands
and AArch64 Zero ordering/atomic primitives.
The globals headers select initial stack/default flags; they do not initialize
a VM. `horizon_bridge.c` is compiled as C because HotSpot and libnx have
incompatible global `u8`, `s8`, `Handle` and `Mutex` type names. The bridge
exports standard-width types and opaque pointers. It makes no POSIX signal or
dynamic-loading claims. See `docs/OPENJDK25_SYNC.md` and
`docs/OPENJDK25_FAULTS.md` for contracts, exact build barriers and test limits.

Build and verify the complete optional instrument profile with
`bash scripts/build-openjdk25-runtime.sh --with-instrument`; omit the option
for the base profile. JVMTI/services, libinstrument and the manifest-parser subset
are genuine cross-built upstream components. Execution remains unverified.
See `docs/OPENJDK25_INSTRUMENTATION.md`. Individual base steps remain available:

```sh
python3 scripts/bootstrap-openjdk-tools.py
python3 scripts/openjdk25.py configure
python3 scripts/openjdk25.py build       # genuine upstream static VM archive
python3 scripts/openjdk25.py java-base   # target classes, configuration and data
python3 scripts/openjdk25.py native-base # eight native java.base archives
python3 scripts/build-openjdk25-image.py
python3 scripts/openjdk25.py link-runtime # real JNI VM/provider NRO
python3 scripts/test-openjdk25-classlib-host.py
python3 scripts/openjdk25.py components  # deliberately separate object build
python3 scripts/openjdk25.py test-host   # clock/sync/thread/TLS mocks
python3 scripts/openjdk25.py probe       # native services NRO, NOT a Java VM
python3 scripts/bootstrap-openjdk-emulator.py # optional Linux x86_64 tool
python3 scripts/test-openjdk25-faults.py # actual ARM64 code; mocked kernel
```

The component build reuses the real upstream compiler command recorded by the
upstream static build, removes the precompiled-header invocation, and compiles
the actual port services and upstream string utilities as AArch64 relocatable
objects. It also compiles the libnx C bridges and fault-entry assembly. The
diagnostic links time/synchronization/permit/native-thread/TLS/fault objects;
Parker is separately compiled because its JavaThread transitions need a real VM.
The atomic header is exercised by the diagnostic's C++ translation unit.
The separate full upstream target builds libjvm.a; linking a service component
alone is not VM execution.

The standalone diagnostic defines a fatal error sink that logs and aborts. This
is not the VM's exception dispatcher. It does not define `JNI_CreateJavaVM` and
the builder checks that the symbol is absent. Its UI and package manifest state
that it contains no VM. The diagnostic overrides libnx's weak 1 KiB exception
stack with a 64 KiB stack, matching the VM diagnostic, because its fatal path
calls the logging handler on that stack. The cross-link check rejects a return
to the weak default. After `sc_init`, the app records a `main` startup marker;
it cannot record a failure that happens before logging is initialized. The
separate `link-runtime` action builds
`java25-test.nro` with the genuine VM/provider. Startup and classfile execution
on Switch are NOT TESTED; the minimal runtime image must be installed too.

When this NRO is packaged, `java-switch-source.tar.gz` includes the pristine
OpenJDK archive as `third-party/openjdk25-upstream.tar.gz` plus the complete
tracked repository, patches, overlay, build scripts and notices. To reconstruct,
extract the repository source, copy that upstream archive to
`java-switch/.deps/openjdk25.archive`, and run the commands above. The helper
checks its locked SHA256 before extraction and patching. The complete miniJVM
and libffi sources are included separately in the same source bundle.
