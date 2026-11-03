#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "$0")/../../../.." && pwd)
src="$root/.deps/openjdk25-source/src/java.base"
hotspot="$root/.deps/openjdk25-source/src/hotspot"
overlay="$root/ports/openjdk25/src/java.base/horizon/native"
test_dir="$root/ports/openjdk25/tests/java_io"
temp=$(mktemp -d)
trap 'rm -rf "$temp"' EXIT

common=(-std=c11 -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -Wall -Wextra -Werror
  -I"$src/unix/native/include"
  -I"$overlay/include" -I"$overlay/libjava"
  -I"$src/share/native/include" -I"$src/unix/native/include"
  -I"$src/share/native/libjava" -I"$src/unix/native/libjava"
  -I"$hotspot/share/include"
  -I"$root/ports/openjdk25/src/hotspot/os/horizon/include"
  -I"$root/build/openjdk25-horizon/support/headers/java.base"
  -I"$root/build/openjdk25-horizon/support/modules_include/java.base"
  -I"$root/build/openjdk25-horizon/jdk/include")

"${CC:-cc}" "${common[@]}" -pthread \
  "$overlay/libjava/io_util_md.c" \
  "$overlay/libjava/jni_util_md.c" \
  "$test_dir/test_io_helpers.c" -o "$temp/test_io_helpers"
"$temp/test_io_helpers"

"${CC:-cc}" "${common[@]}" \
  "$overlay/libjava/canonicalize_md.c" \
  "$test_dir/test_canonicalize.c" -o "$temp/test_canonicalize"
"$temp/test_canonicalize"

"${CC:-cc}" "${common[@]}" \
  "$overlay/libjava/JdkConsoleImpl_md.c" \
  "$test_dir/test_console.c" -o "$temp/test_console"
"$temp/test_console"

"${CC:-cc}" "${common[@]}" -Wno-unused-parameter \
  "$src/unix/native/libjava/Console_md.c" \
  "$test_dir/test_console_status.c" -o "$temp/test_console_status"
"$temp/test_console_status"

"${CC:-cc}" "${common[@]}" \
  "$overlay/libjava/TimeZone_md.c" \
  "$test_dir/test_timezone.c" -o "$temp/test_timezone"
"$temp/test_timezone"

echo 'Horizon java.io native host tests: PASS'
