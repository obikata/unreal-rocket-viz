#!/usr/bin/env bash
# Import powered-descent-sim's vehicle models into Content/Vehicles/ (headless Unreal).
# Not committed (about 120 MB): every machine generates its own. Without it, a SCENE `model`
# falls back to the procedural body.
#
#   PDS_DIR=~/path/to/powered-descent-sim Tools/import_models.sh
#   URV_MODELS=neutron-class,droneship-class PDS_DIR=... Tools/import_models.sh   # only these
#
# Needs: UE 5.8 ($UE, default ~/UnrealEngine), the host project ($URV_HOST_PROJECT) with the
# Python Editor Script Plugin, and the editor closed.
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
UE="${UE:-$HOME/UnrealEngine}"
HOST="${URV_HOST_PROJECT:-$HOME/Documents/Unreal Projects/RocketVizHost/RocketVizHost.uproject}"
editor="$UE/Engine/Binaries/Linux/UnrealEditor"
[[ -n "${PDS_DIR:-}" && -d "$PDS_DIR/models/out" ]] || { echo "set PDS_DIR to a powered-descent-sim checkout (models/out/*.glb)" >&2; exit 1; }
[[ -x "$editor" ]] || { echo "UnrealEditor not found at $editor (set UE)" >&2; exit 1; }
[[ -f "$HOST" ]] || { echo "host project not found: $HOST (set URV_HOST_PROJECT)" >&2; exit 1; }

log="$(mktemp)"
PDS_DIR="$PDS_DIR" "$editor" "$HOST" -run=pythonscript -script="$here/Tools/import_models.py" \
    -unattended -nosplash -stdout -FullStdOutLogOutput 2>&1 | tee "$log" | grep -E "import_models|Error" || true
grep -q "\[import_models\] done" "$log" || { echo "import_models did not finish; see $log" >&2; exit 1; }
echo "imported into $here/Content/Vehicles (not tracked by git)"
