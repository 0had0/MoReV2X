# Running the QoS sweep on the Ubuntu host

Everything runs in Docker, so the host only needs `git`, `docker` and `tmux`. The image builds
ns-3.22 with gcc 11 and Python 2.7 for waf, and brings SUMO and Python 3 for the traces and the
metrics. The same smoke sweep gives identical numbers on the Mac and in the image.

Budget: `scenarios/qos/sweep_full.yaml` is 144 runs (3 scenarios, 2 densities, conditions or
variants, 3 policies, 3 seeds). On this Mac a dense run takes about 20 minutes and its metrics
about a minute, which puts the sweep at 3 to 4.5 hours on 30 jobs. It is sized for 8 hours.

## 1. Once: clone and check

```bash
cd ~/Documents && git clone git@github.com:0had0/MoReV2X.git MoReV2X-hadih
# without a GitHub SSH key on the host: git clone https://github.com/0had0/MoReV2X.git MoReV2X-hadih
cd ~/Documents/MoReV2X-hadih && git checkout feature/qos-evaluation
docker --version && tmux -V            # both must answer
```

Your user must be allowed to run docker (`groups` lists `docker`), otherwise prefix the docker
commands of the scripts with sudo or ask the administrator to add you to the group.

## 2. Smoke sweep, about 5 minutes

```bash
tools/qos/remote/launch.sh scenarios/qos/sweep_smoke.yaml 8
tmux attach -t qos                      # watch it, Ctrl-b d to leave it running
```

The first launch builds the image (a few minutes). The end of the output must say
`18 runs summarised` and nothing about failed runs. Then close that session:

```bash
tools/qos/remote/status.sh qos_smoke
tmux kill-session -t qos
```

## 3. Full sweep

```bash
tools/qos/remote/launch.sh                       # scenarios/qos/sweep_full.yaml, 30 jobs, tmux session "qos"
tools/qos/remote/monitor.sh                      # tmux session "qos-monitor": progress, log, load
```

You can log out: both sessions keep running. Later:

```bash
tmux attach -t qos-monitor        # progress every 30 s, ETA, the last failures, free disk
tmux attach -t qos                # the sweep's own output
tools/qos/remote/status.sh        # one status screen without tmux
```

Stop and resume:

```bash
docker stop morev2x-qos-qos_full     # runs already done are kept
tmux kill-session -t qos
tools/qos/remote/launch.sh            # starts again where it stopped, retries failed runs
```

Fewer jobs (less memory, about 2 GB per simulation and up to 8 GB per metric extraction, at most
`metrics_jobs: 6` extractions at a time):

```bash
tools/qos/remote/launch.sh scenarios/qos/sweep_full.yaml 24
```

## 4. Results back on the Mac

```bash
cd ~/Documents/MoReV2X-hadih
tools/qos/remote/download.sh <user>@<host> qos_full
QOS_SWEEP=qos_full .venv/bin/jupyter lab docs/notebooks/11_qos_results.ipynb
```

`download.sh` copies the summaries and the per-run metrics, not the raw logs. It can run while the
sweep is still going and fetches only what changed. To rerun the notebooks from the copied runs
alone:

```bash
.venv/bin/python tools/qos/sweep.py results/qos/qos_full/sweep.yaml --summarise --out results/qos/qos_full
QOS_SWEEP=qos_full .venv/bin/jupyter nbconvert --to notebook --execute --inplace docs/notebooks/1[12]_*.ipynb
```

## What is where on the host

| Path | What |
|---|---|
| `results/qos/qos_full/progress.json` | done, failed, running, ETA |
| `results/qos/qos_full/sweep.log` | the sweep output |
| `results/qos/qos_full/runs/<id>/` | `command.txt`, `stdout.log`, `metrics/`, `error.txt` if it failed |
| `results/qos/qos_full/summary/` | every run concatenated: `flows.csv`, `by_distance.csv`, `causes.csv`, ... |
| `results/qos/qos_full/traces/` | the SUMO traces, made once per scenario, density and seed |

A run id names its parameters: `<scenario>_<density>_<condition>_<variant>_<policy>_s<seed>`.
To rerun one run by hand: `bash results/qos/qos_full/runs/<id>/command.txt` inside the container
(`docker run --rm -it -v "$PWD/results:/opt/morev2x/results" morev2x-qos bash`).
