#!/usr/bin/env bash
# Runs every scheduler mode, priority, pre-emption and several flows per UE, under the debug profile, where NS_ASSERT is active. The
# optimized build used by tools/gate.sh ignores assertions. Takes about 10 minutes
# the first time, and leaves waf configured for the optimized build.
set -uo pipefail
cd "$(git rev-parse --show-toplevel)"
# shellcheck disable=SC1091
source .env

restore () {
  ./waf configure --disable-python --enable-examples --enable-tests \
    --build-profile=optimized --out=build/optimized > /dev/null
}
trap restore EXIT

./waf configure --disable-python --disable-examples --disable-tests --build-profile=debug \
  --out=build/debug --enable-modules=MoReV2X,traci > /dev/null
./waf build -j"$MOREV2X_JOBS" > /dev/null || exit 1

binary="$MOREV2X_ROOT/build/debug/scratch/HIGHWAY"
sumo_cfg="$MOREV2X_ROOT/scenarios/sumo/highway_2km/highway.sumocfg"
workdir="$(mktemp -d)"
common="--Vehicles=15 --simTime=4 --mcs=13"
failures=0

run () {
  local name="$1"; shift
  (cd "$workdir" && DYLD_LIBRARY_PATH="$MOREV2X_ROOT/build/debug" "$binary" $common "$@" > out.log 2>&1)
  local status=$?
  printf '%-16s %s\n' "$name" "$([ $status -eq 0 ] && echo ok || echo "FAILED: $(grep -aE 'cond=|msg=' "$workdir/out.log" | tail -1 | cut -c1-150)")"
  [ $status -eq 0 ] || failures=$((failures + 1))
}

run periodic --Periodic
run aperiodic --Aperiodic
run mixed --Mixed --Percentage=50
run adaptive --Mixed --Percentage=50 --Adaptive
run dynamic --Periodic --Dynamic
run retx --Periodic --ReTx
run reevaluation --Periodic --ReEvaluation
run reeval_retx --Periodic --ReEvaluation --ReTx
run allslots --Periodic --ReEvaluation --AllSlots
run umh --Periodic --ReTx --ReEvaluation --UMH
run aperiodic_reeval --Aperiodic --ReTx --ReEvaluation
run variable_size --Aperiodic --VariableSize
run numerology1 --Periodic --Numerology=1 --SubChannel=10
run numerology2 --Periodic --Numerology=2 --SubChannel=10 --ChannelBW=20
run freq_reuse --Periodic --Dynamic --FreqReuse --ReuseDist=500
run avg_rri --Aperiodic --AvgRRI
run adaptive_rri --Periodic --AdaptiveRRI
run adaptive_retx --Periodic --AdaptiveRRI --ReTx --ReEvaluation
run adaptive_mixed --Mixed --Percentage=50 --AdaptiveRRI
run adaptive_aper --Aperiodic --AdaptiveRRI
run per_node --Periodic '--SchedulerMap=1-5:sps;6-10:dynamic+retx;11-15:reeval+rsrp=-120+adaptive'
run sumo --Periodic --Vehicles=80 --sumoCfg="$sumo_cfg" --sumoWarmup=60
run mu2_oversize --Periodic --Numerology=2 --ChannelBW=10 --ReEvaluation
run preemption --Periodic --PDB=20 --ReEvaluation '--SchedulerMap=1-7:prio=2+preempt;8-15:prio=6+preempt'
run prio_aware --Periodic --ReEvaluation --ReTx '--SchedulerMap=1-15:prio=4+prioaware+preempt'
run two_flows --Periodic '--SchedulerMap=1-15:prio=5+preempt' --ReEvaluation --ExtraFlow=20:20:150:2:sps
run two_flows_dyn --Periodic --ExtraFlow=50:50:150:3:dynamic
run three_tx --Periodic --ReEvaluation '--SchedulerMap=1-8:tx=3+prio=2+preempt;9-15:tx=2+prio=6+preempt'
run three_tx_mu2 --Periodic --Numerology=2 --ChannelBW=20 --SubChannel=12 --mcs=14 --ReEvaluation '--SchedulerMap=1-15:tx=3'

rm -rf "$workdir"
echo "$failures mode(s) failed"
exit "$failures"
