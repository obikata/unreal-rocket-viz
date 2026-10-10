"""Wire-format gates: Python reference round-trips, golden bytes are stable,
and the C++ decoder (UrvWire.h) reads the golden packets exactly as Python does.

    python -m pytest Tests
"""
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "Tools"))
sys.path.insert(0, str(ROOT / "Tests"))
import make_golden  # noqa: E402
from urv_wire import decode  # noqa: E402

GOLDEN = ROOT / "Tests" / "golden"


def test_golden_bytes_are_stable():
    for name, data in make_golden.packets():
        assert (GOLDEN / f"{name}.bin").read_bytes() == data, f"{name}: encoder output changed"


def test_python_roundtrip():
    for _, data in make_golden.packets():
        m = decode(data)
        assert m["type"] in (1, 2, 3)


def test_rejects_truncated_and_trailing():
    data = (GOLDEN / "frame_one.bin").read_bytes()
    for n in range(len(data)):
        with pytest.raises(ValueError):
            decode(data[:n])
    with pytest.raises(ValueError):
        decode(data + b"\0")


def _py_dump(path: Path) -> str:
    m = decode(path.read_bytes())
    g = lambda v: repr(float(v)) if False else "%.17g" % v  # noqa: E731
    lines = [f"{path}: type={m['type']} seq={m['seq']} sender={m['sender_id']}"]
    if m["type"] == 1:
        lines.append(f"  t={g(m['sim_time'])} send={g(m['send_time'])} n={len(m['entities'])}")
        for e in m["entities"]:
            lines.append("  id=%d pos=%s vel=%s q=%s w=%s gimbal=%s thr=%s mask=%d flags=%d" % (
                e.id, ",".join(g(v) for v in e.pos_ecef), ",".join(g(v) for v in e.vel_ecef),
                ",".join(g(v) for v in e.q_body2ecef), ",".join(g(v) for v in e.omega_body),
                ",".join(g(v) for v in e.gimbal), g(e.throttle), e.engine_mask, 1 if e.engine_on else 0))
            lines += [f"    {k}={g(v)}" for k, v in e.channels.items()]
    elif m["type"] == 2:
        lines.append(f"  t={g(m['sim_time'])} id={m['event_id']} entity={m['entity_id']} name={m['name']}")
    else:
        lines.append("  json=" + (path.read_bytes()[20:]).decode("utf-8"))
    return "\n".join(lines)


@pytest.mark.skipif(shutil.which("g++") is None, reason="no C++ compiler")
def test_cpp_decoder_matches_python(tmp_path):
    exe = tmp_path / "wire_test"
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    "-I", str(ROOT / "Source/UnrealRocketViz/Public"), str(ROOT / "Tests/wire_test.cpp"),
                    "-o", str(exe)], check=True)
    files = sorted(GOLDEN.glob("*.bin"))
    r = subprocess.run([str(exe), *map(str, files)], capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr
    assert r.stdout.strip() == "\n".join(_py_dump(f) for f in files)
