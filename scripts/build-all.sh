#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
export DEVKITPRO="${DEVKITPRO:-$PWD/.toolchain/opt/devkitpro}"
if [[ ! -f "$DEVKITPRO/libnx/lib/libnx.a" ]]; then python3 scripts/bootstrap-toolchain.py; fi
python3 scripts/bootstrap-deps.py
mkdir -p build
make -j2 all 2>&1 | tee build/cross-build.log
make host-test 2>&1 | tee build/host-tests.log
python3 scripts/package.py
