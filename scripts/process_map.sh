#!/usr/bin/env bash
# Toolbox entry point for the independent map-processing package.
set -eo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
WORKSPACE="$(cd -- "$ROOT/../.." && pwd)"
source "${ROS_SETUP:-/opt/ros/humble/setup.bash}"
for overlay in "$WORKSPACE/install/setup.bash" "$ROOT/.studio-install/setup.bash"; do
  if [[ -f "$overlay" ]]; then source "$overlay"; fi
done
exec ros2 run agt_map_runner map_runner "$@"
