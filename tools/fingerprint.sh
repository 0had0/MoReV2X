#!/usr/bin/env bash
# Prints a hash of the Mode 2 logs of three fixed runs. Use it to show that a change
# leaves default behaviour identical: run before and after, then compare the output.
# Rows are sorted because the order of ReceivedLog.txt varies between runs of one binary:
# receivers flush in pointer order. The content does not vary.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
# shellcheck disable=SC1091
source .env
workdir="$(mktemp -d)"
cd "$workdir"
run () {
  local name="$1"; shift
  rm -rf results
  "$MOREV2X_BUILD/scratch/ns3.42-HIGHWAY-optimized" --Vehicles=30 --simTime=5 --mcs=13 --seed=3 "$@" > /dev/null 2>&1
  printf '%-12s %s\n' "$name" "$(cat results/*/SSPSlog.txt results/*/ReceivedLog.txt results/*/phyDebugShort.txt | sort | shasum -a 256 | cut -c1-16)"
}
run sps --Periodic
run retx_reeval --Periodic --ReTx --ReEvaluation
run aperiodic --Aperiodic
rm -rf "$workdir"
