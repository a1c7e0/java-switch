# SDK private crypto symbol isolation

The reusable SDK includes miniJVM's private Mbed TLS implementation. Those
implementation symbols are not part of public SDK ABI 1. Applications may link
an independent Mbed TLS/crypto dependency; exporting both implementations under
`mbedtls_*` previously caused duplicate definitions and risked incompatible
header/context binding. See https://github.com/a1c7e0/java-switch/issues/3.

`scripts/build-sdk.py` now namespaces every archive-defined `mbedtls_*` global
and all corresponding references to `js_private_mbedtls_*`, using a map derived
from the actual archive and target nm/objcopy. Public runtime/JNI/native-resolver
entry points are unchanged. `private-crypto-symbols.map` records the mapping.
The transformed archive is staged in the destination directory for same-filesystem
atomic replacement; source objects and the standalone interpreter archive remain
unchanged. Running isolation again is byte-idempotent.

Validation performed in an isolated checkout based on documentation revision
`8e6c732257937fb01108850b2e1e6838d4d33cfd` with these changes:

* Genuine Horizon and host SDK builds passed, isolating 712/717 private globals.
* The original issue's minimal SDK + portlibs libmbedcrypto link produced an
  AArch64 PIE without duplicate-definition suppression.
* Synthetic composition, public-symbol preservation and idempotence tests passed.
* Maintained Java8 interpreter and Java25 runtime-gate/JNI/FFM host tests passed.

These results concern archive composition, cross-building and host execution.
No Horizon Java VM, Java, JNI/FFM or instrumentation execution was performed.
The complete instrument-enabled VM/image package was not rebuilt for this SDK-only
change. Downstream must test and pin an immutable source revision when consuming
new SDK artifacts; existing pinned packages do not change automatically.
