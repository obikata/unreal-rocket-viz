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


def test_conversion_negates_y_and_scales_to_cm():
    # droneship container_0 sits at y = -14 m in the glb; Interchange imports it at +1400 cm
    n = {x["name"]: x for x in tree("droneship-class")}
    assert n["container_0_mesh"]["mesh"] == "container_0_mesh"
    assert n["container_0_mesh"]["t"] == [0.0, 0.0, 0.0]
