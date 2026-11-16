# Observed on sky, water and cinematics (TASK40)

Host: the build VM, macOS arm64, `soft` at 640x480.

1. Sky: `demo1.dm2` (the default) shows the sky in one stretch of its
   camera path, near the end of the demo: at `PINS_EVERY` 30 it is frame
   21 (`skywater-000021.tga`), at `PINS_EVERY` 10 frames 62 to 64. All
   other captured frames of `demo1.dm2`, and all of `demo2.dm2`, are
   indoors.
   The sky-and-water card accepts the sky on frame 21 at the default
   settings.
2. Water: no warped water surface was seen. All 23 frames of `demo1.dm2`
   and all 21 of `demo2.dm2` at `PINS_EVERY` 30 were looked at, and at
   `PINS_EVERY` 10 (69 and 62 frames) 20 further frames of each demo,
   spread over the whole run, plus frames 60 to 68 of `demo1.dm2`, the
   outdoor stretch. With the
   owner's two demos the `skywater` scenario therefore pins the sky and
   the water-warp settings, not a water surface; a demo whose camera
   passes water is needed for that, and no such demo is in this data.
3. `skywater` run to run: identical (score and bad fraction 0 on all 23
   frames) over three runs, the third from a build with TASK40's capture
   change; the change did not alter a demo capture.
4. Cinematics are reached with `+demomap <name>.cin`: `SV_Map` sees the
   `.cin` extension and spawns an `ss_cinematic` server with the attract
   loop set, the client plays it through `SCR_PlayCinematic`, and at its
   last frame `SCR_FinishCinematic` asks the server for the next map; with
   an empty `nextserver` the server is killed and the client disconnects,
   as at the end of a demo. Before TASK40 the capture hook captured
   nothing there (`cl.refresh_prepped` stays false without a map) and the
   run ended with `pins: playback stopped before the demo ended`, exit 1,
   after about 6 seconds of `idlog.cin`.
5. A cinematic shows a new picture 14 times a second by wall clock,
   whatever the render rate, so counting rendered frames would capture
   different pictures from run to run. The hook now counts decoded
   cinematic frames. `PINS_DEMO=idlog.cin PINS_EVERY=7 pins.sh record soft
   cinematic` captures 12 frames in about 6 seconds and quits with status 0;
   frame 0 is black (the start of the cinematic), the later frames show it
   fading in and out. Three runs were identical (score 0 on all 12 frames).
   The second run is recorded with `record soft cinematic --run 2` and
   compared as `compare soft cinematic --run 2 --ref soft`; `--ref-id
   <dir>` names the reference when the scenario has been recorded with
   other settings (item 8).
   The picture on screen lags the decoded-frame count by one to two
   frames, and with `PINS_EVERY=1` the capture at index 1 falls on a
   rendered frame where nothing new is drawn, so a GL reference should
   not be recorded at N = 1.
   No `Dropped frame` line was logged.
6. `PINS_DEMO=nosuch.cin` exits 1 within a few seconds with `pins: the
   cinematic could not be played`.
7. The owner's data holds the cinematics `idlog.cin`, `ntro.cin`,
   `end.cin` and `eou1_.cin` to `eou8_.cin`, loose under `baseq2/video/`;
   `idlog.cin` is the shortest and is the one recorded.
8. `compare` without `--ref-id` takes the most recently modified complete
   directory under `<scenario>/<ref>/`, which can be a record made with
   other `PINS_*` settings (here, `demo2.dm2` or another `PINS_EVERY`): it
   then reports a frame-count difference. When one scenario is recorded
   with several settings, name the reference with `--ref-id`.
