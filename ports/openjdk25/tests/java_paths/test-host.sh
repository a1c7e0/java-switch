#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
source_root="${OPENJDK25_SOURCE_ROOT:-${repo_root}/.deps/openjdk25-source}"
host_jdk="${HOST_JDK25:-${repo_root}/.deps/host-jdk25/jdk-25.0.5+8}"
patch_file="${repo_root}/patches/openjdk25/0013-horizon-java-base-platform-sources.patch"
overlay="${repo_root}/ports/openjdk25/src/java.base/horizon/classes"
test_source="${repo_root}/ports/openjdk25/tests/java_paths/HorizonUnixPathHostTest.java"
tmp="$(mktemp -d "${TMPDIR:-/tmp}/horizon-java-paths.XXXXXX")"
trap 'rm -rf "$tmp"' EXIT

if [[ ! -x "${host_jdk}/bin/javac" || ! -x "${host_jdk}/bin/java" ]]; then
    echo "JDK 25 host tools not found under ${host_jdk}; set HOST_JDK25" >&2
    exit 2
fi
if [[ ! -f "${source_root}/src/java.base/unix/classes/sun/nio/fs/UnixPath.java" ]]; then
    echo "OpenJDK 25 source tree not found at ${source_root}; set OPENJDK25_SOURCE_ROOT" >&2
    exit 2
fi

# Stage only source files touched by 0013. All edits and patch application stay
# under tmp; the maintained source tree and .deps remain read-only.
files=(
    make/common/Modules.gmk
    make/modules/java.base/Java.gmk
    src/java.base/share/classes/jdk/internal/util/OperatingSystem.java
    src/java.base/unix/classes/jdk/internal/loader/FileURLMapper.java
    src/java.base/share/classes/jdk/internal/loader/URLClassPath.java
    src/java.base/share/classes/sun/net/www/ParseUtil.java
    src/java.base/share/classes/sun/net/www/protocol/jar/URLJarFile.java
    src/java.base/unix/classes/sun/net/www/protocol/file/Handler.java
    src/java.base/unix/classes/java/io/UnixFileSystem.java
    src/java.base/unix/classes/sun/net/PortConfig.java
    src/java.base/unix/classes/sun/nio/ch/UnixFileDispatcherImpl.java
    src/java.base/unix/classes/sun/nio/fs/UnixFileSystem.java
    src/java.base/unix/classes/sun/nio/fs/UnixPath.java
    src/java.base/unix/classes/sun/nio/fs/UnixUriUtils.java
)
for file in "${files[@]}"; do
    mkdir -p "${tmp}/source/$(dirname "${file}")"
    cp "${source_root}/${file}" "${tmp}/source/${file}"
done
(
    cd "${tmp}/source"
    patch -p1 --batch --fuzz=0 -i "${patch_file}"
)

# The host compiler runs on Linux. Supply only the build-generated constant
# class needed to select Horizon code in this host-only test.
mkdir -p "${tmp}/source/src/java.base/share/classes/jdk/internal/util"
cat > "${tmp}/source/src/java.base/share/classes/jdk/internal/util/PlatformProps.java" <<'JAVA'
package jdk.internal.util;
final class PlatformProps {
    static final String CURRENT_OS_STRING = "horizon";
    static final boolean TARGET_OS_IS_LINUX = false;
    static final boolean TARGET_OS_IS_MACOS = false;
    static final boolean TARGET_OS_IS_WINDOWS = false;
    static final boolean TARGET_OS_IS_AIX = false;
}
JAVA

out="${tmp}/classes"
mkdir -p "${out}"
cd "${repo_root}"
"${host_jdk}/bin/javac" -Xlint:all -Werror \
    --patch-module "java.base=${overlay}:${repo_root}/ports/openjdk25/tests/java_paths:${tmp}/source/src/java.base/share/classes:${tmp}/source/src/java.base/unix/classes" \
    -d "${out}" \
    $(find "${overlay}" -name '*.java' -print) \
    "${test_source}" \
    "${tmp}/source/src/java.base/share/classes/jdk/internal/util/OperatingSystem.java" \
    "${tmp}/source/src/java.base/share/classes/jdk/internal/util/PlatformProps.java" \
    "${tmp}/source/src/java.base/unix/classes/java/io/UnixFileSystem.java" \
    "${tmp}/source/src/java.base/unix/classes/sun/net/PortConfig.java" \
    "${tmp}/source/src/java.base/unix/classes/sun/nio/ch/UnixFileDispatcherImpl.java" \
    "${tmp}/source/src/java.base/unix/classes/sun/nio/fs/UnixFileSystem.java" \
    "${tmp}/source/src/java.base/unix/classes/sun/nio/fs/UnixPath.java" \
    "${tmp}/source/src/java.base/unix/classes/sun/nio/fs/UnixUriUtils.java"
"${host_jdk}/bin/java" --patch-module "java.base=${out}" \
    -m java.base/sun.nio.fs.HorizonUnixPathHostTest
