# Experimental OpenJDK 25 Horizon source overlay

Modified/created on 2026-11-02. These files are copied into the patched upstream
tree by `scripts/openjdk25.py`; never maintain edits only in `.deps/`.
The source revision and archive hash remain in `dependencies.lock.json`.

New HotSpot port files are GPL-2.0-only as stated in their SPDX headers.
Upstream-derived files retain their copyright and license notices.
`licenses/openjdk25-LICENSE.txt` contains the upstream GPL/Classpath text; the
Classpath exception applies only to files designated by upstream, not to all
HotSpot code. The root project's MIT license does not replace these notices.

The overlay currently implements actual HotSpot wall/monotonic time functions,
a libnx counting semaphore adapter, and AArch64 Zero ordering/atomic primitives.
The globals headers select initial stack/default flags; they do not initialize
a VM. `horizon_bridge.c` is compiled as C because HotSpot and libnx have
incompatible global `u8`, `s8`, `Handle` and `Mutex` type names. The bridge
exports standard-width types and opaque pointers. It makes no POSIX signal or
dynamic-loading claims.

Reproduce, after the ordinary baseline build:

```sh
python3 scripts/bootstrap-openjdk-tools.py
python3 scripts/openjdk25.py configure
python3 scripts/openjdk25.py build       # expected failure: mutex_horizon.hpp
python3 scripts/openjdk25.py components  # deliberately separate object build
python3 scripts/openjdk25.py test-host   # clocks mocked; host atomic execution
python3 scripts/openjdk25.py probe       # native services NRO, NOT a Java VM
```

The component build reuses the real upstream compiler command recorded by the
failed static build, removes the precompiled-header invocation, and compiles
time, semaphore, and upstream string utilities as AArch64 relocatable objects.
It also compiles the libnx C bridge. Only time, semaphore and the bridge are
linked into the diagnostic; the atomic header is exercised by the diagnostic's
C++ translation unit. None of these objects constitutes `libjvm.a`.

The standalone diagnostic defines a fatal error sink that logs and aborts. This
is not the VM's exception dispatcher. It does not define `JNI_CreateJavaVM` and
the builder checks that the symbol is absent. Its UI and package manifest state
that Java bytecode cannot run. No `java25-test.nro` is generated.

When this NRO is packaged, `java-switch-source.tar.gz` includes the pristine
OpenJDK archive as `third-party/openjdk25-upstream.tar.gz` plus the complete
tracked repository, patches, overlay, build scripts and notices. To reconstruct,
extract the repository source, copy that upstream archive to
`java-switch/.deps/openjdk25.archive`, and run the commands above. The helper
checks its locked SHA256 before extraction and patching. The complete miniJVM
and libffi sources are included separately in the same source bundle.
