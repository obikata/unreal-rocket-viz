# Watching a live simulator

`AUrvLiveGameMode` turns an empty level into a viewer for any sender of the
URV wire protocol (`Docs/wire-protocol.md`): the globe, the vehicles, the
camera targets, the HUD readouts and milestones and the ground reference all
come from the sender's SCENE messages.

## One-time setup

1. Create an Unreal Engine 5.8 **C++** project (Games → Blank, C++), or use an
   existing C++ project.
2. Install **Cesium for Unreal 2.30** (Fab) and enable it.
3. Clone this repo into the project's `Plugins/` folder and enable
   `UnrealRocketViz`; let the editor build the module.
4. Sign in to Cesium ion from the Cesium panel (the default access token is
   then used for terrain and imagery).
5. Make an empty level (File → New Level → Empty Level) and in
   **World Settings → GameMode Override** pick `UrvLiveGameMode`.

## Each run

1. Press **Play** (or launch the game). The HUD shows `UDP 47686  WAITING`.
2. Start the sender, e.g. from `obikata/powered-descent-sim`:

   ```bash
   python -m sim.urv_stream --loop
   ```

   The globe appears at the sender's site, the vehicle is placed on the
   streamed terrain, and the HUD switches to `UDP 47686  50 PKT/S`.

Keys: `1`–`9` pick the target, `V` cycles chase / drone / onboard, `A`
resumes the automatic cuts. Drag to orbit, wheel to zoom.

## Networking

- Default: multicast group `239.255.76.86`, port `47686`, on the local network.
- Sender on another machine without multicast routing, or inside WSL2: send
  unicast to the viewer (`--dest <viewer IP>`) and start the viewer with
  `-UrvGroup=none` (or set `MulticastGroup` to `none` on the GameMode).
- Windows firewall: allow inbound UDP on port 47686 for the editor / game.
- Another port: `-UrvPort=<n>` on the viewer, `--port <n>` on the sender.

## Your own meshes

Subclass `AUrvLiveGameMode` and override `OnVehicleSpawned(Vehicle, Id, Model)`:
`Model` is the sender's vehicle class (e.g. `falcon9-class`); call
`Vehicle->Build()` again with an `FUrvStageLook` whose `Meshes` point at your
imported models.
