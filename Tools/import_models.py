"""Imports powered-descent-sim's vehicle models (models/out/<model>.glb) into the plugin.

Headless, from Tools/regen_assets.sh (needs $PDS_DIR = a powered-descent-sim checkout):
  UnrealEditor <Project>.uproject -run=pythonscript -script=<abs>/import_models.py -unattended -nosplash

For each model it writes, under Content/Vehicles/<model>/:
  * the glTF's meshes and materials as UE assets (Interchange; one StaticMesh per mesh node),
  * <model>.nodes.json: the node tree (name, parent, mesh, rest transform) in UE units,
  * <model>.json: the joint manifest, copied from the model.

Interchange imports every mesh with its node's whole transform baked into the vertices
(world space = the foot plane frame, z up), scaled to cm, with y negated (the importer's
right- to left-handed flip); the glTF root's y-up rotation is not baked, its offset is. So
nodes.json holds each node's rest transform in that same convention: translation
(x, -y, z) * 100 cm, quaternion (-x, y, -z, w), scale unchanged, with the root's offset above
the foot plane added to its children. The plugin rebuilds the hierarchy from it, places each
mesh with the inverse of its parent's rest world transform (the vertices are already in world
space) and drives joints with local(t) = rest_local * delta(t).

The pure-Python part (node_tree) runs without Unreal and is tested by Tests/test_models.py.
"""
import hashlib
import json
import os
import shutil
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VEHICLES = ROOT / "Content" / "Vehicles"
UE_FOLDER = "/UnrealRocketViz/Vehicles"


def read_gltf(path):
    """The JSON chunk of a .glb."""
    data = Path(path).read_bytes()
    magic, version, _ = struct.unpack("<III", data[:12])
    assert magic == 0x46546C67 and version == 2, f"{path} is not a glb 2.0"
    length, kind = struct.unpack("<II", data[12:20])
    assert kind == 0x4E4F534A, "first chunk is not JSON"
    return json.loads(data[20:20 + length])


def node_tree(gltf):
    """Nodes below the scene root, depth first, parents before children.

    Each: {name, parent (index into the list, -1 for the root's children), mesh, t, q, s}.
    The root itself is dropped: its rotation is the y-up conversion. Its translation lifts the body
    frame above the foot plane (glTF y up is body z up: body = (gx, -gz, gy)); that offset is added
    to its children, so the tree is in the foot-plane frame the imported vertices are in.
    """
    nodes = gltf["nodes"]
    roots = gltf["scenes"][gltf.get("scene", 0)]["nodes"]
    assert len(roots) == 1, "expected one scene root"
    out = []
    rx, ry, rz = nodes[roots[0]].get("translation", (0.0, 0.0, 0.0))
    lift = (rx, -rz, ry)   # the root's offset, in the body frame (m)

    def add(i, parent):
        n = nodes[i]
        tx, ty, tz = n.get("translation", (0.0, 0.0, 0.0))
        if parent < 0:
            tx, ty, tz = tx + lift[0], ty + lift[1], tz + lift[2]
        qx, qy, qz, qw = n.get("rotation", (0.0, 0.0, 0.0, 1.0))
        index = len(out)
        out.append({
            "name": n["name"],
            "parent": parent,
            "mesh": n["name"] if "mesh" in n else None,
            "t": [tx * 100.0, -ty * 100.0, tz * 100.0],
            "q": [-qx, qy, -qz, qw],
            "s": list(n.get("scale", (1.0, 1.0, 1.0))),
        })
        for c in n.get("children", []):
            add(c, index)

    for c in nodes[roots[0]].get("children", []):
        add(c, -1)
    return out


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_nodes(glb, model_dir):
    """nodes.json and the manifest copy, and the source hash for the stamp."""
    gltf = read_gltf(glb)
    model = Path(glb).stem
    model_dir.mkdir(parents=True, exist_ok=True)
    tree = {"model": model, "source_sha256": sha256(glb), "nodes": node_tree(gltf)}
    (model_dir / f"{model}.nodes.json").write_text(json.dumps(tree, indent=1) + "\n")
    manifest = Path(glb).with_suffix(".json")
    if manifest.exists():
        (model_dir / f"{model}.json").write_bytes(manifest.read_bytes())
    return tree


def without_animations(glb):
    """A copy of the .glb without its review clips (same file name, in a temp dir).

    Interchange turns every mesh under an animated node into a SkeletalMesh; the plugin drives
    the joints itself, so it wants StaticMeshes.
    """
    import tempfile

    data = Path(glb).read_bytes()
    length = struct.unpack("<I", data[12:16])[0]
    gltf = json.loads(data[20:20 + length])
    gltf.pop("animations", None)
    js = json.dumps(gltf, separators=(",", ":")).encode()
    js += b" " * (-len(js) % 4)
    rest = data[20 + length:]
    out = struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + len(rest))
    out += struct.pack("<II", len(js), 0x4E4F534A) + js + rest
    path = Path(tempfile.mkdtemp(prefix="urv_glb_")) / Path(glb).name
    path.write_bytes(out)
    return path


def import_meshes(glb):
    """Interchange import of the meshes and materials, saved under UE_FOLDER."""
    import unreal

    model = Path(glb).stem
    dest = f"{UE_FOLDER}/{model}"
    eal = unreal.EditorAssetLibrary
    if eal.does_directory_exist(dest):
        eal.delete_directory(dest)
    manager = unreal.InterchangeManager.get_interchange_manager_scripted()
    source = unreal.InterchangeManager.create_source_data(str(without_animations(glb)))
    params = unreal.ImportAssetParameters()
    params.is_automated = True
    manager.import_asset(dest, source, params)
    eal.save_directory(dest, only_if_is_dirty=False, recursive=True)
    unreal.log(f"[import_models] {model}: {len(eal.list_assets(dest, recursive=True))} assets")


def main():
    pds = os.environ.get("PDS_DIR")
    if not pds:
        sys.exit("[import_models] set PDS_DIR to a powered-descent-sim checkout")
    out = Path(pds) / "models" / "out"
    names = [n for n in os.environ.get("URV_MODELS", "").split(",") if n] or \
        sorted(p.stem for p in out.glob("*.glb"))
    for name in names:
        glb = out / f"{name}.glb"
        shutil.rmtree(VEHICLES / name, ignore_errors=True)   # no stale assets from an earlier import
        write_nodes(glb, VEHICLES / name)
        import_meshes(glb)
    try:
        import unreal
        unreal.log("[import_models] done")
    except ImportError:
        print("[import_models] done (nodes only)")


if __name__ in ("__main__", "__builtin__"):
    main()
