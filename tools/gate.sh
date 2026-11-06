#!/usr/bin/env bash
# The commit gate: build, ns-3 test suites and examples, then the MoReV2X scenario
# tests. A pass is recorded for the staged tree, so tools/hooks/pre-commit does not
# run it twice. Usage: tools/gate.sh [--quick]   (--quick skips the ns-3 suites)
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
# shellcheck disable=SC1091
source .env

mode="${1:-full}"

# The gate tests the working tree, so a pass only says something about the staged
# tree when the two are the same.
if [ "$mode" != "--quick" ] && [ -n "$(git status --porcelain | grep -v '^[MADRC] ' || true)" ]; then
  echo "gate: the working tree differs from the index. Stage or stash everything first:" >&2
  git status --short | grep -v '^[MADRC] ' >&2
  exit 1
fi
log_dir="$MOREV2X_ROOT/build/gate"
mkdir -p "$log_dir"

step () { printf '\n== %s\n' "$1"; }

step "build"
ninja -C cmake-cache -j"$MOREV2X_JOBS" > "$log_dir/build.log" 2>&1 || { tail -30 "$log_dir/build.log"; exit 1; }
tail -1 "$log_dir/build.log"

if [ "$mode" != "--quick" ]; then
  step "ns-3 test suites and examples"
  set +e
  ./test.py --no-build > "$log_dir/ns3-tests.log" 2>&1
  status=$?
  set -e
  grep -E 'of [0-9]+ tests passed' "$log_dir/ns3-tests.log" || true
  rm -f ./*.unimo ./[DU]l*Stats.txt ./*.pcap ./*.tr # hundreds of MB written by ns-3 examples
  if [ "$status" -ne 0 ]; then
    # Anything failing that is not listed in tools/known-failures.txt stops the gate
    failed="$(grep -E '\[[0-9]+/[0-9]+\] (FAIL|CRASH|TIMEOUT): |^(FAIL|CRASH|TIMEOUT): ' "$log_dir/ns3-tests.log" | sed -E 's/^.*(FAIL|CRASH|TIMEOUT): (TestSuite|Example) //' | sort -u)"
    known="$( (grep -vE '^(#|$)' tools/known-failures.txt || true) | awk '{print $1}' | sort -u)"
    unexpected="$(comm -23 <(printf '%s\n' "$failed") <(printf '%s\n' "$known"))"
    printf 'known failures: %s\n' "$(comm -12 <(printf '%s\n' "$failed") <(printf '%s\n' "$known") | tr '\n' ' ')"
    if [ -n "$unexpected" ]; then
      printf 'unexpected failures:\n%s\n' "$unexpected"
      exit 1
    fi
  fi
fi

step "MoReV2X scenario tests"
"$MOREV2X_VENV/bin/python" -m unittest discover -s tests 2>&1 | tee "$log_dir/scenario-tests.log" | tail -4
if ! grep -q '^OK' "$log_dir/scenario-tests.log"; then
  exit 1
fi

if [ "$mode" != "--quick" ]; then
  git write-tree > "$MOREV2X_ROOT/.gate-stamp"
  step "gate passed for tree $(cat "$MOREV2X_ROOT/.gate-stamp")"
fi
