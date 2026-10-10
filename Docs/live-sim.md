# Watching a live simulator

`AUrvLiveGameMode` turns an empty level into a viewer for any sender of the
URV wire protocol (`Docs/wire-protocol.md`): the globe, the vehicles, the
camera targets, the HUD readouts and milestones and the ground reference all
come from the sender's SCENE messages.

## One-time setup

1. Create an Unreal Engine 5.8 **C++** project (Games → Blank, C++), or use an
   existing C++ project.
2. Install **Cesium for Unreal** (2.29 or later, the build for UE 5.8) and enable
   it: from Fab on Windows/macOS, or on Linux the `CesiumForUnreal-58-v*.zip` from
   the [GitHub releases](https://github.com/CesiumGS/cesium-unreal/releases)
   unzipped into the engine's `Engine/Plugins/Marketplace/` (in a project's
   `Plugins/` the build recompiles it from source with the project's
   warnings-as-errors, which it does not pass).
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
   streamed terrain, and the HUD switches to `UDP 47686  50 HZ`.

Keys: `1`–`9` pick the target, `V` cycles chase / drone / onboard, `A`
resumes the automatic cuts. Drag to orbit, wheel to zoom.

## Networking

- Default: multicast group `239.255.76.86`, port `47686`, on the local network.
- Sender on another machine without multicast routing, or inside WSL2: send
  unicast to the viewer (`--dest <viewer IP>`) and start the viewer with
  `-UrvGroup=none` (or set `MulticastGroup` to `none` on the GameMode).
- Windows firewall: allow inbound UDP on port 47686 for the editor / game.
- Another port: `-UrvPort=<n>` on the viewer, `--port <n>` on the sender.

## Linux (Ubuntu)

There is no Epic Launcher: use the Linux build of UE 5.8 (or one built from
source), and Cesium from its GitHub release zip (step 2). Build the editor
target from a terminal, which also gives readable errors:

```bash
cd ~/RocketVizHost
~/UnrealEngine/Engine/Build/BatchFiles/Linux/Build.sh RocketVizHostEditor Linux Development \
    -Project="$PWD/RocketVizHost.uproject" -WaitMutex
~/UnrealEngine/Engine/Binaries/Linux/UnrealEditor "$PWD/RocketVizHost.uproject" -UrvGroup=none
```

With the sender on the same machine, unicast to loopback is the simplest:
`python -m sim.urv_stream --dest 127.0.0.1 --loop` with the viewer started with
`-UrvGroup=none`. If `ufw` is active: `sudo ufw allow 47686/udp`.

## Your own meshes

Subclass `AUrvLiveGameMode` and override `OnVehicleSpawned(Vehicle, Id, Model)`:
`Model` is the sender's vehicle class (e.g. `falcon9-class`); call
`Vehicle->Build()` again with an `FUrvStageLook` whose `Meshes` point at your
imported models.
