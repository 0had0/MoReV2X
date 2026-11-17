#!/usr/bin/env bash
# Progress of tools/rcs/grid.py: runs done, failures, rate, and an estimate of the end.
# Usage: tools/rcs/monitor.sh [results/rcs]   (watch -n 60 tools/rcs/monitor.sh for a live view)
set -euo pipefail
root="${1:-$(cd "$(dirname "$0")/../.." && pwd)/results/rcs}"
log="$root/progress.log"
[ -f "$log" ] || { echo "no $log yet"; exit 1; }

start_line="$(grep ' start ' "$log" | tail -1)"
total="$(echo "$start_line" | awk '{print $4}')"
started="$(echo "$start_line" | awk '{print $1" "$2}')"
done_runs="$(find "$root" -name metrics.json | wc -l | tr -d ' ')"
since="$(awk -v s="$started" '$1" "$2 >= s' "$log" | grep -c ' [0-9]*/[0-9]* ' || true)"
failed="$(awk -v s="$started" '$1" "$2 >= s' "$log" | grep -E ' (failed|error)' || true)"
running="$( (pgrep -f 'scratch/HIGHWAY' || true) | wc -l | tr -d ' ')"

echo "runs with metrics: $done_runs   finished since $started: $since / $total   running now: $running"
if [ -n "$failed" ]; then
  echo "failures:"; echo "$failed" | tail -5
fi
if [ "$since" -gt 0 ]; then
  # times from the log itself: the grid may log in another time zone (UTC in Docker)
  last="$(tail -1 "$log" | awk '{print $1" "$2}')"
  python3 - "$started" "$last" "$since" "$total" <<'EOF'
import sys, datetime as dt
parse = lambda s: dt.datetime.strptime(s, "%Y-%m-%d %H:%M:%S")
start, last = parse(sys.argv[1]), parse(sys.argv[2])
done, total = int(sys.argv[3]), int(sys.argv[4])
elapsed = (last - start).total_seconds()
left = elapsed / done * (total - done)
print("%.1f h for %d runs, about %.1f h left (ends near %s in the log's clock)" % (
      elapsed / 3600, done, left / 3600, (last + dt.timedelta(seconds=left)).strftime("%H:%M")))
EOF
fi
echo "last:"; tail -3 "$log"
df -h "$root" | tail -1 | awk '{print "disk: "$4" free of "$2}'
