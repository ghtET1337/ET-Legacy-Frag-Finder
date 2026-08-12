ETL-FRAG-FINDER By ght!

STARTUP HOTFIX (1.7.4)

This package fixes the maintenance EXE that could exit silently before opening
the main window. Future startup exceptions display an error and are written to
%LOCALAPPDATA%\ETLFragFinder\startup.log. The application remains version 1.7.4
and Fast Capture remains the manual F9 recorder.

You can copy it/edit and whatever you want to do so.

It's "vibe coded" app within a day so nothing fancy, however any feedback or bugs reports you can send me dm on discord @ght on https://discord.gg/cGj5hq324g

Link also available on https://polandetlegacy.com

For more information please check rest of the docs files included with the app.

Version 1.7.3 added automatic action-to-MP4 rendering through ET: Legacy's
video/audio pipe. It includes a render queue, batch export from Highlights,
quality/FPS/resolution controls, cancellation, MP4 finalization checks, an
in-app preview and an optional separate Discord-compatible 1080p/60 FPS copy.
Folder scan now lets you right-click an action to add it to the render queue
without opening the exporter, or load that demo directly in Multi-kill finder.
Use Render clip queue to open collected jobs without adding another action.
The header Profile / CFG menu can select an installed ETL launch profile,
the bundled Destiny fragmovie example (Israel), or a custom CFG before demo
playback and rendering. Frag Finder uses that exact profile and no longer
creates ff_play_* or ff_render_* directories. A selected CFG replaces that
profile's etconfig.cfg after a timestamped etconfigBEFOREfragfinder backup.
The corrected build detects profiles from Windows Documents\ETLegacy rather
than confusing it with the etl.exe installation folder. It explicitly passes
fs_homepath and the selected cl_profile during normal -5s playback.
Check README.md and RELEASE-NOTES-1.7.4.txt before the first render; ETL 2.83+
and ffmpeg.exe are required. README.md includes the complete FFmpeg setup.

Version 1.7.4 adds explicit Fast capture frame pacing. Smooth constant FPS is
the default for regular playback; Only new desktop frames keeps diagnostic VFR.
The final status reports output FPS, approximate source FPS, paced duplicates
and dropped frames. The 250+ FPS NVENC path now uses lower-overhead P3 settings.

The current Fast Capture safety update warns before the capture window opens:
Alt+Tab, switching windows/displays, fullscreen transitions, resolution changes
or refresh-rate changes may invalidate Desktop Duplication and stop FFmpeg with
error 887a0026. Select Copy commands and open Fast Capture, or use the Copy ETL
windowed commands button, then paste `r_fullscreen 0; vid_restart` into the ETL
console. Frag Finder does not auto-resume or join an interrupted recording with
a later clip; it keeps that recording's recoverable MKV and log separately.

This 1.7.4 maintenance update also restores the previous application session
from SQLite: the library view, last demo and folder, active tab, searches,
filters, table sorting and main-window position. The normal Render Clip queue,
interrupted jobs and completed render history survive a restart as well. A job
interrupted while Starting or Rendering returns to Queued. Fast capture remains
the manual F9 recorder and has no automatic action queue in this build.

Render Clip now forces seta demo_infoWindow 0 before loading and again after
cgame becomes active. Before ETL is opened, a separate warning window lists all
profile, audio, display, FPS, HUD and video-pipe commands and allows the user to
start the queue or cancel safely.

Cheers!
