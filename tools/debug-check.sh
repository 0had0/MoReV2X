#!/usr/bin/env bash
# Runs every scheduler mode under the debug profile, where NS_ASSERT is active. The
# optimized build used by tools/gate.sh ignores assertions. Takes about 20 minutes
# the first time. It builds in cmake-cache-debug and build-debug.
set -uo pipefail
cd "$(git rev-parse --show-toplevel)"
# shellcheck disable=SC1091
source .env

cmake -S . -B cmake-cache-debug -G Ninja -DCMAKE_BUILD_TYPE=debug -DNS3_ASSERT=ON -DNS3_LOG=ON \
  -DNS3_ENABLED_MODULES="MoReV2X;traci" -DNS3_OUTPUT_DIRECTORY="$MOREV2X_ROOT/build-debug" \
  -DNS3_WARNINGS_AS_ERRORS=OFF > /dev/null || exit 1
ninja -C cmake-cache-debug -j"$MOREV2X_JOBS" scratch_HIGHWAY > /dev/null || exit 1

binary="$MOREV2X_ROOT/build-debug/scratch/ns3.42-HIGHWAY-debug"
sumo_cfg="$MOREV2X_ROOT/scenarios/sumo/highway_2km/highway.sumocfg"
workdir="$(mktemp -d)"
common="--Vehicles=15 --simTime=4 --mcs=13"
failures=0

run () {
  local name="$1"; shift
  (cd "$workdir" && DYLD_LIBRARY_PATH="$MOREV2X_ROOT/build-debug/lib" "$binary" $common "$@" > out.log 2>&1)
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

rm -rf "$workdir"
echo "$failures mode(s) failed"
exit "$failures"
