#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "$0")/../../../.." && pwd)
upstream="$root/.deps/openjdk25-source/src/java.base"
overlay="$root/ports/openjdk25/src/java.base/horizon/native"
test_dir="$root/ports/openjdk25/tests/java_props"
temp=$(mktemp -d)
trap 'rm -rf "$temp"' EXIT

"${CC:-cc}" -std=c11 -D_POSIX_C_SOURCE=200809L \
  -DARCHPROPNAME='"aarch64"' -Wall -Wextra -Werror \
  -I"$upstream/share/native/include" \
  -I"$upstream/unix/native/include" \
  -I"$overlay/include" \
  -I"$upstream/share/native/libjava" \
  -I"$upstream/unix/native/libjava" \
  "$overlay/libjava/java_props_md.c" \
  "$test_dir/test_java_props.c" -o "$temp/test_java_props"

env -u USER -u LOGNAME -u HOME -u TMPDIR \
  "$temp/test_java_props" 'sdmc:/switch/mc-runtime/tmp' '?' 'sdmc:/switch/mc-runtime'
env -u USER -u LOGNAME -u HOME TMPDIR='sdmc:/switch/custom-tmp' \
  USER='switch-user' HOME='sdmc:/switch/home' \
  "$temp/test_java_props" 'sdmc:/switch/custom-tmp' 'switch-user' 'sdmc:/switch/home'
env -u USER TMPDIR='sdmc:/switch/missing-tmp' LOGNAME='switch-logname' \
  HOME='sdmc:/switch/logname-home' \
  "$temp/test_java_props" 'sdmc:/switch/missing-tmp' 'switch-logname' 'sdmc:/switch/logname-home'
echo 'Horizon java.base property host tests: PASS'
