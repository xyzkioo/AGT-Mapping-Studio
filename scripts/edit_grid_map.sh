#!/usr/bin/env bash
# Launch the standalone grid editor from the project's tools directory.
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
exec "$ROOT/tools/agt-grid-map-editor/start.sh" "$@"
