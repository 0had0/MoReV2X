#!/usr/bin/env python3
"""A QOS trace of static vehicles in a square, the RCS paper's setting where everybody hears
everybody (Rolich et al. 2026, Section III).

    tools/rcs/static_square.py --vehicles 180 --side 10 --duration 31 --seed 1 --out traces/square_180_1

Writes trace.csv (time,node_id,x,y,speed every 0.1 s, speed 0) and roles.csv (every node a car),
the format of tools/qos/make_traces.py.
"""
import argparse
import csv
import json
import os
import random


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--vehicles", type=int, required=True)
    parser.add_argument("--side", type=float, default=10.0, help="m")
    parser.add_argument("--duration", type=float, required=True, help="s")
    parser.add_argument("--step", type=float, default=0.1, help="s between two rows of a vehicle")
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--out", required=True)
    args = parser.parse_args(argv)

    rng = random.Random(args.seed)
    positions = [(rng.uniform(0, args.side), rng.uniform(0, args.side)) for _ in range(args.vehicles)]
    os.makedirs(args.out, exist_ok=True)
    with open(os.path.join(args.out, "trace.csv"), "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["time", "node_id", "x", "y", "speed"])
        steps = int(round(args.duration / args.step)) + 1
        for k in range(steps):
            t = round(k * args.step, 6)
            for node, (x, y) in enumerate(positions):
                writer.writerow([t, node, round(x, 3), round(y, 3), 0])
    with open(os.path.join(args.out, "roles.csv"), "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["node_id", "sumo_id", "role", "group", "position"])
        for node in range(args.vehicles):
            writer.writerow([node, "square%d" % node, "car", -1, -1])
    json.dump(vars(args), open(os.path.join(args.out, "meta.json"), "w"), indent=1)


if __name__ == "__main__":
    main()
