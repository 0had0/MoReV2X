#!/usr/bin/env bash
# Live view of the sweep, refreshed every 30 s:  experiment/monitor.sh
# (or in its own tmux session: tmux new -d -s wilab-monitor experiment/monitor.sh)
cd "$(dirname "$0")/.."
while true; do
  clear
  date
  experiment/status.sh
  echo
  echo "running (simulated time and the simulator's own estimate of what is left):"
  for log in results/*/log.txt; do
    dir=$(dirname "$log")
    [ -f "$dir/done" ] && continue
    line=$(grep -ao "[0-9.]* / [0-9.]*s, end estimated in [a-z0-9 ,]*" "$log" | tail -1)
    [ -n "$line" ] && printf '  %-34s %s\n' "$(basename "$dir")" "$line"
  done
  echo
  uptime
  sleep 30
done
