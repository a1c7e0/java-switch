# Clean build validation for issues #1 and #2

Tested source revision: `b7c2ac7fd422bc2543e5c698db4e33a01cc0312d`.

Issue #1 exposed configure's dependence on an existing native output directory.
The helper now creates the libffi output directory with its parents before
copying or updating the target archive. A regression exercises both configure
profiles with no native output directory and checks repeat configuration keeps
existing outputs. External configure/archiver commands are mocked in this small
filesystem test; actual cross-build validation is recorded separately below.

Issue #2 exposed host JDK discovery through a directory symlink. The FFM host
script follows cache symlinks and rejects missing/empty caches explicitly. Its
regression invokes the actual pinned javac/java with the actual maintained CABI
patch and fixtures. An ordinary JDK root and bin directory, a symlinked cache
root, and absent/empty caches are tested. With the old discovery command, the
ordinary case passed and the symlinked case failed. Both valid cases now pass.

## Complete workflow verification

A detached worktree at the tested revision reused the pinned devkitA64/libnx
toolchain, verified host-JDK caches and pristine source caches. Its JDK25 cache
root was a symlink. Target outputs were built locally in that worktree.

```sh
bash scripts/build-openjdk25-runtime.sh --with-instrument
bash scripts/build-openjdk25-runtime.sh
```

The instrument workflow began without any `build/` directory. Its complete
VM/native/image/NRO build and package verification passed. Before the base run,
only the isolated worktree's `build/openjdk25-horizon` directory was removed;
its absence was asserted. The base run reused baseline dependency artifacts and
prepared sources, and rebuilt its native VM output from scratch. No parent
directory workaround or runtime source modification was applied in either run.

| Verified item | Instrument profile | Base profile |
| --- | --- | --- |
| Host regressions | 17 passed | 17 passed |
| Actual upstream VM archive | 577 ARM64 members | 553 ARM64 members |
| JVMTI/services | Enabled | Disabled |
| Target image | java.base, java.instrument | java.base |
| Actual Horizon PIE/NRO link | Zero missing symbols | Zero missing symbols |
| Maintained class-library host suite | Passed | Passed |
| SDK, SD package and native/image hashes | Passed | Passed |
| Full pinned source/patch/overlay reconstruction | Passed | Passed |

The optional client-referenced module profile is implemented separately in
`docs/OPENJDK25_MINECRAFT_PROFILE.md`. It has not been cross-built or packaged
at the time of this report; the paired verification results above cover only
the instrument and base profiles.

The paired reports in `docs/evidence/issues-1-2-clean-instrument.json` and
`docs/evidence/issues-1-2-clean-base.json` record source pins, feature sets,
module/class counts and each build's actual NRO hash. These checks establish
clean build workflow behavior, not byte-for-byte reproducibility across build
paths or times. They do not execute the target VM on Horizon.

Runtime ABI 1 and default profile selection remain unchanged. Physical Switch
startup, classfile69 execution, JNI, Java FFM, instrumentation and Minecraft
remain **NOT TESTED**.
