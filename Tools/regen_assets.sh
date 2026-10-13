#!/usr/bin/env bash
# Regenerate the plugin's Unreal assets (Content/Materials) from Tools/make_materials.py,
# headless, and stamp them with the script's hash (Tests/test_assets.py checks the stamp).
#
#   Tools/regen_assets.sh            # then commit Content/Materials together with the script
#
# Needs: UE 5.8 ($UE, default ~/UnrealEngine) and a host project that mounts this plugin
# with the Python Editor Script Plugin enabled ($URV_HOST_PROJECT, default below).
# The editor must be closed (it locks the assets).
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
UE="${UE:-$HOME/UnrealEngine}"
HOST="${URV_HOST_PROJECT:-$HOME/Documents/Unreal Projects/RocketVizHost/RocketVizHost.uproject}"
editor="$UE/Engine/Binaries/Linux/UnrealEditor"
[[ -x "$editor" ]] || { echo "UnrealEditor not found at $editor (set UE)" >&2; exit 1; }
[[ -f "$HOST" ]] || { echo "host project not found: $HOST (set URV_HOST_PROJECT)" >&2; exit 1; }

log="$(mktemp)"
URV_REBUILD_MATERIALS=1 "$editor" "$HOST" -run=pythonscript -script="$here/Tools/make_materials.py" \
    -unattended -nosplash 2>&1 | tee "$log" | grep -E "make_materials|Error" || true
grep -q "\[make_materials\] done" "$log" || { echo "make_materials did not finish; see $log" >&2; exit 1; }

python3 - "$here" <<'PY'
import hashlib, json, pathlib, sys
root = pathlib.Path(sys.argv[1])
digest = hashlib.sha256((root / "Tools/make_materials.py").read_bytes()).hexdigest()
(root / "Content/Materials/GENERATED.json").write_text(json.dumps(
    {"generator": "Tools/make_materials.py", "sha256": digest}, indent=2) + "\n")
print("stamped Content/Materials/GENERATED.json", digest[:12])
PY
git -C "$here" add Content/Materials
echo "staged Content/Materials; commit it together with any make_materials.py change"
