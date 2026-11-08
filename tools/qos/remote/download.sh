#!/usr/bin/env bash
# Copies the results of a sweep from the Ubuntu host to this machine: summaries, per-run
# metrics, commands and errors. Raw logs stay on the host (they are deleted unless keep_raw).
#
#   tools/qos/remote/download.sh user@host [sweep name, default qos_full] [remote repository path]
#
# Safe to run while the sweep is going: it only copies, and a second call fetches what changed.
set -euo pipefail
cd "$(dirname "$0")/../../.."
host="${1:?usage: download.sh user@host [sweep name] [remote path]}"
name="${2:-qos_full}"
remote="${3:-~/Documents/MoReV2X-hadih}"
mkdir -p "results/qos/$name"
rsync -az --info=progress2 --prune-empty-dirs \
  --include='*/' \
  --include='summary/***' --include='flows/***' \
  --include='runs/*/metrics/***' --include='runs/*/command.txt' --include='runs/*/error.txt' \
  --include='runs/*/done' --include='traces/*/meta.json' --include='traces/*/roles.csv' \
  --include='progress.json' --include='sweep.yaml' --include='sweep.log' \
  --exclude='*' \
  "$host:$remote/results/qos/$name/" "results/qos/$name/"
echo "Copied to results/qos/$name. Rebuild the summary from the copied runs with:"
echo "  tools/qos/sweep.py results/qos/$name/sweep.yaml --summarise --out results/qos/$name"
