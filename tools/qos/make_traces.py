#!/usr/bin/env python3
"""Build a QoS scenario in SUMO and record it as an ns-3 mobility trace.

    tools/qos/make_traces.py highway_platoon --density low --seed 1 --out traces/hp_low_1
    tools/qos/make_traces.py urban_intersection --density high --seed 2 --out traces/ui_high_2
    tools/qos/make_traces.py highway_cpm --density 30 --seed 1 --out traces/hc_30_1

Scenarios
  highway_platoon     straight highway, 3 lanes per direction, truck platoons in the right
                      lane of each direction plus background cars and trucks
  urban_intersection  Manhattan grid with traffic lights, buildings on every block,
                      cars, cyclists, pedestrians and road side units at the central crossings
  highway_cpm         the highway_platoon road without platoons, for dense CPM contention

The density is either a name (low, medium, high) or a number:
  highways  vehicles per km per lane, background traffic only
  urban     vehicles present in the grid at once

Every output folder holds
  trace.csv      time,node_id,x,y,speed   for HIGHWAY/QOS --mobilityTrace (node_id = pool index)
  roles.csv      node_id,sumo_id,role,group,position
                 role is car, truck, platoon, emergency, cyclist, pedestrian or rsu. group is the
                 platoon number (-1 otherwise), position the place in the platoon (0 = leader)
  buildings.csv  urban only: building id and its corners as x y pairs
  meta.json      the inputs, node counts and the area, to check a trace before using it

The SUMO files used to make the trace are kept in sumo/ next to them, so the run can be
opened in sumo-gui: sumo-gui -c <out>/sumo/scenario.sumocfg
"""
import argparse
import csv
import json
import os
import random
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET

DENSITIES = {
    "highway_platoon": {"low": 5, "medium": 10, "high": 20},
    "highway_cpm": {"low": 10, "medium": 20, "high": 30},
    "urban_intersection": {"low": 60, "medium": 120, "high": 180},
}

HIGHWAY_SPEED = 33.33       # m/s, 120 km/h speed limit
PLATOON_SPEED = 22.22       # m/s, 80 km/h for trucks
LANE_WIDTH = 3.2            # SUMO default
GRID_SPACING = 200.0        # m between urban crossings
GRID_NODES = 4              # 4 x 4 crossings, 3 x 3 blocks
BUILDING_SETBACK = 12.0     # m from the road centre line to the facade


def sumo_tool(name):
    home = os.environ.get("SUMO_HOME")
    if not home:
        sys.exit("SUMO_HOME is not set, source .env first")
    return os.path.join(home, "tools", name)


