# MoReV2X on ns-3.22 for the QOS sweep: Ubuntu 22.04, gcc 11, Python 2.7 for waf, Python 3 and
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
 && apt-get install -y --no-install-recommends build-essential g++ python2 python3 python3-venv python3-pip \
      libsqlite3-dev libgsl-dev libxml2-dev pkg-config ca-certificates procps \
      libx11-6 libxext6 libxrender1 libgl1 \
 && rm -rf /var/lib/apt/lists/* \
 && ln -sf /usr/bin/python2 /usr/local/bin/python

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

# Same flags as .env on the Mac, -O2 and no fused multiply-add so results match across hosts.
# waf 1.7 runs on Python 2 only, hence python2 ./waf
ENV CXXFLAGS="-std=gnu++11 -O2 -g0 -ffp-contract=off -w" \
    MOREV2X_ROOT=/opt/morev2x \
    LD_LIBRARY_PATH=/opt/morev2x/build/optimized
RUN python2 ./waf configure --disable-python --disable-examples --disable-tests --build-profile=optimized \
      --out=build/optimized --enable-modules=MoReV2X,traci > /tmp/configure.log \
 && (python2 ./waf build -j"$([ "$JOBS" -gt 0 ] && echo "$JOBS" || nproc)" > /tmp/build.log 2>&1 || (tail -60 /tmp/build.log; exit 1))

# SUMO and Python 3 from the venv, see the entrypoint
ENTRYPOINT ["/opt/morev2x/tools/qos/remote/entrypoint.sh"]
CMD ["tools/qos/sweep.py", "scenarios/qos/sweep_smoke.yaml", "--jobs", "4"]
