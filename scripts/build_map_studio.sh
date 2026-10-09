#!/usr/bin/env bash
# Build this checkout's Studio into a local overlay used by map_studio.sh.
set -eo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
WORKSPACE="$(cd -- "$ROOT/../.." && pwd)"
ROS_SETUP="${ROS_SETUP:-/opt/ros/humble/setup.bash}"
[[ -f "$ROS_SETUP" ]] || { echo "ROS setup not found: $ROS_SETUP (set ROS_SETUP)" >&2; exit 2; }
source "$ROS_SETUP"
for overlay in "$WORKSPACE/install/setup.bash" "$WORKSPACE/install_mapping_framework/setup.bash"; do
  if [[ -f "$overlay" ]]; then
    source "$overlay"
  fi
done
export CMAKE_BUILD_PARALLEL_LEVEL="${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
colcon --log-base "$ROOT/.studio-log" build \
  --base-paths "$ROOT/apps/agt_map_studio" \
  --build-base "$ROOT/.studio-build" \
  --install-base "$ROOT/.studio-install" \
  --packages-select agt_map_studio \
  --allow-overriding agt_map_studio \
  --event-handlers console_cohesion+ \
  --cmake-args -DBUILD_TESTING=ON -DSTUDIO_ALGORITHM_SMOKE=ON "$@"
echo "Map Studio built. Start it with: $ROOT/scripts/map_studio.sh"
