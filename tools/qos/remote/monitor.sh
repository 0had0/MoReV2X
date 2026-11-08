#!/usr/bin/env bash
# Opens the tmux session "qos-monitor": progress every 30 s, the sweep log, and the machine load.
#
#   tools/qos/remote/monitor.sh [sweep name, default qos_full]
#
# Detach with Ctrl-b d, it keeps running. tmux kill-session -t qos-monitor closes it.
set -euo pipefail
cd "$(dirname "$0")/../../.."
name="${1:-qos_full}"
if ! tmux has-session -t qos-monitor 2>/dev/null; then
  tmux new-session -d -s qos-monitor -n status "watch -n 30 tools/qos/remote/status.sh $name"
  tmux split-window -t qos-monitor:status -v "tail -n 40 -F results/qos/$name/sweep.log"
  tmux split-window -t qos-monitor:status -h "$(command -v htop || command -v top)"
  tmux select-layout -t qos-monitor:status main-horizontal
fi
exec tmux attach -t qos-monitor
