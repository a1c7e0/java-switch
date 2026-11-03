#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
host_jdk="${HOST_JDK25:-${repo_root}/.deps/host-jdk25/jdk-25.0.5+8}"
tmp="$(mktemp -d "${TMPDIR:-/tmp}/horizon-wakeup-host.XXXXXX")"
trap 'rm -rf "$tmp"' EXIT

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
    "${repo_root}/ports/openjdk25/tests/nio_native/test-wakeup.c" \
    -o "${tmp}/test-wakeup"
"${tmp}/test-wakeup"
