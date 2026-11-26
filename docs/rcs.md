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

| File | What |
|---|---|
| `src/MoReV2X/model/nr-v2x-rcs-contention.{h,cc}` | `NrV2XRcsContention`: the contention of every frame for all UEs. Attributes `Groups` (J), `Rounds` (r), `Subcarriers` (M), `ScPerFrame` (K), `ScPerSlot`, `ToneMode`, `ToneRange`, `ToneThreshold`, `TonePower`, `PriorityAware`, `PrioritySigma`, `OutputPath`. Supports priority-weighted subcarrier picks and contiguous intra-slot multi-SC grants. Writes `RcsContentionLog.txt`. |
| `src/MoReV2X/model/nr-v2x-ue-mac.{h,cc}` | `NrV2XUeMac::RcsSchedule`, called from `DoSubframeIndication` per sidelink flow in priority order when `Rcs` is set. `RcsFrame`, `RcsContentionTime`. Supports multi-flow UE contention sharing (higher-priority preemption before contention starts) and multi-SC TBs. Writes `RcsLog.txt` (and `MacDropLog.txt` on drops). |
| `scratch/HIGHWAY.cc`, `scratch/QOS.cc` | Options `--Rcs`, `--RcsGroups`, `--RcsRounds`, `--RcsSubcarriers`, `--RcsFrame`, `--RcsContention`, `--RcsTone`, `--RcsToneRange`, `--RcsToneThreshold`, `--RcsTonePower`, `--RcsPriorityAware`, `--RcsPrioritySigma`; and `--Ideal`, `--RxLog`, `--RxTimeResolution` (`1e-5` s default). |
| `tools/rcs/grid.py` | The paper's grid and the highway variant, resumable, one `metrics.json` per run. |
| `tools/rcs/metrics.py` | PRR, Pcoll, Pdrop, E[PIR], paper E[AoI] / V(Ath) and delay-aware `aoi_full_ms` / `aoi_full_violation_*`. |
| `tools/rcs/monitor.sh` | Progress and estimated end of a grid. |
| `src/MoReV2X/test/nr-v2x-mode2-test-suite.cc`, `tests/test_rcs.py`, `tests/test_qos.py` | Unit tests of contention, priority, multi-SC slot packing, and real HIGHWAY / QOS runs. |

## How a packet goes through the MAC

1. A new packet ID in an LC buffer: the MAC checks single-slot TB fit (`oversize` drops to `MacDropLog.txt`) and `neededScs <= w`. If no higher/equal-priority flow on the UE holds the next frame (`ceil(now / frameSlots)`) and the first data slot is within the PDB, it registers with its SL priority and `neededScs`; if a lower-priority flow on the same UE holds a frame whose contention has not started yet, the higher-priority flow takes it over (`outcome 4`).
2. At the first data slot of that frame (`dataStart`) it reads its result (`GetResult`). The contention object resolves the whole frame once: when `neededScs > 1`, lower tones account for contiguous intra-slot SC blocks (`ScPerSlot`) and winners never straddle slot boundaries.
3. Won starting SC `sc`: slot `frameStart + contentionSlots + sc / scPerSlot`, starting sub-channel `sc % scPerSlot`, length `neededScs * subchannelSize` RBs. The MAC builds a one-shot grant (`Cresel = 0`, no re-evaluation even with `--AllSlots`, no reservation kept), and the existing transmission path sends it. A won SC whose slot is past the PDB drops the packet (`RcsLog` outcome `3` and `MacDropLog.txt` `rcs-late-win`).
4. Lost (`outcome 1`): re-registers for the next frame if its first data slot fits within the PDB, otherwise drops (`outcome 2`, `rcs-pdb-expired`). Packets dropped because no free frame remains within PDB while another flow holds contention log `outcome 5` (`rcs-preempted-pdb`). Dropping pulls the PDU out of RLC and discards it (`m_rcsDiscard`).

Note on CBR (`NrV2XUePhy::CbrResourceRatio`): because RCS contention slots carry no PSSCH/PSCCH energy in ns-3, `CbrResourceRatio = (frameSlots - contentionSlots) / frameSlots` scales the CBR denominator to active data slots.

Note on model scope & physical-layer abstractions vs 3GPP NR-V2X: multi-SC contention assumes a UE can infer whether a lower tone belongs to a multi-SC request (e.g. tone signaling) and keeps half-duplex/channel abstractions described in Rolich et al. (full-duplex tone TX/RX per round, unattenuated single-tone `TonePower`, pairwise `Hears()` without multi-tone aggregation). Under RCS each packet sends once (`m_TxNumber = 1`, no Mode 2 blind HARQ retransmissions).

The numerology only changes the frame geometry. With 20 MHz and 25-RB sub-channels: mu = 0 has
106 RBs, 4 SCs per slot, 1 contention slot and 9 data slots (K = 36, M = 400); mu = 1 has 51 RBs,
2 SCs per slot, 2 contention slots and 18 data slots (K = 36, M = 200). `--RcsSubcarriers=0`
takes one sub-carrier out of three of the band, as the paper does.

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
