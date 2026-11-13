#!/usr/bin/env bash
# Build the genuine experimental Horizon VM; no device execution is implied.
set -euo pipefail
cd "$(dirname "$0")/.."

with_instrument=0
with_minecraft=0
for option in "$@"; do
  case "$option" in
    --with-instrument) with_instrument=1 ;;
    --with-minecraft) with_minecraft=1 ;;
    *) echo 'Usage: scripts/build-openjdk25-runtime.sh [--with-instrument] [--with-minecraft]' >&2; exit 2 ;;
  esac
done
profile=base
if [[ $with_instrument == 1 ]]; then profile=instrument; fi
if [[ $with_minecraft == 1 ]]; then profile=minecraft; fi
if [[ $with_minecraft == 1 && $with_instrument == 1 ]]; then profile=minecraft-instrument; fi

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
if [[ $with_instrument == 1 ]]; then
  configure=configure-agent
  link=link-runtime-agent
  image_options=(--with-instrument)
fi
if [[ $with_minecraft == 1 ]]; then
  configure=configure-minecraft
  link=link-runtime-minecraft
  if [[ $with_instrument == 1 ]]; then link=link-runtime-agent-minecraft; fi
  image_options+=(--with-minecraft)
fi
python3 scripts/openjdk25.py "$configure"
if [[ $with_minecraft == 1 ]]; then
  python3 scripts/openjdk25.py build-minecraft
else
  python3 scripts/openjdk25.py build
fi
python3 scripts/openjdk25.py java-base
python3 scripts/openjdk25.py native-base
if [[ $with_instrument == 1 ]]; then
  python3 scripts/openjdk25.py java-instrument
  python3 scripts/openjdk25.py native-instrument
  make build/java25-agent-smoke-agent.jar build/java25-agent-smoke-app.jar build/openjdk25-agent-host.json
fi
if [[ $with_minecraft == 1 ]]; then
  python3 scripts/openjdk25.py java-minecraft
  python3 scripts/openjdk25.py native-management
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
