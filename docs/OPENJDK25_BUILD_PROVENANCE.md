# OpenJDK 25 diagnostic build provenance

`src/platform.c` writes `JS_BUILD_REVISION` into standalone and VM diagnostic
logs. The normal Make target refreshes `build/build-info.h` through its forced
prerequisite, but `scripts/openjdk25.py` compiles `platform.c` directly for the
VM diagnostic and the zero-services probe. Those direct paths must run
`scripts/build-info.py` immediately before compilation so the logged revision
and dirty marker identify the tree used for the diagnostic binary.

An old `runtime_source` string is evidence that the generated header was stale;
by itself it does not show that the VM archive or JNI provider archives were
stale. Check their separate archive and link hashes in the OpenJDK build
evidence. A `-dirty` suffix is expected whenever source changes are present at
compile time; final release artifacts should be rebuilt from the intended
clean revision and repackaged together with that revision's source.
