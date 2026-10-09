"""Reference encoder/decoder for the URV wire protocol v1 (Docs/wire-protocol.md).

Python standard library only, so any sender can copy or import it.

    from urv_wire import Entity, encode_frame, encode_event, encode_scene, decode
    sock.sendto(encode_frame(seq, sender_id, t, time.time(), [Entity(...)]), (GROUP, PORT))
"""
from __future__ import annotations

import json
import struct
from dataclasses import dataclass, field

MAGIC = 0x31565255          # b"URV1" little-endian
VERSION = 1
FRAME, EVENT, SCENE = 1, 2, 3
DEFAULT_GROUP = "239.255.76.86"
DEFAULT_PORT = 47686

_HDR = struct.Struct("<IHHIII")                    # magic, version, type, seq, sender_id, payload_bytes
_FRAME_HEAD = struct.Struct("<ddHH")               # sim_time, send_time, n_entities, reserved
_ENTITY = struct.Struct("<i3d3d4dIIH")             # id, pos, vel, quat, engine_mask, flags, n_channels
_EVENT_HEAD = struct.Struct("<dI")                 # sim_time, event_id
HEADER_BYTES = _HDR.size                           # 20


@dataclass
class Entity:
    id: int
    pos_ecef: tuple[float, float, float]
    vel_ecef: tuple[float, float, float] = (0.0, 0.0, 0.0)
    q_body2ecef: tuple[float, float, float, float] = (1.0, 0.0, 0.0, 0.0)   # w, x, y, z; body +X = nose
    engine_mask: int = 0
    engine_on: bool = False
    channels: dict[str, float] = field(default_factory=dict)


def _str(s: str) -> bytes:
    b = s.encode("utf-8")
    if len(b) > 255:
        raise ValueError(f"string longer than 255 bytes: {s[:20]}...")
    return bytes([len(b)]) + b


def _wrap(msg_type: int, seq: int, sender_id: int, payload: bytes) -> bytes:
    return _HDR.pack(MAGIC, VERSION, msg_type, seq & 0xFFFFFFFF, sender_id & 0xFFFFFFFF, len(payload)) + payload


def encode_frame(seq: int, sender_id: int, sim_time: float, send_time: float, entities: list[Entity]) -> bytes:
    out = [_FRAME_HEAD.pack(sim_time, send_time, len(entities), 0)]
    for e in entities:
        out.append(_ENTITY.pack(e.id, *e.pos_ecef, *e.vel_ecef, *e.q_body2ecef,
                                e.engine_mask & 0xFFFFFFFF, 1 if e.engine_on else 0, len(e.channels)))
        for name, value in e.channels.items():
            if not name.isascii():
                raise ValueError(f"channel names are ASCII: {name!r}")
            out.append(_str(name) + struct.pack("<d", value))
    return _wrap(FRAME, seq, sender_id, b"".join(out))


def encode_event(seq: int, sender_id: int, sim_time: float, event_id: int, name: str) -> bytes:
    return _wrap(EVENT, seq, sender_id, _EVENT_HEAD.pack(sim_time, event_id & 0xFFFFFFFF) + _str(name))


def encode_scene(seq: int, sender_id: int, scene: dict) -> bytes:
    body = json.dumps(scene, ensure_ascii=False, separators=(",", ":"), sort_keys=True).encode("utf-8")
    if len(body) > 60000:
        raise ValueError("SCENE JSON must fit in one datagram (<= 60000 bytes)")
    return _wrap(SCENE, seq, sender_id, body)


def decode(buf: bytes) -> dict:
    """Inverse of the encoders; raises ValueError on anything malformed."""
    if len(buf) < HEADER_BYTES:
        raise ValueError("short header")
    magic, version, msg_type, seq, sender_id, n = _HDR.unpack_from(buf, 0)
    if magic != MAGIC or version != VERSION:
        raise ValueError("bad magic/version")
    if len(buf) != HEADER_BYTES + n:
        raise ValueError("payload length mismatch")
    p, off = buf[HEADER_BYTES:], 0
    out = {"type": msg_type, "seq": seq, "sender_id": sender_id}

    def take(st: struct.Struct):
        nonlocal off
        if off + st.size > len(p):
            raise ValueError("truncated")
        v = st.unpack_from(p, off)
        off += st.size
        return v

    def take_str() -> str:
        nonlocal off
        if off >= len(p) or off + 1 + p[off] > len(p):
            raise ValueError("truncated string")
        k = p[off]
        s = p[off + 1: off + 1 + k].decode("utf-8")
        off += 1 + k
        return s

    if msg_type == FRAME:
        t, ts, ne, _ = take(_FRAME_HEAD)
        ents = []
        for _ in range(ne):
            v = take(_ENTITY)
            chans = {}
            for _ in range(v[13]):
                name = take_str()
                chans[name] = take(struct.Struct("<d"))[0]
            ents.append(Entity(v[0], tuple(v[1:4]), tuple(v[4:7]), tuple(v[7:11]), v[11], bool(v[12] & 1), chans))
        out.update(sim_time=t, send_time=ts, entities=ents)
    elif msg_type == EVENT:
        t, eid = take(_EVENT_HEAD)
        out.update(sim_time=t, event_id=eid, name=take_str())
    elif msg_type == SCENE:
        out.update(scene=json.loads(p.decode("utf-8")))
        off = len(p)
    else:
        raise ValueError(f"unknown type {msg_type}")
    if off != len(p):
        raise ValueError("trailing bytes")
    return out
