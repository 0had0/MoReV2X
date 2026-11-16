#!/usr/bin/env bash
# Project-local Octave 10.3 (conda-forge, wilabv2xsim/.octave, built by setup.sh). OCTAVE_HOME must
# be set: without it the conda build cannot locate itself on macOS 26 and recurses until it crashes.
here="$(cd "$(dirname "$0")" && pwd)"
export OCTAVE_HOME="$here/.octave"
exec "$OCTAVE_HOME/bin/octave-cli" --norc --no-gui -q "$@"
