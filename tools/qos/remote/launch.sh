#!/usr/bin/env bash
# Builds the Docker image and starts a QOS sweep in the detached tmux session "qos".
#
#   tools/qos/remote/launch.sh                                   full sweep, 30 jobs
#   tools/qos/remote/launch.sh scenarios/qos/sweep_smoke.yaml 4  the smoke sweep
#
# Run it from the repository on the Ubuntu host. Needs docker and tmux. Detach-safe: closing
# the SSH session leaves the sweep running. Attach with: tmux attach -t qos
set -euo pipefail
cd "$(dirname "$0")/../../.."
config="${1:-scenarios/qos/sweep_full.yaml}"
jobs="${2:-30}"
name="$(awk '/^name:/ {print $2; exit}' "$config")"
[ -n "$name" ] || { echo "no name: in $config" >&2; exit 1; }
if tmux has-session -t qos 2>/dev/null; then
  echo "A tmux session 'qos' is already running. Attach with: tmux attach -t qos" >&2
  exit 1
fi
docker build -t morev2x-qos . | tail -3
mkdir -p "results/qos/$name"
# --user: the results belong to you, not to root. The sweep resumes if started again.
tmux new-session -d -s qos \
  "docker run --rm --name morev2x-qos-$name --user $(id -u):$(id -g) -e HOME=/tmp \
     -v '$PWD/results:/opt/morev2x/results' morev2x-qos \
     tools/qos/sweep.py '$config' --jobs $jobs 2>&1 | tee -a 'results/qos/$name/sweep.log'; \
   echo; echo 'Sweep finished. Press enter to close this session.'; read"
echo "Sweep '$name' started in tmux session 'qos'."
echo "  watch it:   tools/qos/remote/monitor.sh $name"
echo "  attach:     tmux attach -t qos      (detach again with Ctrl-b d)"
echo "  stop it:    docker stop morev2x-qos-$name   (runs already done are kept)"
