# Recording a clip (video with sound) on Linux/X11

How the T+0 to landing demo clip was made on Yohei's machine (Ubuntu 22.04, X11,
RTX GPU). Needs OBS Studio (`apt install obs-studio`, 27.x), `ffmpeg`, `xwininfo`.

## What does not work
- **`ffmpeg -f x11grab`** is not frame-synchronised with the game: the clip stutters,
  and CPU x264 at 60 fps makes the game itself drop to ~50 fps.
- **Capturing the screen** records whatever is in front (an IDE, a private window).
  Capture the game *window*.

## Setup (once)
OBS profile and scene collection both named `URV`:
- Video 1920x1042 (the window area below the title bar), 60 fps, NV12, Rec.709.
- Output: simple, mkv, quality HQ, x264, to `~/obs_rec`. (An NVENC encoder offloads
  the CPU further if the game is CPU bound.)
- Scene `Scene` with a source `Game` of type *Window Capture (Xcomposite)*, cursor off.
- Desktop audio: an *Audio Output Capture (PulseAudio)* source on the default device.
  In the scene JSON the global `DesktopAudioDevice1` must be an object holding the
  source definition (`id: pulse_output_capture`), not a string.

## Each take
1. Close the editor. Start the **game** (not PIE), fresh each time; a game that has been
   running for hours can lose its audio:
   ```bash
   $UE/Engine/Binaries/Linux/UnrealEditor "$P/RocketVizHost.uproject" /Game/NewMap \
     -game -windowed -ResX=1920 -ResY=1080 -WinX=0 -WinY=0 -UrvGroup=none -NoSplash \
     -ExecCmds="t.MaxFPS 60,DisableAllScreenMessages"
   ```
   `DisableAllScreenMessages` hides on-screen notices; `t.MaxFPS 60` matches the capture.
2. Start the sender first. The scene is empty (black) until SCENE arrives. The closed loop
   takes about 46 s to solve before it streams. `--hold 25` keeps the first frame so tiles
   load before the flight starts:
   ```bash
   .venv/bin/python -u -m sim.urv_stream --closed-loop --dest 127.0.0.1 --hold 25 --wind 0 8 0
   ```
   Use the sim's `.venv` python (the system one lacks `cvxpy`).
3. Wait for tiles and the "Preparing Shaders" overlay to finish; give the game focus
   (click it, or send `_NET_ACTIVE_WINDOW`) and check with `xprop -root _NET_ACTIVE_WINDOW`.
4. Put the game window id into the `Game` source. The id changes at every launch:
   `xwininfo -root -tree | grep RocketVizHost` gives it in hex; OBS stores
   `capture_window` as `"<decimal id>\r\n<title>\r\nUnrealEditor"` in
   `~/.config/obs-studio/basic/scenes/URV.json` (edit with OBS closed).
5. Record:
   ```bash
   obs --profile URV --collection URV --startrecording --minimize-to-tray --disable-shutdown-check
   ```
   Stop with `pkill -x -TERM obs` (wait for it to exit so the mkv is finalised).
6. Trim the held start and the end, then make an mp4 (`-ss` is where T+0 starts):
   ```bash
   ffmpeg -ss 25 -t 22 -i "~/obs_rec/<file>.mkv" -c:v libx264 -crf 18 -pix_fmt yuv420p \
     -c:a aac -b:a 192k out.mp4
   ```
   The flight is 20.3 s at 50 Hz; leave a few seconds after touchdown for the vent hiss
   (it fades out over about 15 s).
7. Check it: frame grabs (`ffmpeg -ss t -i out.mp4 -frames:v 1 f.png`), and the sound with
   `-af volumedetect`. Silent mp4 means the audio source or the game's audio died; relaunch.

## Sending the file to a phone or another PC
Send the mp4 through the session (SendUserFile) or any file share; 60 fps is ~28 MB for
the 20 s flight, 30 fps ~23 MB.

## Posting
Credit Cesium (ion / OpenStreetMap, as shown on screen). Describe the vehicle as
"-class", never by company logo, and say it is a simulation.
