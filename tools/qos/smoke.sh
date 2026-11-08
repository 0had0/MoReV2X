#!/usr/bin/env bash
# Runs scenarios/qos/sweep_smoke.yaml and checks that every run finished with sane metrics.
# Do this before every long sweep, locally (source .env first) or in the Docker image:
#
#   tools/qos/smoke.sh                      local build
#   docker run --rm morev2x-qos tools/qos/smoke.sh
#
# Exit status 0 means: all runs done, every class of every run has attempts, PDR in [0, 1],
# root causes adding up to the attempts, CBR in [0, 1], and the summary tables present.
set -euo pipefail
cd "$(dirname "$0")/../.."
jobs="${1:-4}"
out="results/qos/qos_smoke"
rm -rf "$out"
python3 tools/qos/sweep.py scenarios/qos/sweep_smoke.yaml --jobs "$jobs"
python3 tools/qos/check_summary.py "$out" --expect-runs "$(python3 tools/qos/sweep.py scenarios/qos/sweep_smoke.yaml --dry-run | tail -1 | cut -d' ' -f1)"
