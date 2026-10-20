# CLAUDE.md — agent guide for `unreal-rocket-viz`

A generic Unreal Engine 5 plugin that draws launch vehicles on a Cesium globe
from time-stamped ECEF states. Fed live over UDP (URV wire protocol v1,
`Docs/wire-protocol.md`) or by a host project's own code. Senders know nothing
about Unreal. The main sender today is `obikata/powered-descent-sim`
(`python -m sim.urv_stream`).

## Boundaries
- Keep it generic: no PDG-, vehicle- or company-specific logic in the plugin.
  Senders describe what to draw through the SCENE JSON.
- Public information only (Apache-2.0). No real-company logos or trademarks
  (vehicles are "-class").
- Wire format: a new message type or optional SCENE key is backward compatible.
  Any change to an existing binary layout bumps `version`. Goldens pin it.

## Definition of done
```bash
python -m pytest Tests      # golden packets; Python <-> C++ decoder cross-check (g++ ASan/UBSan)
```
Then **build in Unreal and look**. Visual quality is a human review gate
(Yohei), never claimed from code alone.

## Dev loop (Yohei's machine: Ubuntu 22.04, UE 5.8.0 binary at ~/UnrealEngine)
```bash
P="$HOME/Documents/Unreal Projects/RocketVizHost"     # host project; this repo is in $P/Plugins/
$UE/Engine/Build/BatchFiles/Linux/Build.sh RocketVizHostEditor Linux Development \
    -Project="$P/RocketVizHost.uproject" -WaitMutex 2>&1 | tee "$P/build.log" | tail -3
$UE/Engine/Binaries/Linux/UnrealEditor "$P/RocketVizHost.uproject" -UrvGroup=none
# sender (other terminal): python -m sim.urv_stream --closed-loop --dest 127.0.0.1 --loop --wind 0 8 0
```
- Build with the editor **closed**; the editor does not pick up new code otherwise.
- The level is an empty level whose GameMode Override is `UrvLiveGameMode`; it must be saved.
- `-UrvGroup=none` = unicast (same machine). Default is multicast 239.255.76.86:47686.
- `-UrvArm=<m>` sets the chase distance (default 160 m), for close-ups.
- Keys: 1–9 target, V view, A auto cuts, P paths (current / ghosts / hidden),
  T trails, H HUD. P and T print the new mode on screen.

## Gotchas learned the hard way
- **Cesium for Unreal** (2.29.x, the UE 5.8 zip from GitHub releases) lives in
  `$UE/Engine/Plugins/Marketplace/`. In a project's `Plugins/` it gets recompiled
  with the project's warnings-as-errors and fails.
- **Exposure:** Cesium's physical sun exposes for daylight. Emissive needs about
  ×2000 ("DaylightNits") to show at all, or translucent things just look black.
- **Materials**: see *Generated assets* below. Never clear a material the running
  editor has loaded: it asserts (`!IsRooted`) and crashes the editor.
- UE 5.8 treats `-Wshadow` and unreachable code as errors in plugins. Include what
  you use (a missing `UrvTypes.h` include broke the first build).
- Time of day: SCENE `solar_time` is local *solar* time; the time zone defaults to lon/15.
- The director settles on the newest frame when the feed stalls for
  max(0.25 s, 3 frame intervals), so a stopped stream does not bob vehicles through the ground.

## Generated assets (Content/Materials)
- Source of truth: `Tools/make_materials.py`. The `.uasset` files are its output,
  stored in **Git LFS** (`.gitattributes`). Clone with `git lfs install` first, or
  `git lfs pull` afterwards; `Tests/test_assets.py` fails on LFS pointer files.
- After changing `make_materials.py`: close the editor, run `Tools/regen_assets.sh`
  (headless Unreal; `$UE`, `$URV_HOST_PROJECT`), and commit the script, the
  `.uasset` files and `GENERATED.json` together. `test_assets.py` fails until then,
  because the stamp holds the script's sha256.
- This needs Unreal, so only on Yohei's machine (a local Claude Code session can
  run it). A cloud session cannot; it says so and leaves the gate red.
- In the editor, Tools -> Execute Python Script runs the same script but only
  creates *missing* materials.
- Done 2026-10-20 (`.uasset` now LFS pointers). On a new clone: `git lfs install` before cloning, or `git lfs pull`.

## Open work (as of 2026-10-13)
- **Vehicle models from powered-descent-sim: built; assets are generated, not committed.**
  `Tools/import_models.sh` (`PDS_DIR`, optional `URV_MODELS`; editor closed) runs
  `Tools/import_models.py`: it imports `models/out/*.glb` into `Content/Vehicles/<model>/`
  (StaticMeshes, `<model>.nodes.json`, manifest copy; about 120 MB, git-ignored). Decided
  2026-10-19: no LFS for these, each machine generates them; without them a SCENE `model`
  falls back to the procedural body. SCENE `model` selects the import (`BuildFromModel`);
  joints are driven from the manifest (`SetJoints`: FRAME `gimbal`, channels `legs_deploy`,
  `fins_deploy`, `fin_deflect`; absent = rest pose). The drone ship is a `passive` entity
  with `model` `droneship-class`; the sim's `--site droneship` declares it.
  Facts learned: Interchange bakes each node's whole world transform into its mesh vertices
  (cm, y negated; the root's lift above the foot plane included), so a mesh is attached
  with the inverse of its parent's rest world transform; animated glb nodes import as
  SkeletalMeshes, so `import_models.py` strips the `animations` first.
  Acceptance (Yohei): UE against the Blender renders (`shiphero` view, `--sea -2.8`);
  Neutron-class looks all black in UE so far. Not seen yet: strut telescoping mid-fold,
  grid fins moving, Neutron-class legs.
- **PR #6 `feat/paths`** (with powered-descent-sim PR #5): PATH messages, the
  guidance plan, earlier plans as ghosts, flown trails, P/T/H keys. Function works
  on Yohei's machine. **The visual style is not accepted yet.** Already rejected:
  plain debug lines; cyan glow; pastel cyan; magenta/amber three-layer
  (outline + body + core). Current candidate (c032157, `FUrvPalette`): azure route,
  amber objective ring + beacon, white->amber->red trail. Yohei found it dark at
  first; now `Glow` 10, chase exposure bias +0.5 EV, which looks "much better"
  but is not signed off. Next step: get Yohei's direction (a screenshot, or a
  reference look) before another round. A key that cycles a few palettes live
  would make comparing quick. Style knobs are on `AUrvPaths`: `Glow`,
  `LinePixels`, `Opacity`, and `FUrvPalette` / `FUrvPathStyle`.
- Chase camera defaults changed: FOV 65, arm 160 m (Yohei found it too close).
- Post-cutoff vent hiss (01f426f): when an engine goes off, `AUrvSoundscape` plays
  high-passed noise decaying over ~15 s (distance and speed-of-sound aware). Checked
  by audio analysis; Yohei listened and found it natural. Knobs: `VentVolume`,
  `VentLevel` in `UrvRoarSynth.cpp`. The web viewer fades the engine over 3 s on
  cutoff; here it stops at once, which sounded fine, so no fade was added.
- Lumen warning "Cached lighting ... clipped": host `Config/DefaultEngine.ini` sets
  `r.EyeAdaptation.CachedLightingPreExposure=10`; not confirmed gone yet.
- Cesium ion: the project default token must be set and saved in the host project
  (otherwise 401 and no terrain).
