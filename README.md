# unreal-rocket-viz

Unreal Engine plugin (`UnrealRocketViz`) that draws rocket vehicles on a [Cesium for Unreal](https://cesium.com/platform/cesium-for-unreal/) globe from time-stamped states.

## Requirements

- Unreal Engine 5.8
- Cesium for Unreal 2.30 (enabled as a plugin; tiles stream from Cesium ion)

## Usage

Clone into a project's `Plugins/` folder and enable `UnrealRocketViz` in the `.uproject`.

States enter through `AUrvDirector` from any thread:

```cpp
FUrvFrame Frame;
Frame.SimTime = T;                         // seconds
FUrvEntityState& E = Frame.Entities.AddDefaulted_GetRef();
E.Id = 1;
E.PosEcef = ...;                           // WGS84 ECEF [m]
E.VelEcef = ...;                           // [m/s]
E.QBody2Ecef = ...;                        // rotates body vectors into ECEF; body +X is the nose
E.bEngineOn = true;
E.Channels.Add(TEXT("speed_kmh"), ...);    // display values, shown as sent
E.Channels.Add(TEXT("altitude_km"), ...);
Director->PushFrame(Frame);
Director->PushEvent(T, TEXT("LIFTOFF"));
```

The director keeps a short buffer, plays it about 100 ms behind the newest frame and interpolates (position linear, attitude slerp). Each entity id is bound to an `AUrvVehicle` with `AUrvDirector::Bind`.

Positions are drawn exactly as sent. To stand a site such as a launch pad or a landing zone on the streamed terrain, call `AUrvDirector::SetGroundReference(LonLatHeight, AboveGroundM)`; every vehicle is then shifted so that point ends up `AboveGroundM` above the terrain under it. The terrain height comes from Cesium's most detailed tiles, whatever is loaded or drawn at the time. `GetGroundOffset()` returns the shift, for scenery that has to move with the vehicles, and `SampleGround` gives the same height anywhere else.

A stage is drawn procedurally from `FUrvStageLook`, or from your own meshes: list them in `FUrvStageLook::Meshes` with one shared offset, rotation and scale, so parts modelled in one frame stay assembled. `StartX` and `BellDiameter` still place the plume, so set them to match the meshes' nozzle exit.

The overlay shows the mission clock with the readouts in `AUrvHud::Readouts` either side of it (default: `speed_kmh` and `altitude_km`), and the display name of the latest event from `AUrvHud::Milestones`. Engine on-screen debug messages are not touched; run `DisableAllScreenMessages` if they get in the way. `AUrvHud::Credit`, when set, is printed small at the lower right (for imagery attribution).

| Class | Role |
|---|---|
| `AUrvDirector` | Buffers frames and events, places the bound vehicles |
| `AUrvVehicle` | Procedural vehicle look and exhaust plume |
| `AUrvChasePawn` | Camera with three views: chase (left-drag orbits, wheel zooms), drone (`FUrvDroneCamera`: hovers, climbs, gimbal and zoom track the target) and onboard (`FUrvMountedCamera` per target). Keys 1–9 pick the target, V cycles the views, A resumes the plan in `Cuts` |
| `AUrvSoundscape` | Synthesised engine noise at the camera: delayed by the speed of sound, quieter and duller with distance; drives the camera shake |
| `AUrvHud` | Overlay: speed, mission clock, altitude, latest event |
| `UrvScene::Setup` | Adds a Cesium georeference, terrain, imagery, sun and sky. Imagery is a Cesium ion asset, or any Web Mercator tile server (for the common north-first numbering, `{z}/{x}/{reverseY}`) when `ImageryUrlTemplate` is set |

`Tools/make_materials.py` regenerates `Content/Materials` (run with the Python editor plugin enabled).

## License

Apache License 2.0 (see `LICENSE`). The fonts in `Resources/Fonts` are under the SIL Open Font License; their license files sit next to them.
