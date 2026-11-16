#!/usr/bin/env bash
# One WiLabV2Xsim run of the DIT paper experiment:
#   experiment/run_one.sh <scheme> <density veh/km> <seed> <simulated seconds>
# Schemes (paper Fig. 3 and 4):
#   sps_noret_noreeval  SPS w/o ret, no Re-Eval       sps_noret_reeval  SPS w/o ret, std Re-Eval
#   sps_ret_noreeval    SPS w/ ret, no Re-Eval        sps_ret_reeval    SPS w/ ret, std Re-Eval
#   dit_noreeval        DIT w/o Re-Eval, no Pre-Emp   dit_reeval_preempt DIT w/ Re-Eval and Pre-Emp
# Output: results/<scheme>_rho<density>_s<seed>/ ; skipped if already complete.
set -euo pipefail
cd "$(dirname "$0")/.."
scheme=$1; rho=$2; seed=$3; simtime=$4
case "$scheme" in
  sps_noret_noreeval) args="'cv2xNumberOfReplicasMax',1,'resourceReEvaluation',false" ;;
  sps_noret_reeval)   args="'cv2xNumberOfReplicasMax',1,'resourceReEvaluation',true" ;;
  sps_ret_noreeval)   args="'cv2xNumberOfReplicasMax',2,'resourceReEvaluation',false" ;;
  sps_ret_reeval)     args="'cv2xNumberOfReplicasMax',2,'resourceReEvaluation',true" ;;
  dit_noreeval)       args="'cv2xNumberOfReplicasMax',2,'resourceReEvaluation',false,'DIT',true,'DITpreemption',false" ;;
  dit_reeval_preempt) args="'cv2xNumberOfReplicasMax',2,'resourceReEvaluation',true,'DIT',true,'DITpreemption',true" ;;
  *) echo "unknown scheme $scheme"; exit 1 ;;
esac
out="results/${scheme}_rho${rho}_s${seed}"
if [ -f "$out/done" ]; then exit 0; fi
rm -rf "$out"; mkdir -p "$out"
start=$(date +%s)
if ./octave.sh --eval "warning('off','Octave:data-file-in-path'); warning('off','Octave:empty-index'); addpath(fullfile(pwd,'octave_compat')); cd WiLabV2Xsim; addpath(genpath(pwd)); WiLabV2Xsim('../experiment/dit_paper.cfg','seed',$seed,'simulationTime',$simtime,'rho',$rho,'Raw',[150 200],$args,'outputFolder','../$out')" > "$out/log.txt" 2>&1; then
  echo $(( $(date +%s) - start )) > "$out/done"
  echo "$(date +%T) ok   $(( $(date +%s) - start )) s  $out"
else
  echo "$(date +%T) FAIL $out (see $out/log.txt)"
fi
