#!/usr/bin/env bash
# Progress of the sweep, from the run folders: done, running (log written in the last 10 min),
# failed (no "done" and a log that stopped), not started.
cd "$(dirname "$0")/.."
total=$(grep -v '^#' experiment/runs.txt | grep -c .)
done_=0; running=0; failed=0; failedlist=""
for d in results/*_rho*_s*/; do
  [ -d "$d" ] || continue
  if [ -f "$d/done" ]; then done_=$((done_+1))
  elif [ -n "$(find "$d/log.txt" -mmin -10 2>/dev/null)" ]; then running=$((running+1))
  else failed=$((failed+1)); failedlist="$failedlist $(basename "$d")"
  fi
done
echo "$done_ / $total done, $running running, $failed failed, $((total-done_-running-failed)) not started"
[ -n "$failedlist" ] && echo "failed:$failedlist (rerun: experiment/run_sweep.sh)"
exit 0
