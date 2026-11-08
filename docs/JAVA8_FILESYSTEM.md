# Java 8 diagnostic filesystem behavior

miniJVM's Java 8 class library resolves `File` paths through `FileSystemPosix`.
Horizon's `sdmc:/` URI is absolute even though it does not begin with `/`. The
maintained class-library patch recognizes that URI before `FileSystemImpl`
prepends the process startup directory. Without this, a URI passed as the
diagnostic directory becomes a nested path such as
`sdmc:/switch/mc-runtime/instances/java-smoke/sdmc:/switch/...`.

`File.createNewFile()` now calls a registered native operation that uses
`open(O_WRONLY | O_CREAT | O_EXCL, 0666)`. The result distinguishes a successful
creation, `EEXIST` (reported as `false`), and other errno values (reported as an
`IOException` containing the errno). This makes creation atomic and avoids a
check-then-truncate race.

The C runtime remains patched from the pinned source by
`patches/minijvm-horizon.patch`. Java filesystem sources are copied into
`build/minijvm-java-source` and patched with
`patches/minijvm-java8-fs.patch` before Java 8 compilation. The pinned source
under `.deps/minijvm-source` stays unchanged. The filesystem regression test
runs the actual miniJVM host diagnostic and leaves any existing
`java-smoke.txt` untouched:

```sh
python3 scripts/build-java-tests.py
python3 scripts/build-minijvm.py --host
python3 -m unittest tests.test_java8_filesystem tests.test_java
```

These host checks validate interpreter behavior on the host. They do not
establish Horizon execution. The reported physical Switch log failed at
`exclusive file creation` on firmware 21.2.0; rerun the packaged Java 8
diagnostic on hardware to verify the URI resolution and native `O_EXCL` path.
