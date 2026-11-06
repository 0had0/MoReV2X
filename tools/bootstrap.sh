#!/usr/bin/env bash
# First-time setup on a fresh clone: Python 3 venv, SUMO, waf configure, git hooks.
set -euo pipefail
cd "$(dirname "$0")/.."
# shellcheck disable=SC1091
source .env

if [ ! -x .venv/bin/python ]; then
  uv venv .venv --python 3.11
fi
VIRTUAL_ENV="$PWD/.venv" uv pip install -q -r tools/requirements.txt

git config core.hooksPath tools/hooks

./waf configure --disable-python --enable-examples --enable-tests \
  --build-profile=optimized --out=build/optimized
echo "Ready. Next: ./waf build -j$MOREV2X_JOBS"
