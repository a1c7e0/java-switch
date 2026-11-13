# OpenJDK 25 client-referenced module profile

The optional `--with-minecraft` build profile uses actual OpenJDK 25 target
module classes. It does not change SDK ABI 1 or replace the baseline
`java.base` and optional `java.instrument` profiles. It adds `java.logging`,
`jdk.unsupported`, `java.management`, `java.desktop`, `java.datatransfer`, and
`java.xml`, and `java.prefs`, plus `jdk.charsets` for the Unix font charset support, following the static module audit in
`Switchcraft/docs/MINECRAFT_RUNTIME_GAPS.md`.

`java.management` is paired with the genuine upstream `libmanagement` native
archive and HotSpot's management/JMM feature. The profile configures that VM
feature, records the full upstream `libjvm.a` inventory and module port digest,
and verifies both `JNI_OnLoad_management` and `JVM_GetManagement` in the linked
ELF. No Java management provider is fabricated. The other modules are compiled
from OpenJDK's actual target sources and checked for classfile version 69 and
complete per-module file hashes before image construction.

Build the optional profile with:

```sh
bash scripts/build-openjdk25-runtime.sh --with-minecraft
```

It can be combined with the existing startup instrumentation profile using
`--with-minecraft --with-instrument`. The image builder accepts the matching
`--with-minecraft` flag and requires fresh proofs from the configured VM,
compiled target modules, and native management archive. The image and runtime
package manifests list every included module, native archive, and source-port
digest. Host jlink/jmod tools only assemble target classes; no host VM or native
library enters the image.

The module set is a cross-build step toward the audited launcher's dependencies,
not a claim that the game runs. JNDI, HTTP client, and JFR remain outside this
profile. `java.desktop` supplies actual Java classes including `AudioFormat`,
but does not add AWT, a Java Sound backend, audio-device support, or desktop
native libraries. Socket/TLS limitations, downstream LWJGL integration,
rendering, native providers, and physical execution remain separate work.
Package metadata explicitly keeps runtime startup unverified and title
execution unclaimed.
