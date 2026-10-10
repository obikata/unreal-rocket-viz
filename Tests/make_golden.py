"""Writes the golden packets that pin the wire format (Tests/golden/*.bin).

Run only when the protocol deliberately changes (and bump `version` if the
binary layout changes): python Tests/make_golden.py
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "Tools"))
from urv_wire import Entity, encode_event, encode_frame, encode_scene  # noqa: E402

OUT = Path(__file__).resolve().parent / "golden"
SENDER = 0xC0FFEE01


def packets():
    yield "frame_one", encode_frame(7, SENDER, 12.5, 1760000000.25, [
        Entity(1, (918263.5, -5529341.25, 3026845.75), (12.5, -3.25, -80.0),
               (0.7071067811865476, 0.0, -0.7071067811865476, 0.0), (0.01, -0.02, 0.5), (0.03, -0.015), 0.75, 0b1, True,
               {"speed_kmh": 290.5, "altitude_m": 1234.0})])
    yield "frame_two", encode_frame(8, SENDER, 99.0, 0.0, [
        Entity(1, (1.0, 2.0, 3.0), (0.0, 0.0, 0.0), (1.0, 0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (0.0, 0.0), 1.0, 0x1FF, True, {}),
        Entity(2, (-1.5, 2.5, -3.5), (4.0, 5.0, 6.0), (0.5, 0.5, 0.5, 0.5), (1.0, 2.0, 3.0), (-0.5, 0.25), 0.0, 0, False,
               {"q": -0.125})])
    yield "event", encode_event(9, SENDER, 41.25, 3, "TOUCHDOWN", 1)
    yield "scene", encode_scene(10, SENDER, {
        "mission": "PDG landing", "origin": {"lon": -80.544, "lat": 28.486, "height": 0.0},
        "ground_ref": {"entity": 1, "above_m": 2.5},
        "entities": [{"id": 1, "label": "BOOSTER", "label_local": "ブースター",
                      "look": {"start_x": 1.6, "length": 46.5, "diameter": 3.66}}]})


if __name__ == "__main__":
    OUT.mkdir(exist_ok=True)
    for name, data in packets():
        (OUT / f"{name}.bin").write_bytes(data)
        print(f"{name}.bin  {len(data)} bytes")
