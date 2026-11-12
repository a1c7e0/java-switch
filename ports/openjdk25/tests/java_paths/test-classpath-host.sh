#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
host_jdk="${HOST_JDK25:-${repo_root}/.deps/host-jdk25/jdk-25.0.5+8}"
tmp="$(mktemp -d "${TMPDIR:-/tmp}/horizon-sdmc-classpath.XXXXXX")"
trap 'rm -rf "$tmp"' EXIT

mkdir -p "${tmp}/classes" "${tmp}/cwd/sdmc:/switch/mc-runtime/tests"
"${host_jdk}/bin/javac" -Xlint:all -Werror -d "${tmp}/classes" \
    "${repo_root}/ports/openjdk25/tests/java_paths/SdmcClasspathPayload.java" \
    "${repo_root}/ports/openjdk25/tests/java_paths/SdmcClasspathHostTest.java"
"${host_jdk}/bin/jar" --create \
    --file "${tmp}/cwd/sdmc:/switch/mc-runtime/tests/payload.jar" \
    -C "${tmp}/classes" hostclasspath/SdmcClasspathPayload.class
cd "${tmp}/cwd"
"${host_jdk}/bin/java" -cp "${tmp}/classes" SdmcClasspathHostTest
