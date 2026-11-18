# MoReV2X on ns-3.42 for the QOS sweep: Ubuntu 22.04, gcc 11, CMake and Ninja, Python 3 and
# SUMO for the traces and the metrics. Built and run by tools/qos/remote/launch.sh:
#
#   docker build -t morev2x-qos .
#   docker run --rm -v "$PWD/results:/opt/morev2x/results" morev2x-qos tools/qos/sweep.py scenarios/qos/sweep_full.yaml
#
# Only the QOS and HIGHWAY programs and the MoReV2X and traci modules are built. The X11 and GL
# libraries are there because the SUMO binaries of the eclipse-sumo wheel link them, even sumo.
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
 && apt-get install -y --no-install-recommends build-essential g++ cmake ninja-build python3 python3-venv python3-pip \
      libsqlite3-dev libgsl-dev libxml2-dev pkg-config ca-certificates procps \
      libx11-6 libxext6 libxrender1 libgl1 \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /opt/morev2x

# Python first, so a source change does not reinstall it
COPY tools/requirements.txt tools/requirements.txt
RUN python3 -m venv .venv \
 && .venv/bin/pip install --no-cache-dir --upgrade pip \
 && .venv/bin/pip install --no-cache-dir -r tools/requirements.txt

COPY . .

# Parallel compile jobs, default all cores. Lower it on a machine with little memory:
# docker build --build-arg JOBS=4 -t morev2x-qos .
ARG JOBS=0

# No fused multiply-add, as in .env on the Mac, so results match across hosts
ENV CXXFLAGS="-ffp-contract=off -w" \
    MOREV2X_ROOT=/opt/morev2x \
    LD_LIBRARY_PATH=/opt/morev2x/build/lib
RUN ./ns3 configure --build-profile=optimized --disable-examples --disable-tests \
      --enable-modules='MoReV2X;traci' --disable-werror > /tmp/configure.log \
 && (./ns3 build -j"$([ "$JOBS" -gt 0 ] && echo "$JOBS" || nproc)" > /tmp/build.log 2>&1 || (tail -60 /tmp/build.log; exit 1))

# SUMO and Python 3 from the venv, see the entrypoint
ENTRYPOINT ["/opt/morev2x/tools/qos/remote/entrypoint.sh"]
CMD ["tools/qos/sweep.py", "scenarios/qos/sweep_smoke.yaml", "--jobs", "4"]
