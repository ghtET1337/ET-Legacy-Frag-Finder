ET: Legacy Frag Finder by ght — Version 1.7.6

CONTEXT MENU FIX — SAME VERSION 1.7.6

All four action tabs now have the same complete right-click menu:
Cut demo / Add to highlights / Add clip to render queue /
Load demo in Multi-kill finder / Open demo file location /
Copy ETL console command (-5s).

Commands use the row you right-click, also after sorting/filtering.

NEW: CUT DEMO (.dm_84)

1. Extract the complete package and run ETLFragFinder.exe.
2. Open/scan your demo and select an action.
3. Right-click it in Multi-kill finder, Folder scan, All kills / events,
   or Highlights, and choose "Cut demo (.dm_84)...".
4. Set the seconds BEFORE the first frag and AFTER the last frag.
   Defaults: 5 seconds before, 3 seconds after. Decimals are supported.
5. Check the requested range, select "Save cut demo..." and choose a NEW name.
6. Use "Show saved file" to locate the resulting .dm_84 for sharing.

Cutting needs neither ETL nor FFmpeg. The source demo is never overwritten.
The saved file contains only the cut plus the game state needed to load it.
It preserves the source POV. The recipient still needs the map and a compatible
mod. Timing follows recorded snapshots; the actual range appears after saving.
Zero margins retain at least two snapshots so the recording can be played.

The margins and last save folder are remembered separately from Render Clip.
Progress and cancellation run in the cut window. Failed/cancelled cuts leave
no partial output demo. Existing output names must be changed, not overwritten.

UPDATE FROM 1.7.5

Close Frag Finder, extract this ZIP into a new folder and run the new EXE.
Your saved index, highlights, render queue and settings in
%LOCALAPPDATA%\ETLFragFinder remain compatible. Keep your previous folder
until you have checked the new build on your demos.

The 1.7.5 playback/render waits (500 / 100 engine frames), Render Clip
com_zoneMegs 512 memory setting, and manual F9 Fast Capture are retained.
FFmpeg is still included for the existing video export functions.

See README.md and RELEASE-NOTES-1.7.6.txt for format limits and validation.
Feedback: Discord @ght — https://discord.gg/cGj5hq324g
Community: https://polandetlegacy.com
