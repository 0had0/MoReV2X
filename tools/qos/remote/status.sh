#!/usr/bin/env bash
# One status screen of a sweep: progress, ETA, failures, disk. Used by monitor.sh.
#
#   tools/qos/remote/status.sh [sweep name, default qos_full]
set -uo pipefail
cd "$(dirname "$0")/../../.."
name="${1:-qos_full}"
folder="results/qos/$name"
if [ ! -f "$folder/progress.json" ]; then
  echo "No progress yet in $folder (traces are made first)."
  ls "$folder/traces" 2>/dev/null | sed 's/^/  trace /'
  exit 0
fi
python3 - "$folder" <<'PY'
import json, os, sys, time
folder = sys.argv[1]
p = json.load(open(os.path.join(folder, "progress.json")))
done, failed, total = p["done"], p["failed"], p["total"]
print("sweep      %s" % os.path.basename(folder))
print("updated    %s" % p["updated"])
print("runs       %d done, %d failed, %d running, %d to go, of %d" % (done, failed, p["running"], p["remaining"], total))
bar = int(40 * (done + failed) / max(total, 1))
print("           [%s%s] %.0f %%" % ("#" * bar, "." * (40 - bar), 100.0 * (done + failed) / max(total, 1)))
if p["mean_run_s"]:
    elapsed = p["elapsed_s"]
    rate = (done + failed) / max(elapsed, 1)
    eta = p["remaining"] / rate if rate > 0 else float("nan")
    print("mean run   %.0f s, elapsed %.1f h, ETA %.1f h" % (p["mean_run_s"], elapsed / 3600, eta / 3600))
runs = os.path.join(folder, "runs")
errors = [r for r in sorted(os.listdir(runs)) if os.path.exists(os.path.join(runs, r, "error.txt"))] if os.path.isdir(runs) else []
for r in errors[-5:]:
    print("failed     %s: %s" % (r, open(os.path.join(runs, r, "error.txt")).read().strip()[:120]))
PY
echo
df -h "$folder" | tail -1 | awk '{print "disk       " $4 " free of " $2}'
docker stats --no-stream --format 'container  {{.Name}} cpu {{.CPUPerc}} mem {{.MemUsage}}' 2>/dev/null | head -3