def run(command, cwd):
    done = subprocess.run(command, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if done.returncode != 0:
        sys.exit("%s failed:\n%s" % (" ".join(command), done.stdout[-3000:]))
    return done.stdout


def write_config(folder, net, routes, end, additional=None):
    inputs = ['        <net-file value="%s"/>' % net, '        <route-files value="%s"/>' % routes]
    if additional:
        inputs.append('        <additional-files value="%s"/>' % additional)
    with open(os.path.join(folder, "scenario.sumocfg"), "w") as out:
        out.write("""<?xml version="1.0" encoding="UTF-8"?>
<configuration>
    <input>
%s
    </input>
    <time>
        <begin value="0"/>
        <end value="%g"/>
        <step-length value="0.1"/>
    </time>
    <report>
        <no-step-log value="true"/>
        <no-warnings value="true"/>
    </report>
</configuration>
""" % ("\n".join(inputs), end))


# ---------------------------------------------------------------- highway

def build_highway_net(folder, length):
    with open(os.path.join(folder, "highway.nod.xml"), "w") as out:
        out.write('<nodes>\n    <node id="west" x="0.0" y="0.0" type="priority"/>\n'
                  '    <node id="east" x="%.1f" y="0.0" type="priority"/>\n</nodes>\n' % length)
    with open(os.path.join(folder, "highway.edg.xml"), "w") as out:
        out.write('<edges>\n'
                  '    <edge id="eastbound" from="west" to="east" numLanes="3" speed="%.2f"/>\n'
                  '    <edge id="westbound" from="east" to="west" numLanes="3" speed="%.2f"/>\n'
                  '</edges>\n' % (HIGHWAY_SPEED, HIGHWAY_SPEED))
    run(["netconvert", "--node-files", "highway.nod.xml", "--edge-files", "highway.edg.xml",
         "-o", "highway.net.xml", "--no-turnarounds", "true"], folder)
    return "highway.net.xml"


def highway_routes(folder, density, warmup, end, length, platoons, platoon_size, seed):
    """Background flows sized for `density` vehicles/km/lane plus scheduled platoons.

    A flow of q veh/s at speed v fills a lane with q/v veh/m once it has crossed the road,
    so each lane gets q = density/1000 * v. Cars (85 %) and trucks (15 %) share it.
    """
    rng = random.Random(seed)
    lanes = 3
    car_rate = 0.85 * density / 1000.0 * 0.9 * HIGHWAY_SPEED * lanes
    truck_rate = 0.15 * density / 1000.0 * PLATOON_SPEED * lanes
    lines = ['<routes>',
             '    <vType id="car" accel="2.6" decel="4.5" sigma="0.5" length="4.5" maxSpeed="36.11" speedDev="0.1"/>',
             '    <vType id="truck" accel="1.3" decel="4.0" sigma="0.5" length="12.0" maxSpeed="25.0" '
             'speedDev="0.05" vClass="truck"/>',
             # Platoon trucks: no driver noise, short reaction time and no lane changes, so they
             # keep a tight, stable formation in the right lane
             '    <vType id="platoon" accel="1.3" decel="4.0" sigma="0" tau="0.3" minGap="3" length="12.0" '
             'maxSpeed="%.2f" speedFactor="1" speedDev="0" vClass="truck" lcStrategic="-1" lcSpeedGain="0" '
             'lcKeepRight="0" lcCooperative="0" color="0,120,255"/>' % PLATOON_SPEED,
             '    <route id="east" edges="eastbound"/>',
             '    <route id="west" edges="westbound"/>']
    for direction in ("east", "west"):
        lines.append('    <flow id="%sCars" type="car" route="%s" begin="0" end="%g" period="exp(%.4f)" '
                     'departLane="random" departSpeed="max"/>' % (direction, direction, end, car_rate))
        lines.append('    <flow id="%sTrucks" type="truck" route="%s" begin="0" end="%g" period="exp(%.4f)" '
                     'departLane="random" departSpeed="max"/>' % (direction, direction, end, truck_rate))
    # Platoons enter so that, when the recording starts, they are spread over the middle
    # of the road: leaders at 70 %, 45 % and 20 % of the length in their direction
    vehicles = []
    for k in range(platoons):
        direction = "east" if k % 2 == 0 else "west"
        target = length * (0.7 - 0.25 * (k // 2) - 0.05 * (k % 2))
        leader_depart = warmup - target / PLATOON_SPEED + rng.uniform(-1, 1)
        for i in range(platoon_size):
            depart = max(0.0, leader_depart + i * 1.0)
            vehicles.append((depart, '    <vehicle id="platoon%d_%d" type="platoon" route="%s" depart="%.2f" '
                                     'departLane="0" departSpeed="%.2f"/>' % (k, i, direction, depart, PLATOON_SPEED)))
    for _, line in sorted(vehicles):
        lines.append(line)
    lines.append('</routes>')
    with open(os.path.join(folder, "routes.rou.xml"), "w") as out:
        out.write("\n".join(lines) + "\n")
    return "routes.rou.xml"


# ---------------------------------------------------------------- urban

def build_urban_net(folder):
    run(["netgenerate", "--grid", "--grid.x-number", str(GRID_NODES), "--grid.y-number", str(GRID_NODES),
         "--grid.length", str(GRID_SPACING), "--grid.attach-length", "100",
         "--default.lanenumber", "2", "--default.speed", "13.89", "--tls.guess", "true",
         "--sidewalks.guess", "true", "--no-turnarounds", "true", "-o", "urban.net.xml"], folder)
    return "urban.net.xml"


def urban_buildings(folder, net):
    """One building per block: the square between four crossings, set back from the roads."""
    xs, ys = set(), set()
    for junction in ET.parse(os.path.join(folder, net)).getroot().iter("junction"):
        if junction.get("type") in ("traffic_light", "priority", "right_before_left", "traffic_light_right_on_red"):
            xs.add(round(float(junction.get("x")), 1))
            ys.add(round(float(junction.get("y")), 1))
    xs, ys = sorted(xs), sorted(ys)
    buildings = []
    for i in range(len(xs) - 1):
        for j in range(len(ys) - 1):
            x0, x1 = xs[i] + BUILDING_SETBACK, xs[i + 1] - BUILDING_SETBACK
            y0, y1 = ys[j] + BUILDING_SETBACK, ys[j + 1] - BUILDING_SETBACK
            if x1 - x0 > 20 and y1 - y0 > 20:
                buildings.append([(x0, y0), (x1, y0), (x1, y1), (x0, y1)])
    with open(os.path.join(folder, "buildings.poly.xml"), "w") as out:
        out.write("<additional>\n")
        for n, corners in enumerate(buildings):
            shape = " ".join("%.2f,%.2f" % c for c in corners + [corners[0]])
            out.write('    <poly id="building%d" type="building" color="180,180,180" fill="1" layer="-1" '
                      'shape="%s"/>\n' % (n, shape))
        out.write("</additional>\n")
    return buildings, xs, ys


def urban_routes(folder, net, vehicles, warmup, end, seed):
    """Random trips over the grid. A trip lasts about 100 s with the lights, so a period of 100/N keeps
    about N vehicles in the network. Cyclists are 10 % of the vehicles and pedestrians 20 %."""
    trip_seconds = 100.0
    period = trip_seconds / max(vehicles, 1)
    common = ["-n", net, "-b", "0", "-e", str(end), "--seed", str(seed), "--validate"]
    run([sys.executable, sumo_tool("randomTrips.py")] + common +
        ["-p", "%.4f" % (period / 0.9), "--fringe-factor", "5", "--min-distance", "300",
         "--trip-attributes", 'type="car" departLane="best" departSpeed="max"', "--prefix", "car",
         "--additional-file", "types.add.xml", "-o", "cars.trips.xml", "-r", "cars.rou.xml"], folder)
    run([sys.executable, sumo_tool("randomTrips.py")] + common +
        ["-p", "%.4f" % (period / 0.1), "--vclass", "bicycle", "--min-distance", "200",
         "--trip-attributes", 'type="bike"', "--prefix", "bike",
         "--additional-file", "types.add.xml", "-o", "bikes.trips.xml", "-r", "bikes.rou.xml"], folder)
    run([sys.executable, sumo_tool("randomTrips.py")] + common +
        ["-p", "%.4f" % (period / 0.2 * 3), "--pedestrians", "--max-distance", "400", "--prefix", "ped",
         "-o", "peds.trips.xml", "-r", "peds.rou.xml"], folder)
    return "cars.rou.xml,bikes.rou.xml,peds.rou.xml"


def write_urban_types(folder):
    with open(os.path.join(folder, "types.add.xml"), "w") as out:
        out.write('<additional>\n'
                  '    <vType id="car" accel="2.6" decel="4.5" sigma="0.5" length="4.5" maxSpeed="16.67" speedDev="0.1"/>\n'
                  '    <vType id="bike" vClass="bicycle" accel="1.2" decel="3.0" sigma="0.5" length="1.6" '
                  'maxSpeed="6.0" speedDev="0.1"/>\n'
                  '</additional>\n')


def rsu_positions(xs, ys):
    """Road side units on one corner of each central crossing, 10 m from the centre."""
    inner_x, inner_y = xs[1:-1], ys[1:-1]
    return [(x + 10.0, y + 10.0) for x in inner_x for y in inner_y]


# ---------------------------------------------------------------- recording

def role_of(element, vehicle_type):
    if element.tag == "person":
        return "pedestrian"
    if vehicle_type.startswith("platoon"):
        return "platoon"
    if vehicle_type.startswith("bike"):
        return "cyclist"
    return "truck" if vehicle_type.startswith("truck") else "car"


def record(folder, out, warmup, duration, rsus, emergency_share, seed):
    """Runs SUMO with fcd output and turns the window [warmup, warmup+duration] into the trace."""
    fcd = os.path.join(folder, "fcd.xml")
    run(["sumo", "-c", "scenario.sumocfg", "--seed", str(seed), "--fcd-output", "fcd.xml",
         "--fcd-output.attributes", "x,y,speed,type", "--device.fcd.begin", str(warmup),
         "--end", str(warmup + duration + 1)], folder)
    rng = random.Random(seed * 7919)
    ids, roles = {}, []
    end = warmup + duration
    with open(os.path.join(out, "trace.csv"), "w", newline="") as trace_file:
        trace = csv.writer(trace_file)
        trace.writerow(["time", "node_id", "x", "y", "speed"])
        for _, step in ET.iterparse(fcd, events=("end",)):
            if step.tag != "timestep":
                continue
            time = float(step.get("time"))
            if warmup <= time <= end + 1e-9:
                for element in list(step):
                    sumo_id = element.get("id")
                    if sumo_id not in ids:
                        ids[sumo_id] = len(ids)
                        vehicle_type = element.get("type") or ""
                        role = role_of(element, vehicle_type)
                        group, position = -1, -1
                        if role == "platoon":
                            group, position = (int(v) for v in sumo_id[len("platoon"):].split("_"))
                        elif role == "car" and rng.random() < emergency_share:
                            role = "emergency"
                        roles.append([ids[sumo_id], sumo_id, role, group, position])
                    trace.writerow(["%.1f" % (time - warmup), ids[sumo_id], element.get("x"), element.get("y"),
                                    element.get("speed")])
            step.clear()
        for n, (x, y) in enumerate(rsus):
            node = len(ids) + n
            roles.append([node, "rsu%d" % n, "rsu", -1, -1])
            for time in (0.0, duration + 5.0):  # present for the whole run and its drain
                trace.writerow(["%.1f" % time, node, "%.2f" % x, "%.2f" % y, "0"])
    with open(os.path.join(out, "roles.csv"), "w", newline="") as roles_file:
        writer = csv.writer(roles_file)
        writer.writerow(["node_id", "sumo_id", "role", "group", "position"])
        writer.writerows(roles)
    os.remove(fcd)
    return roles


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("scenario", choices=sorted(DENSITIES))
    parser.add_argument("--density", default="medium")
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--duration", type=float, default=20.0, help="recorded seconds")
    parser.add_argument("--warmup", type=float, default=150.0, help="SUMO seconds before the recording")
    parser.add_argument("--length", type=float, default=2000.0, help="highway length in m")
    parser.add_argument("--platoons", type=int, default=3, help="highway_platoon: number of platoons")
    parser.add_argument("--platoon-size", type=int, default=6)
    parser.add_argument("--emergency-share", type=float, default=0.02,
                        help="share of cars that act as emergency vehicles (PQI 89 senders)")
    parser.add_argument("--out", required=True)
    args = parser.parse_args()

    density = DENSITIES[args.scenario].get(args.density)
    density = float(args.density) if density is None else float(density)
    out = os.path.abspath(args.out)
    sumo_dir = os.path.join(out, "sumo")
    if os.path.isdir(sumo_dir):
        shutil.rmtree(sumo_dir)
    os.makedirs(sumo_dir)
    end = args.warmup + args.duration + 1
    rsus, buildings, area = [], [], None

    if args.scenario.startswith("highway"):
        net = build_highway_net(sumo_dir, args.length)
        platoons = args.platoons if args.scenario == "highway_platoon" else 0
        routes = highway_routes(sumo_dir, density, args.warmup, end, args.length, platoons,
                                args.platoon_size, args.seed)
        write_config(sumo_dir, net, routes, end)
        area = [0.0, -2 * 3 * LANE_WIDTH, args.length, 2 * 3 * LANE_WIDTH]
    else:
        net = build_urban_net(sumo_dir)
        write_urban_types(sumo_dir)
        buildings, xs, ys = urban_buildings(sumo_dir, net)
        routes = urban_routes(sumo_dir, net, int(density), args.warmup, end, args.seed)
        write_config(sumo_dir, net, routes, end, additional="buildings.poly.xml")
        rsus = rsu_positions(xs, ys)
        area = [xs[0], ys[0], xs[-1], ys[-1]]
        with open(os.path.join(out, "buildings.csv"), "w", newline="") as building_file:
            writer = csv.writer(building_file)
            writer.writerow(["building", "corners"])
            for n, corners in enumerate(buildings):
                writer.writerow([n, " ".join("%.2f %.2f" % c for c in corners)])

    roles = record(sumo_dir, out, args.warmup, args.duration, rsus, args.emergency_share, args.seed)
    counts = {}
    for row in roles:
        counts[row[2]] = counts.get(row[2], 0) + 1
    meta = {"scenario": args.scenario, "density": density, "density_name": args.density, "seed": args.seed,
            "duration": args.duration, "warmup": args.warmup, "nodes": len(roles), "roles": counts,
            "area": area, "buildings": len(buildings)}
    with open(os.path.join(out, "meta.json"), "w") as meta_file:
        json.dump(meta, meta_file, indent=2)
    print(json.dumps(meta))


if __name__ == "__main__":
    main()
