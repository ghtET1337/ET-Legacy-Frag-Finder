ETL-FRAG-FINDER By ght!

You can copy it/edit and whatever you want to do so.

It's "vibe coded" app within a day so nothing fancy, however any feedback or bugs reports you can send me dm on discord @ght on https://discord.gg/cGj5hq324g

Link also available on https://polandetlegacy.com

For more information please check rest of the docs files included with the app.

Version 1.7.3 adds automatic action-to-MP4 rendering through ET: Legacy's
video/audio pipe. It includes a render queue, batch export from Highlights,
quality/FPS/resolution controls, cancellation, MP4 finalization checks, an
in-app preview and an optional separate Discord-compatible 1080p/60 FPS copy.
Folder scan now lets you right-click an action to add it to the render queue
without opening the exporter, or load that demo directly in Multi-kill finder.
Use Render clip queue to open collected jobs without adding another action.
The header Profile / CFG menu can select an installed ETL launch profile,
the bundled Destiny fragmovie cfg example, or a custom CFG before demo
playback and rendering. Frag Finder uses that exact profile and no longer
creates ff_play_* or ff_render_* directories. A selected CFG replaces that
profile's etconfig.cfg after a timestamped etconfigBEFOREfragfinder backup.
The corrected build detects profiles from Windows Documents\ETLegacy rather
than confusing it with the etl.exe installation folder. It explicitly passes
fs_homepath and the selected cl_profile during normal -5s playback.
Check README.md and RELEASE-NOTES-1.7.3.txt before the first render; ETL 2.83+
and ffmpeg.exe are required. README.md includes the complete FFmpeg setup.

Cheers!
