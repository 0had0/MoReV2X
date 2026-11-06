#!/usr/bin/env bash
# First-time setup on a fresh clone: Python 3 venv, SUMO, ns-3 configure, git hooks.
set -euo pipefail
cd "$(dirname "$0")/.."
# shellcheck disable=SC1091
source .env

if [ ! -x .venv/bin/python ]; then
  uv venv .venv --python 3.11
fi
VIRTUAL_ENV="$PWD/.venv" uv pip install -q -r tools/requirements.txt

git config core.hooksPath tools/hooks

./ns3 configure --build-profile=optimized --enable-tests --enable-modules='MoReV2X;traci' --disable-werror
echo "Ready. Next: ninja -C cmake-cache -j$MOREV2X_JOBS"
