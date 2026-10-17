"""Node tree of the sim's vehicle models (Tools/import_models.py, no Unreal needed).

Needs powered-descent-sim: set PDS_DIR, or have it next to this repo's host project.
"""
import json
import os
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "Tools"))
import import_models  # noqa: E402

PDS = Path(os.environ.get("PDS_DIR", Path.home() / "sandbox/rocket-pdg/powered-descent-sim"))
OUT = PDS / "models" / "out"
pytestmark = pytest.mark.skipif(not OUT.is_dir(), reason="powered-descent-sim not found (PDS_DIR)")


def tree(name):
    return import_models.node_tree(import_models.read_gltf(OUT / f"{name}.glb"))


def test_parents_come_first_and_names_are_unique():
    for name in ("neutron-class", "droneship-class"):
        nodes = tree(name)
        assert len({n["name"] for n in nodes}) == len(nodes)
        assert all(n["parent"] < i for i, n in enumerate(nodes))


def test_every_manifest_joint_is_a_node():
    m = json.loads((OUT / "neutron-class.json").read_text())
    names = {n["name"] for n in tree("neutron-class")}
    joints = [e["node"] for e in m["engines"]] + [l["node"] for l in m["legs"]]
    joints += [p["node"] for p in m["grid_fins"]] if m["grid_fins"] else []
    assert joints and set(joints) <= names


def test_root_offset_is_added_to_its_children():
    # the falcon9-class root lifts the body frame 1.6 m above the foot plane; the first-level
    # meshes are baked at +160 cm in UE (aft_flare_mesh: glb z -2..55 cm -> UE 158..215 cm)
    n = {x["name"]: x for x in tree("falcon9-class")}
    assert n["aft_flare_mesh"]["parent"] == -1
    assert n["aft_flare_mesh"]["t"] == pytest.approx([0.0, 0.0, 160.0])


def test_children_below_the_first_level_keep_their_local_transform():
    n = {x["name"]: x for x in tree("falcon9-class")}
    mount = n["leg_00_mount"]
    assert mount["t"] == pytest.approx([199.0, 0.0, 60.0 + 160.0])
    leg = n["leg_00"]
    assert leg["t"] == pytest.approx([0.0, 0.0, 0.0])
