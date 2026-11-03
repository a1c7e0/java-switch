#!/usr/bin/env bash
# Build the genuine experimental Horizon VM; no device execution is implied.
set -euo pipefail
cd "$(dirname "$0")/.."

profile=base
case "${1:-}" in
  '') ;;
  --with-instrument) profile=instrument ;;
  *) echo 'Usage: scripts/build-openjdk25-runtime.sh [--with-instrument]' >&2; exit 2 ;;
esac
if [[ $# -gt 1 ]]; then
  echo 'Usage: scripts/build-openjdk25-runtime.sh [--with-instrument]' >&2
  exit 2
fi

export DEVKITPRO="${DEVKITPRO:-$PWD/.toolchain/opt/devkitpro}"
if [[ ! -f "$DEVKITPRO/libnx/lib/libnx.a" ]]; then
  python3 scripts/bootstrap-toolchain.py
fi
python3 scripts/bootstrap-deps.py
python3 scripts/bootstrap-openjdk-tools.py
mkdir -p build
make -j2 all 2>&1 | tee build/cross-build.log
make host-test 2>&1 | tee build/host-tests.log

configure=configure
link=link-runtime
image_options=()
if [[ $profile == instrument ]]; then
  configure=configure-agent
  link=link-runtime-agent
  image_options=(--with-instrument)
fi
python3 scripts/openjdk25.py "$configure"
python3 scripts/openjdk25.py build
python3 scripts/openjdk25.py java-base
python3 scripts/openjdk25.py native-base
if [[ $profile == instrument ]]; then
  python3 scripts/openjdk25.py java-instrument
  python3 scripts/openjdk25.py native-instrument
  make build/java25-agent-smoke-agent.jar build/java25-agent-smoke-app.jar build/openjdk25-agent-host.json
fi
python3 scripts/build-openjdk25-image.py "${image_options[@]}"
python3 scripts/openjdk25.py components
python3 scripts/openjdk25.py test-host
python3 scripts/openjdk25.py probe
python3 scripts/openjdk25.py "$link"
python3 scripts/test-openjdk25-classlib-host.py
python3 scripts/package.py
python3 scripts/verify-package.py
echo "Genuine Horizon Java25 $profile profile packaged; hardware execution NOT TESTED"
