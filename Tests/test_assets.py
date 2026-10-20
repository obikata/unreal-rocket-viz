"""Generated-asset gate: Content/Materials was regenerated from the current
Tools/make_materials.py (Tools/regen_assets.sh stamps GENERATED.json), and every
material the script makes is committed.

The assets themselves need Unreal to build, so this checks the stamp, not the bytes.
"""
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MATS = ROOT / "Content" / "Materials"
SCRIPT = ROOT / "Tools" / "make_materials.py"


def test_materials_match_the_generator():
    stamp = json.loads((MATS / "GENERATED.json").read_text())
    assert stamp["sha256"] == hashlib.sha256(SCRIPT.read_bytes()).hexdigest(), (
        "Tools/make_materials.py changed since Content/Materials was generated: "
        "run Tools/regen_assets.sh (needs Unreal) and commit the assets with the script")


def test_every_generated_material_is_committed():
    names = set(re.findall(r'get_or_create\("(M_\w+)"\)', SCRIPT.read_text()))
    assert names, "no materials found in make_materials.py"
    for name in sorted(names):
        f = MATS / f"{name}.uasset"
        assert f.exists(), f"{name}.uasset missing: run Tools/regen_assets.sh"
        head = f.read_bytes()[:64]
        assert not head.startswith(b"version https://git-lfs"), (
            f"{name}.uasset is an LFS pointer: run `git lfs install && git lfs pull`")
