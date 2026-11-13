#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
host_jdk="${HOST_JDK25:-${repo_root}/.deps/host-jdk25/jdk-25.0.5+8}"
tmp="$(mktemp -d "${TMPDIR:-/tmp}/horizon-sdmc-classpath.XXXXXX")"
trap 'rm -rf "$tmp"' EXIT

device_test_dir="${tmp}/cwd/sdmc:/switch/mc runtime/測試"
mkdir -p "${tmp}/classes" "${device_test_dir}/classes"
patched_mapper="${repo_root}/.deps/openjdk25-horizon/src/java.base/unix/classes/jdk/internal/loader/FileURLMapper.java"
patched_classpath="${repo_root}/.deps/openjdk25-horizon/src/java.base/share/classes/jdk/internal/loader/URLClassPath.java"
patched_parseutil="${repo_root}/.deps/openjdk25-horizon/src/java.base/share/classes/sun/net/www/ParseUtil.java"
patched_jarfile="${repo_root}/.deps/openjdk25-horizon/src/java.base/share/classes/sun/net/www/protocol/jar/URLJarFile.java"
patched_file_handler="${repo_root}/.deps/openjdk25-horizon/src/java.base/unix/classes/sun/net/www/protocol/file/Handler.java"
if [[ ! -f "${patched_mapper}" ]]; then
    echo "Prepared patched FileURLMapper source is required; run scripts/openjdk25.py prepare" >&2
    exit 1
fi
if [[ ! -f "${patched_classpath}" ]]; then
    echo "Prepared patched URLClassPath source is required; run scripts/openjdk25.py prepare" >&2
    exit 1
fi
"${host_jdk}/bin/javac" --patch-module java.base="$(dirname "${patched_mapper}"):$(dirname "${patched_classpath}"):$(dirname "${patched_parseutil}"):$(dirname "${patched_jarfile}"):$(dirname "${patched_file_handler}")" \
    -d "${tmp}/patched-java-base" "${patched_mapper}" "${patched_classpath}" \
    "${patched_parseutil}" "${patched_jarfile}" "${patched_file_handler}"
"${host_jdk}/bin/javac" -Xlint:all -Werror -d "${tmp}/classes" \
    "${repo_root}/ports/openjdk25/tests/java_paths/HelloClasspathPayload.java" \
    "${repo_root}/ports/openjdk25/tests/java_paths/SdmcClasspathPayload.java" \
    "${repo_root}/ports/openjdk25/tests/java_paths/SdmcClasspathHostTest.java"
cp -R "${tmp}/classes/." "${device_test_dir}/classes/"
printf 'resource-through-device-path\n' > "${device_test_dir}/classes/payload.txt"
"${host_jdk}/bin/jar" --create \
    --file "${device_test_dir}/payload.jar" \
    -C "${tmp}/classes" hostclasspath/SdmcClasspathPayload.class \
    -C "${tmp}/classes" HelloClasspathPayload.class \
    -C "${device_test_dir}/classes" payload.txt
cd "${tmp}/cwd"
"${host_jdk}/bin/java" -Dos.name=Horizon -cp "${tmp}/classes" \
    SdmcClasspathHostTest expect-cnfe
"${host_jdk}/bin/java" --patch-module java.base="${tmp}/patched-java-base" \
    -Dos.name=Horizon -cp "${tmp}/classes" SdmcClasspathHostTest \
    "${device_test_dir}/payload.jar"
