# OpenJDK 25 application class path on Horizon

R4 physical logs showed that the application JAR opened from `sdmc:/` and had a
valid ZIP signature, while the system loader could not find either
`HelloHorizon25` or the packaged smoke class. This isolated the failure to
class-path resolution after native file preflight. The log set is preserved at
`/tmp/switchcraft-r4-feedback/logs-r4` in the test workspace; R4 is device
evidence, while the fix below has only host validation.

OpenJDK's `URLClassPath` turns a class-path entry into a `file:` URL with
`ParseUtil.fileToEncodedURL`. That method adds a leading slash to the Horizon
device path, producing `file:/sdmc:/...`. `JarLoader` and `FileLoader` then
interpret the URL path as a host-style `/sdmc:/...` path. Horizon's newlib
devoptab expects `sdmc:/...`, so the loader misses the existing JAR and
directory. The NIO `UnixUriUtils` path is not involved in this loader path.

Patch 0013 adds one `ParseUtil` conversion for local file URLs. It decodes the
URL path first, then removes the leading slash from the `/sdmc:/` root only
when `os.name` is `Horizon`. Jar and directory class loaders, `URLJarFile`,
and the local file URL handler share this conversion. Other Unix paths remain
unchanged. Remote file authorities do not map to local storage; directory
class loading rejects them explicitly.

`ports/openjdk25/tests/java_paths/test-classpath-host.sh` compiles the
maintained patched `ParseUtil`, `FileURLMapper`, `URLClassPath`, `URLJarFile`,
and file URL handler into the host JDK's `java.base` module and exercises actual
`URLClassLoader` JAR and directory loading. The fixture uses an encoded device
colon, escaped space, and Unicode directory. The test checks default-package
and packaged classes, JAR and directory resource streams, direct `jar:` and
`file:` URL resource reads with and without a query string, localhost and
ordinary Unix JAR URLs, an absent resource, and remote JAR and directory file
URLs. It also runs the unpatched
host loader and requires the same payload `ClassNotFoundException` seen in the
physical logs before verifying the patched loader succeeds.

Validation completed:

- `python3 scripts/openjdk25.py prepare` — pinned source prepared with zero-fuzz patches.
- `bash ports/openjdk25/tests/java_paths/test-classpath-host.sh` — host JAR, directory, and resource reads passed; the negative control reproduced the payload `ClassNotFoundException`.
- `python3 scripts/test-openjdk25-classlib-host.py` — class-library host suite passed.
- `bash scripts/build-openjdk25-runtime.sh --with-instrument --with-minecraft` — full instrumented Minecraft profile built, linked and packaged; full source and package verification passed.

The package manifest records a clean immutable source revision and
`source_tree_dirty=false`. The built image contains the patched loader classes,
and the VM-linked AArch64 NRO passed the ELF/package checks. This is cross-build
evidence only: Horizon startup, Java execution, resource loading, JNI, agents,
graphics, and Minecraft remain unverified on device.
