#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "$0")/../../../.." && pwd)
overlay="$root/ports/openjdk25/src/java.instrument/horizon/native/libinstrument"
test_dir="$root/ports/openjdk25/tests/java_instrument"
temp=$(mktemp -d)
trap 'rm -rf "$temp"' EXIT

"${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
  -I"$overlay" -I"$root/ports/openjdk25/src/hotspot/os/horizon/include" \
  "$overlay/FileSystemSupport_md.c" "$overlay/EncodingSupport_md.c" \
  "$test_dir/test_filesystem_encoding.c" -o "$temp/test_filesystem_encoding"
"$temp/test_filesystem_encoding"

echo 'Horizon java.instrument path and encoding host tests: PASS'
