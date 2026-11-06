#!/usr/bin/env python3
"""Convert a SUMO fcd-output file into the trace format of HIGHWAY --mobilityTrace.

Output rows are time,node_id,x,y,speed. node_id is a 0-based index given to each
vehicle in order of first appearance, so it is the position in the UE pool.
"""
import argparse
import csv
import xml.etree.ElementTree as ET


def convert(fcd_path, csv_path, begin=0.0, end=float("inf")):
    ids = {}
    with open(csv_path, "w", newline="") as out:
        writer = csv.writer(out)
        writer.writerow(["time", "node_id", "x", "y", "speed"])
        for _, element in ET.iterparse(fcd_path, events=("end",)):
            if element.tag != "timestep":
                continue
            time = float(element.get("time"))
            if begin <= time <= end:
                for vehicle in element.findall("vehicle"):
                    index = ids.setdefault(vehicle.get("id"), len(ids))
                    writer.writerow([round(time - begin, 3), index, vehicle.get("x"),
                                     vehicle.get("y"), vehicle.get("speed")])
            element.clear()
    return len(ids)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("fcd")
    parser.add_argument("csv")
    parser.add_argument("--begin", type=float, default=0.0)
    parser.add_argument("--end", type=float, default=float("inf"))
    args = parser.parse_args()
    vehicles = convert(args.fcd, args.csv, args.begin, args.end)
    print("%d vehicles written to %s" % (vehicles, args.csv))


if __name__ == "__main__":
    main()
