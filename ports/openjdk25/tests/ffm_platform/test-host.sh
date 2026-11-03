#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"
SOURCE="$ROOT/.deps/openjdk25-source"
if [[ ! -f "$SOURCE/src/java.base/share/classes/jdk/internal/foreign/CABI.java" ]]; then
  echo "Pinned OpenJDK 25 source is required; run scripts/openjdk25.py prepare first" >&2
  exit 1
fi
HOST_JAVAC="$(find "$ROOT/.deps/host-jdk25" -type f -path '*/bin/javac' -print -quit)"
if [[ -z "$HOST_JAVAC" ]]; then
  echo "OpenJDK 25 host javac is required" >&2
  exit 1
fi
HOST_JAVA="${HOST_JAVAC%/javac}/java"
TEMP_DIR="$(mktemp -d)"
SRC="$TEMP_DIR/src"
OUT="$TEMP_DIR/classes"
WORK="$TEMP_DIR/work"
mkdir -p "$WORK/src/java.base/share/classes/jdk/internal/foreign" \
  "$SRC/jdk/internal/foreign" "$SRC/jdk/internal/foreign/abi/fallback" \
  "$SRC/jdk/internal/util" "$SRC/jdk/internal/vm" "$OUT"
cp "$SOURCE/src/java.base/share/classes/jdk/internal/foreign/CABI.java" \
  "$WORK/src/java.base/share/classes/jdk/internal/foreign/CABI.java"
(cd "$WORK" && patch --batch --fuzz=0 -p1 < \
  "$ROOT/patches/openjdk25/0021-horizon-ffm-platform-selection.patch")
cp "$WORK/src/java.base/share/classes/jdk/internal/foreign/CABI.java" "$SRC/jdk/internal/foreign/"
cp "$ROOT/ports/openjdk25/tests/ffm_platform/HorizonCabiHostTest.java" "$SRC/jdk/internal/foreign/"
cp "$ROOT/ports/openjdk25/tests/ffm_platform/OperatingSystem.java" "$SRC/jdk/internal/util/"
cp "$ROOT/ports/openjdk25/tests/ffm_platform/ForeignLinkerSupport.java" "$SRC/jdk/internal/vm/"
cp "$ROOT/ports/openjdk25/tests/ffm_platform/FallbackLinker.java" "$SRC/jdk/internal/foreign/abi/fallback/"
"$HOST_JAVAC" --patch-module "java.base=$SRC" -d "$OUT" \
  "$SRC/jdk/internal/util/OperatingSystem.java" \
  "$SRC/jdk/internal/vm/ForeignLinkerSupport.java" \
  "$SRC/jdk/internal/foreign/abi/fallback/FallbackLinker.java" \
  "$SRC/jdk/internal/foreign/CABI.java" \
  "$SRC/jdk/internal/foreign/HorizonCabiHostTest.java"
"$HOST_JAVA" --patch-module "java.base=$OUT" -Dos.arch=aarch64 \
  -Dtest.fallback.supported=true -m java.base/jdk.internal.foreign.HorizonCabiHostTest FALLBACK
"$HOST_JAVA" --patch-module "java.base=$OUT" -Dos.arch=aarch64 \
  -Dtest.fallback.supported=false -m java.base/jdk.internal.foreign.HorizonCabiHostTest UNSUPPORTED
echo "Horizon C ABI initialization host tests passed"
