#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
host_jdk="${HOST_JDK25:-${repo_root}/.deps/host-jdk25/jdk-25.0.5+8}"
tmp="$(mktemp -d "${TMPDIR:-/tmp}/horizon-nio-host.XXXXXX")"
trap 'rm -rf "$tmp"' EXIT

cc -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror \
    -I"${repo_root}/ports/openjdk25/src/java.base/horizon/native/libnio/ch" \
    -I"${repo_root}/ports/openjdk25/src/java.base/horizon/native/libnio/fs" \
    "${repo_root}/ports/openjdk25/tests/nio_native/test_file_io.c" \
    -o "${tmp}/test_file_io"
"${tmp}/test_file_io"

cc -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror \
    -I"${repo_root}/ports/openjdk25/src/java.base/horizon/native/libnio/fs" \
    "${repo_root}/ports/openjdk25/tests/nio_native/test_horizon_realpath.c" \
    -Wl,--wrap=realpath -Wl,--wrap=stat -o "${tmp}/test_horizon_realpath"
"${tmp}/test_horizon_realpath"

cc -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -pthread \
    -I"${host_jdk}/include" -I"${host_jdk}/include/linux" \
    -I"${repo_root}/build/openjdk25-horizon/support/headers/java.base" \
    -I"${repo_root}/ports/openjdk25/src/java.base/horizon/native/libjava" \
    -I"${repo_root}/ports/openjdk25/src/java.base/horizon/native/libnio/ch" \
    -I"${repo_root}/.deps/openjdk25-source/src/java.base/share/native/libjava" \
    -I"${repo_root}/.deps/openjdk25-source/src/java.base/unix/native/libjava" \
    -I"${repo_root}/.deps/openjdk25-source/src/java.base/share/native/libnio/ch" \
    -I"${repo_root}/.deps/openjdk25-source/src/hotspot/share/include" \
    -I"${repo_root}/ports/openjdk25/src/hotspot/os/horizon/include" \
    "${repo_root}/ports/openjdk25/src/java.base/horizon/native/libnio/ch/UnixFileDispatcherImpl.c" \
    "${repo_root}/ports/openjdk25/tests/nio_native/test_dispatcher_jni.c" \
    -o "${tmp}/test_dispatcher_jni"
"${tmp}/test_dispatcher_jni"

cc -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror \
    -I"${host_jdk}/include" -I"${host_jdk}/include/linux" \
    -I"${repo_root}/build/openjdk25-horizon/support/headers/java.base" \
    -I"${repo_root}/ports/openjdk25/src/java.base/horizon/native/libnio/ch" \
    -I"${repo_root}/.deps/openjdk25-source/src/java.base/share/native/libjava" \
    -I"${repo_root}/.deps/openjdk25-source/src/java.base/unix/native/libjava" \
    -I"${repo_root}/.deps/openjdk25-source/src/java.base/share/native/libnio/ch" \
    -I"${repo_root}/.deps/openjdk25-source/src/java.base/share/native/include" \
    -I"${repo_root}/.deps/openjdk25-source/src/hotspot/share/include" \
    -I"${repo_root}/ports/openjdk25/src/hotspot/os/horizon/include" \
    "${repo_root}/ports/openjdk25/src/java.base/horizon/native/libnio/ch/IOUtil.c" \
    "${repo_root}/ports/openjdk25/src/java.base/horizon/native/libnio/ch/NativeThread.c" \
    "${repo_root}/ports/openjdk25/tests/nio_native/test_ioutil_jni.c" \
    -o "${tmp}/test_ioutil_jni"
"${tmp}/test_ioutil_jni"

cc -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror \
    -I"${host_jdk}/include" -I"${host_jdk}/include/linux" \
    -I"${repo_root}/build/openjdk25-horizon/support/headers/java.base" \
    -I"${repo_root}/ports/openjdk25/src/java.base/horizon/native/libnio/ch" \
    -I"${repo_root}/.deps/openjdk25-source/src/java.base/share/native/libjava" \
    -I"${repo_root}/.deps/openjdk25-source/src/java.base/unix/native/libjava" \
    -I"${repo_root}/.deps/openjdk25-source/src/java.base/share/native/include" \
    -I"${repo_root}/.deps/openjdk25-source/src/java.base/share/native/libnio/ch" \
    "${repo_root}/ports/openjdk25/src/java.base/horizon/native/libnio/ch/UnixDomainSockets.c" \
    "${repo_root}/ports/openjdk25/src/java.base/horizon/native/libnio/MappedMemoryUtils.c" \
    "${repo_root}/ports/openjdk25/tests/nio_native/test_unsupported_jni.c" \
    -o "${tmp}/test_unsupported_jni"
"${tmp}/test_unsupported_jni"

cc -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror \
    -I"${host_jdk}/include" -I"${host_jdk}/include/linux" \
    -I"${repo_root}/build/openjdk25-horizon/support/headers/java.base" \
    -I"${repo_root}/ports/openjdk25/src/java.base/horizon/native/libnio/ch" \
    -I"${repo_root}/.deps/openjdk25-source/src/java.base/share/native/libjava" \
    -I"${repo_root}/.deps/openjdk25-source/src/java.base/unix/native/libjava" \
    -I"${repo_root}/.deps/openjdk25-source/src/java.base/share/native/include" \
    -I"${repo_root}/.deps/openjdk25-source/src/java.base/share/native/libnio/ch" \
    -I"${repo_root}/.deps/openjdk25-source/src/hotspot/share/include" \
    -I"${repo_root}/ports/openjdk25/src/hotspot/os/horizon/include" \
    "${repo_root}/ports/openjdk25/src/java.base/horizon/native/libnio/ch/SocketDispatcher.c" \
    "${repo_root}/ports/openjdk25/src/java.base/horizon/native/libnio/ch/DatagramDispatcher.c" \
    "${repo_root}/ports/openjdk25/tests/nio_native/test_dispatchers_jni.c" \
    -o "${tmp}/test_dispatchers_jni"
"${tmp}/test_dispatchers_jni"
