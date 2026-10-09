# URV wire protocol v1 (UDP)

How a simulator (or a log player, or live telemetry) feeds `UnrealRocketViz`
over the network. The plugin subscribes; senders know nothing about Unreal.

- Transport: UDP datagrams, one message per datagram. Default **multicast
  group `239.255.76.86`, port `47686`** (any unicast address works too).
- Byte order: **little-endian**, fields packed with no padding.
- Floats: IEEE-754 `f64`. Integers: `u8/u16/u32` unsigned, `i32` signed.
- Strings: length-prefixed (`u8` length, then that many UTF-8 bytes, no NUL).
- Frames: world quantities in **WGS84 ECEF** [m, m/s]; attitude is the
  quaternion that rotates **body** vectors into ECEF, with **body +X = nose**
  (the plugin's convention). Senders convert from their own frames.

Reference encoder/decoder: `Tools/urv_wire.py` (Python stdlib only).
Reference C++ decoder (no Unreal dependency): `Source/UnrealRocketViz/Public/UrvWire.h`.
Both are pinned by the golden packets in `Tests/golden/`.

## Header (20 bytes, every message)

| off | type  | field          | notes |
|----:|-------|----------------|-------|
| 0   | u32   | magic          | `0x31565255` — the bytes `"URV1"` |
| 4   | u16   | version        | `1` |
| 6   | u16   | type           | `1` FRAME, `2` EVENT, `3` SCENE |
| 8   | u32   | seq            | per sender, +1 every datagram (wraps) |
| 12  | u32   | sender_id      | random per sender run: a new value = the sender restarted |
| 16  | u32   | payload_bytes  | bytes after the header |

Receivers drop datagrams with a wrong magic or version, a short payload, or
an unknown type. A FRAME older than the newest one received (by `sim_time`,
same `sender_id`) may be dropped; a new `sender_id` resets the receiver.

## FRAME (type 1) — everything known at one simulation time, ~20–100 Hz

| type | field | notes |
|------|-------|-------|
| f64  | sim_time | [s] |
| f64  | send_time | sender wall clock [s since 1970], 0 if unknown |
| u16  | n_entities | |
| u16  | reserved | 0 |
| ENTITY × n_entities | | |

ENTITY:

| type | field | notes |
|------|-------|-------|
| i32  | id | matches an entity in the SCENE |
| f64×3 | pos_ecef | [m] the entity's reference point (see SCENE `look.start_x`) |
| f64×3 | vel_ecef | [m/s], relative to the rotating Earth |
| f64×4 | q_body2ecef | `w, x, y, z`, unit; body +X = nose |
| u32  | engine_mask | bit *i*: engine *i* burning |
| u32  | flags | bit 0: any engine on |
| u16  | n_channels | |
| CHANNEL × n_channels | | |

CHANNEL: `u8 name_len, name (ASCII), f64 value` — a display value shown as
sent (e.g. `speed_kmh`, `altitude_km`). The viewer computes nothing from it.

## EVENT (type 2) — a discrete event

| type | field | notes |
|------|-------|-------|
| f64  | sim_time | [s] |
| u32  | event_id | unique per sender run |
| str  | name | e.g. `LANDING_BURN`, matched against SCENE milestones |

UDP may drop a datagram: senders repeat every EVENT **3 times**; receivers
keep the first and drop repeats with the same `(sender_id, event_id)`.

## SCENE (type 3) — what to draw, ~1 Hz

Payload is one UTF-8 JSON object. Sent at start and then about once a second,
so a viewer started late still builds the scene. Receivers rebuild only when
the content changes.

```json
{
  "mission": "PDG landing",
  "origin": {"lon": -80.544, "lat": 28.486, "height": 0.0},
  "ground_ref": {"lon": -80.544, "lat": 28.486, "height": 0.0, "above_m": 0.0},
  "solar_time": 10.5,
  "entities": [
    {"id": 1, "label": "BOOSTER", "label_local": "ブースター",
     "look": {"start_x": 1.6, "length": 46.5, "diameter": 3.66,
              "nose_length": 0.0, "fins": 0, "bell_diameter": 0.92,
              "plume_length": 30.0},
     "model": "falcon9-class"}
  ],
  "readouts": [
    {"channel": "speed_kmh", "label": "SPEED", "unit": "KM/H", "decimals": 0, "max": 2000},
    {"channel": "altitude_m", "label": "ALTITUDE", "unit": "M", "decimals": 0, "max": 5000}
  ],
  "milestones": [
    {"time": 0.0, "code": "LANDING_BURN", "name": "着陸燃焼", "name_en": "LANDING BURN"},
    {"time": 41.2, "code": "TOUCHDOWN", "name": "着陸", "name_en": "TOUCHDOWN"}
  ]
}
```

- `origin`: georeference origin (degrees, degrees, metres above WGS84).
- `ground_ref` (optional): stand this point `above_m` above the streamed
  terrain (`AUrvDirector::SetGroundReference`), e.g. the landing pad.
- `look`: `FUrvStageLook` in metres. The entity's reference point is the
  actor origin; the body spans `start_x .. start_x + length` along +X.
- `model` (optional): a vehicle class name the host project may map to its own meshes.
- `readouts`, `milestones`: the HUD's `FUrvReadout` / `FUrvMilestone` lists.

Unknown JSON keys are ignored, so senders may add fields without a version bump.
Any change to the binary layout bumps `version`.
