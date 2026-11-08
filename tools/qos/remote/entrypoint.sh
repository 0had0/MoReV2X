#!/usr/bin/env bash
# Entry point of the Docker image: Python 3 and SUMO from the venv, then the given command.
set -euo pipefail
SUMO_HOME="$(/opt/morev2x/.venv/bin/python -c 'import os, sumo; print(os.path.dirname(sumo.__file__))')"
export SUMO_HOME
export PATH="$SUMO_HOME/bin:/opt/morev2x/.venv/bin:$PATH"
cd /opt/morev2x
exec "$@"
