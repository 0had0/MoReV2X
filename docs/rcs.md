# Repeated Contention Scheduling (RCS) in MoReV2X

A. Rolich, M. Tricco, S. Paroli, M. Yildiz, A. Baiocchi, "Repeated Contention Scheduling: A
Novel Resource Allocation Algorithm Toward 6G Vehicular Networks", MAIN 2026, arXiv:2607.06103.

RCS replaces the sensing-based selection of Mode 2. Time is cut into frames of Tf = 10 ms. The
first 1 ms of every frame is a contention time and carries no data; the K sub-channels (SCs) of
the other slots are what gets allocated. A UE with a new packet joins the next contention: in each
of r rounds it sends a tone on one sub-carrier picked at random and listens to the others, and it
leaves once it hears w or more distinct lower sub-carriers. With J groups, the K SCs and the M
sub-carriers are split into J sets of w = K/J SCs and m = floor(M/J) sub-carriers, and a UE
contends in one group picked at random. A winner that heard k lower sub-carriers in the last round
takes the k-th SC of its group. A loser tries the next frame, unless that frame starts after the
delay budget, in which case the packet is dropped.

## Where it lives

[`architecture/mode2-qos-schedulers.html`](architecture/mode2-qos-schedulers.html) draws how the QoS
flows reach SB-SPS, DS and RCS in a UE, each box linked to its source. It is rendered with
[Archify](https://github.com/tt-a1i/archify) from `architecture/mode2-qos-schedulers.architecture.json`:

```bash
node ~/.claude/skills/archify/bin/archify.mjs finalize architecture \
  docs/architecture/mode2-qos-schedulers.architecture.json docs/architecture/mode2-qos-schedulers.html \
  --repo-root . --quality showcase
```

| File | What |
|---|---|
| `src/MoReV2X/model/nr-v2x-rcs-contention.{h,cc}` | `NrV2XRcsContention`: the air of the contention, which plays every frame for all UEs with the groups and tones they choose. Attributes `Groups` (J), `Rounds` (r), `Subcarriers` (M), `ScPerFrame` (K), `ToneMode`, `ToneRange`, `ToneThreshold`, `TonePower`, `OutputPath`. Writes `RcsContentionLog.txt`. |
| `src/MoReV2X/model/nr-v2x-rcs-choice.{h,cc}` | `NrV2XRcsChoice`: the distributions the group and the tones are drawn from, uniform. `NrV2XRcsPacket`: what they see of the packet (priority, PDB, time waited, contention number). |
| `src/MoReV2X/model/nr-v2x-ue-mac.{h,cc}` | `NrV2XUeMac::RcsSchedule`, called from `DoSubframeIndication` in place of the sensing-based selection when the `Rcs` attribute is set. `RcsFrame`, `RcsContentionTime`, `RcsChoice`. Chooses the UE's group and tones. Writes `RcsLog.txt`. |
| `scratch/HIGHWAY.cc` | Options `--Rcs`, `--RcsGroups`, `--RcsRounds`, `--RcsSubcarriers`, `--RcsFrame`, `--RcsContention`, `--RcsTone`, `--RcsToneRange`, `--RcsToneThreshold`, `--RcsTonePower`; and for the paper's setup `--Ideal`, `--IdealSide`, `--UniformPeriod`, `--RxFile`, `--RxLog`. |
| `tools/rcs/grid.py` | The paper's grid and the highway variant, resumable, one `metrics.json` per run. |
| `tools/rcs/metrics.py` | PRR, Pcoll, Pdrop, E[PIR], E[AoI] and V(Ath) as defined in the paper. |
| `tools/rcs/monitor.sh` | Progress and estimated end of a grid. |
| `src/MoReV2X/test/nr-v2x-mode2-test-suite.cc`, `tests/test_rcs.py` | Unit tests of the contention, and checks on real runs. |

## How a packet goes through the MAC

1. A new packet ID in the buffer: the UE chooses a group and registers for frame
   `ceil(now / frameSlots)`, the first whose contention time starts at or after now.
2. At the first data slot of that frame it reads its result. The contention object resolves the
   whole frame the first time a UE asks, so every contender of the frame plays the same rounds.
   In each round it asks every UE still in for its tone (`NrV2XUeMac::RcsTone`).
3. Won SC `sc`: slot `frameStart + contentionSlots + sc / scPerSlot`, sub-channel
   `sc % scPerSlot`. The MAC builds a one-shot grant on it (`Cresel = 0`, no re-evaluation, no
   reservation kept), and the existing transmission path sends it. A won SC whose slot is past the
   PDB drops the packet (`RcsLog` outcome 3).
4. Lost: chooses a group again and registers for the next frame if that frame's first data slot is within the PDB
   (outcome 1), otherwise the packet is dropped (outcome 2). Dropping pulls the PDU out of RLC
   and throws it away (`m_rcsDiscard`).

**Several flows.** A UE plays one contention per frame. With several flows (one MAC process per
destination, served in priority order) the contention belongs to one packet at a time:

- the first packet with nothing ahead of it registers;
- a more urgent packet (lower SL priority value) takes the contention over until that frame's
  contention starts: the other one leaves it (`NrV2XRcsContention::Unregister`, `RcsLog` outcome 4)
  and contends again later;
- a packet waiting for the UE's contention is dropped once even the next frame would deliver it
  after its PDB (outcome 5);
- a packet replaced in the RLC by a newer one of its flow (`KeepOnlyNewestSdu`) hands its
  registration to the newer packet.

The numerology only changes the frame geometry. With 20 MHz and 25-RB sub-channels: mu = 0 has
106 RBs, 4 SCs per slot, 1 contention slot and 9 data slots (K = 36, M = 400); mu = 1 has 51 RBs,
2 SCs per slot, 2 contention slots and 18 data slots (K = 36, M = 200). `--RcsSubcarriers=0`
takes one sub-carrier out of three of the band, as the paper does.

## The group and the tones

The UE makes both choices. Its MAC holds an `NrV2XRcsChoice` (attribute `RcsChoice` of
`NrV2XUeMac`; empty means uniform) and draws its group from `GroupDistribution (J, packet)` when it
registers and, in each round, its tone from `ToneDistribution (m, round, packet)` when the
contention asks for it. The contention only checks that the group and the tone are in range. Both return one weight per group or
sub-carrier, sub-carrier 0 being the lowest, the one that beats all others. Both are uniform, as
in the paper. A subclass can skew them with the packet in contention (`NrV2XRcsPacket`): its SL
priority (the default priority level of its PQI), its PDB, how long it has waited and how many
contentions it has lost. Skewing the tones towards the low sub-carriers raises the chance to win;
skewing the groups moves packets to some SCs of the frame. Each UE can hold its own subclass:

```cpp
Ptr<NrV2XUeMac> mac = ...;
mac->SetAttribute ("RcsChoice", PointerValue (CreateObject<MyPriorityChoice> ()));
```

Every UE draws from its own random stream. `RcsLog.txt` ends with the group the UE chose and the
SL priority of the packet: time, node, packet, generation time (ms), frame, contention, outcome,
SC, delay (ms), group, priority. The group is -1 on a row of a packet that did not contend in that
frame (outcomes 2 at registration and 5).

`NrV2XRcsSkewedChoice` skews the tones by the priority p of the packet (1 highest .. 8): tone k of
the group's m sub-carriers has a probability proportional to the skew-normal density at
x = (k + 0.5) / m, centred on (p - 1) / 7, with `Scale` 0.25 and `Shape` 4 by default. Priority 2
then draws tones near 0.33 of the group on average and priority 5 near 0.73; the group stays
uniform.

## Who hears a tone

`--RcsTone`:

- `all`: every UE hears every tone. This is the paper's model, and it holds on the highway too,
  however far apart the UEs are.
- `range`: a UE hears the tones sent within `--RcsToneRange` metres.
- `power`: a UE hears a tone when `--RcsTonePower` (23 dBm) minus the scenario's path loss and
  shadowing (`NrV2XPropagationLossModel`, the same as the data) reaches `--RcsToneThreshold`. A
  single 15 kHz tone at 23 dBm with a 9 dB noise figure has a noise floor near -123 dBm, so
  -110 dBm is a detection margin of about 13 dB.

Who hears whom is fixed for the whole contention time of a frame. Two winners that cannot hear
each other can take the same SC: on the highway that is spatial reuse when they are far apart, and
a hidden-terminal collision when their receivers hear both.

## The paper's model and MoReV2X

`--Ideal` places the UEs, static, in a 10 m square without shadowing, so all of them are within
range of each other. `tools/rcs/metrics.py --model ideal` then takes the paper's all-or-nothing
view from the transmission logs. A packet is received when no other transmission used its slot
and sub-channel. Half duplex is ignored, and the PHY's SINR plays no part. `--model highway`
counts what the PHY decoded instead (`ReceivedLog.txt`), per distance and per flow within 150,
300 and 500 m.

Where MoReV2X differs from the paper's simulator:

- SPS and DS are MoReV2X's 3GPP implementations: sensing over 1 s, the RSRP threshold raised in
  3 dB steps until 20 % of the candidates are left, and RC drawn in [5, 15] for RRI = 100 ms. The
  paper's SPS drops a packet when too few resources are left, but MoReV2X always selects one, so
  its Pdrop is 0. MoReV2X's DS senses too, while the paper's DS picks at random.
- SPS and DS use K = 40 SCs per frame (no contention time) and RCS uses K = 36, as in the paper.
  So for one load a the node count is n = 400 a for SPS and DS, and n = 360 a for RCS.
- `--UniformPeriod=100` sends every packet every 100 ms. Without it, HIGHWAY keeps the upstream
  split (20 % of the UEs every 20 ms).
- Packets generated in the first second are not counted, so that SPS has its sensing history.

## Results

`notebooks/07_rcs_reproduction.ipynb` compares the grid with the paper's Figs 2 to 8, claim by
claim, and shows the highway with each tone setting: seed 1 of every run, and a second seed of the
runs of Figs 7 and 8. Its figures go to `figures/rcs/`, which git does not track.

## RCS in QOS

`scratch/QOS` takes the same options as HIGHWAY (`--Rcs`, `--RcsGroups`, `--RcsRounds`,
`--RcsSubcarriers`, `--RcsFrame`, `--RcsContention`, `--RcsTone`, `--RcsToneRange`,
`--RcsToneThreshold`, `--RcsTonePower`), plus `--RcsChoice=uniform|skewed`, `--RcsSkewScale`,
`--RcsSkewShape`, `--Ideal` (no shadowing) and `--RxLog=0` (no `ReceivedLog.txt`). Under RCS it
refuses a flow whose packets do not fit one sub-channel and prints that limit (509 B of payload on
25 RBs at MCS 13). A flow can be limited to a window of the run (`start_s`, `stop_s` in the flows
file), and the traffic of every flow draws from fixed random streams, so 3GPP and RCS runs of one
seed carry exactly the same packets.

## The priority experiment

`tools/rcs/priority_grid.py` runs two flows of different priority on every vehicle:
periodic1 (PQI 90, SL priority 5, PDB 100 ms, SB-SPS) all the time, and aperiodic1 (PQI 89,
priority 2, PDB 50 ms, dynamic, sizes capped at 400 B) on 70 % of the vehicles from 10 to 20 s.
It compares the 3GPP selection (re-evaluation and pre-emption off, re-evaluation, both; each
also with one blind retransmission of aperiodic1, whose announcement in the SCI is what lets
pre-emption act) with RCS
(J = 1 and 36, uniform and skewed tones) in the paper's square and on a 2 km highway, where RCS
tones are also heard within 600, 300 or 150 m only. `tools/rcs/priority_metrics.py` gives the
paper's metrics per flow, for both flows and per phase (before, during, after the burst), the
re-evaluation and pre-emption checks and changes, RCS's contention statistics per flow and the
CBR every 100 ms.

```bash
tools/rcs/priority_grid.py --jobs 30 --seeds 1 2      # 192 runs, results/rcs_priority/
tools/rcs/priority_grid.py --summary                  # summary.csv and the stacked tables
```

## Running

```bash
tools/rcs/grid.py --list | tail -1                 # 315 runs per seed
tools/rcs/grid.py --jobs 30 --sim-time 50 --short-sim-time 20
tools/rcs/monitor.sh                               # progress and estimated end
tools/rcs/plots.py results/rcs/summary.csv --out docs/figures/rcs
```

The ideal square is expensive. Every UE decodes every transmission, so a run costs about
n^2 per simulated second, and the densest 50 s run (SPS, a = 1.1, 440 UEs) takes 3.5 to 4 h on one thread of an i9-13900K
running alongside 29 others. `--short-sim-time` gives the runs outside the paper's Figs 7 and 8
(the rest of the J sweep, and SPS and DS at mu = 0) a shorter time. The Fig 7-8 runs and the
highway keep `--sim-time` (the highway's PHY skips far receivers and runs about 8 times faster).
The queue starts with the longest runs.

On ease-rtx the grid runs in the `morev2x-qos` Docker image of the QoS branch, with this branch
in a separate clone mounted at `/work` (the image's entrypoint ignores `-w`, hence the `cd`):

```bash
docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp -v "$PWD:/work" morev2x-qos bash -c \
  "cd /work && python2 ./waf configure --disable-python --disable-examples --disable-tests \
     --build-profile=optimized --out=build/optimized --enable-modules=MoReV2X,traci \
   && python2 ./waf build -j32 \
   && /opt/morev2x/.venv/bin/python tools/rcs/grid.py --jobs 30 --sim-time 50 --short-sim-time 20"
```

The same build gives the same numbers on the Mac (clang) and in Docker (gcc 11), run for run.
