#!/usr/bin/env bash
# All runs of experiment/runs.txt, N at a time (default: cores - 2). Resumable: finished runs are skipped.
#   experiment/run_sweep.sh [N]
cd "$(dirname "$0")/.."
jobs=${1:-$(( $(getconf _NPROCESSORS_ONLN) - 2 ))}
grep -v '^#' experiment/runs.txt | grep . | xargs -P "$jobs" -L 1 experiment/run_one.sh | tee -a results/sweep.log
echo "sweep finished: $(ls -d results/*/done 2>/dev/null | wc -l) of $(grep -v '^#' experiment/runs.txt | grep -c .) runs done"
