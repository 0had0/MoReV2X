#!/usr/bin/env bash
# Builds the WiLabV2Xsim + Octave environment inside this folder, on Linux x86_64 or macOS arm64:
#   .tools/bin/micromamba    micromamba, its package cache in .tools/mamba
#   .octave/                 Octave 10.3 from conda-forge
#   WiLabV2Xsim/             upstream main at the tested commit, with patches/ applied
# Nothing is installed outside this folder. Run it once:  ./setup.sh
set -euo pipefail
cd "$(dirname "$0")"
COMMIT=612bf26ab91f294b635263065c26216921ad07ad   # V2Xgithub/WiLabV2Xsim main, 2025-11-18
case "$(uname -s)-$(uname -m)" in
  Linux-x86_64) platform=linux-64 ;;
  Darwin-arm64) platform=osx-arm64 ;;
  *) echo "unsupported platform $(uname -s)-$(uname -m)"; exit 1 ;;
esac
mkdir -p .tools
if [ ! -x .tools/bin/micromamba ]; then
  curl -sL "https://micro.mamba.pm/api/micromamba/$platform/latest" -o .tools/micromamba.tar.bz2
  tar -xjf .tools/micromamba.tar.bz2 -C .tools bin/micromamba
fi
if [ ! -x .octave/bin/octave-cli ]; then
  MAMBA_ROOT_PREFIX="$PWD/.tools/mamba" .tools/bin/micromamba create -y -q -p "$PWD/.octave" -c conda-forge octave=10.3.0
fi
if [ ! -d WiLabV2Xsim/.git ]; then
  git clone -q https://github.com/V2Xgithub/WiLabV2Xsim.git WiLabV2Xsim
  git -C WiLabV2Xsim checkout -q "$COMMIT"
  git -C WiLabV2Xsim apply ../patches/wilabv2xsim-octave-dit.patch
fi
./octave.sh --eval "printf('Octave %s ready\n', version())"
