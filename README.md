# ET: Legacy Frag Finder by ght

ET: Legacy Frag Finder is a native Windows tool for indexing demo collections, finding multi-kills, building a clip shortlist and manually reviewing every obituary event stored in ET: Legacy `.dm_84` demos. It reads the binary protocol directly; no server log and no running game client are required for analysis.

## Version 1.7.3

This update adds the complete action-to-video workflow. Selected actions and saved highlights can be rendered with ET: Legacy's internal video/audio pipe instead of desktop capture. The queue supports pre-roll, post-roll, resolution, FPS, quality, HUD, cancellation, preview and batch rendering. The corrected stock controller covers the requested **demo-time** range even though offline rendering is slower than real time; the optional patched controller stops directly on demo `serverTime`.

Version 1.7.3 also waits for ffmpeg to finish and validates the final MP4 structure before marking a render complete, preventing interrupted `mdat`-only files without a playable `moov` atom. A new optional **Create Discord copy (1080p / 60 FPS)** setting preserves the original master and produces a second broadly compatible H.264/AAC file for Discord's embedded player. Folder results provide a four-command context menu: add an action silently to the render queue, load its source demo in Multi-kill finder, open its file location or copy a ready-to-paste ETL console command. **Render clip queue** opens the accumulated queue without adding the currently selected action.

The 1.7.3 maintenance build adds a persistent **Profile / CFG** selector before demo playback and rendering. Frag Finder now launches the exact profile selected by the user and no longer creates `ff_play_*` or `ff_render_*` profile copies. A selected custom CFG, including the optional cinematic example supplied by Destiny from Israel, is installed as that profile's `etconfig.cfg` after the existing file receives a timestamped backup.

The corrected 1.7.3 build distinguishes the ETL installation folder containing `etl.exe` from ETL's Windows user-data folder (`fs_homepath`). Profiles are detected from the real default location, normally `Documents\ETLegacy\legacy\profiles\<profile>\etconfig.cfg`. Normal −5-second playback preserves `fs_homepath`, `fs_game` and the selected `cl_profile` before `activeAction` and `+demo`.

## Quick start

1. Run `ETLFragFinder.exe` on Windows 10 or 11 x64.
2. Optionally open **Profile: Default • No CFG** in the header to choose the exact ETL launch profile and a startup/fragmovie CFG, then drop a `.dm_84` file onto the window or select **Open demo**.
3. Use **Multi-kill finder** to select a player, minimum kill count, minimum headshot count, maximum gap and weapon. Warmup kills are excluded by default; optional delayed-explosive handling is also disabled by default.
4. Open **All kills / events** to inspect the complete chronological obituary stream manually. Select **View full demo protocol** for every decoded message and the complete raw payload in a separate searchable window.
5. Select a row and choose **Play selected (−5s)**. On first use, locate your `etl.exe`.
6. Select **Render clip** to add the action and open the video queue, or use **Render clip queue** to inspect jobs already collected without adding another action. **Add to highlights** saves an action for later batch rendering.
7. Open **Demo library**, or use the search row in **Folder scan**, to search indexed demos by nickname, map, recording date or filename.

Double-clicking a multi-kill, individual event or saved highlight starts playback as well. The graphical timeline can be clicked to jump to the nearest row.

## Folder scan

The **Folder scan** tab searches an entire demo collection in one operation:

1. Select **Choose folder**, or drag a folder onto the application.
2. Set the minimum kill count, minimum headshot count, maximum gap, weapon, teamkill and warmup options.
3. Select **Update index**. Every `.dm_84` file in the folder and its subfolders is checked in the background. New or changed demos are parsed; unchanged demos are loaded from the persistent index.

The recorded POV is detected separately in every demo and used as that file's player filter. Demos without a detectable POV are skipped; unreadable files are counted without stopping the rest of the scan. Valid in-band download messages are consumed and ignored because their file payload is unrelated to frag analysis. If a recording ends with an incomplete final message, all earlier complete snapshots and events are recovered, indexed and reported as a parser warning instead of rejecting the entire demo. Select **Cancel scan** to stop after the current demo. Completed index updates are saved even after a cancelled scan.

After the scan, use **Search indexed demos** to enter part of a nickname, map, `YYYY-MM-DD` date or demo filename, choose the matching **Search field**, and select **Apply cached filters**. **Everything** checks all four fields; nickname search includes both the recorded POV and every player name stored for the demo. Clear the query to return to the complete selected folder.

The folder query is combined with minimum kills, minimum headshots, maximum gap, weapon, teamkills, warmup inclusion and the optional post-death explosive window. Results are recalculated from the SQLite event index; the `.dm_84` files are not read again. Demo payloads are loaded one at a time and only matching multi-kill rows remain in RAM. When the same folder is selected after restarting the application, matching index entries are available immediately. **Update index** then reparses only new files or files whose size, modification timestamp or partial content hash changed.

Enable **Auto-index new demos** to watch the selected folder. File-system notifications are debounced, so a demo still being written is indexed after activity settles instead of triggering a scan for every write. Automatic and manual updates both retain the previous visible results while they run. Removed files are pruned from the index, and a manual update lists the filenames and parser errors for unreadable demos.

The aggregate result table shows the relative demo filename, map, recorded POV, demo time, match clock, kill count, confirmed headshot-hit count, duration, victims and weapons. Selecting a result fills the detail table below it and updates the timeline. Double-click a result or select **Play selected (−5s)** to open the correct demo in ET: Legacy five seconds before that multi-kill.

Right-click a result for four direct actions:

- **Add clip to render queue** collects the action without opening the clip-exporter window, making it practical to shortlist many results quickly;
- **Load demo in Multi-kill finder** opens that exact source demo in the single-demo tab so its other actions can be inspected;
- **Open demo file location** selects the source file in Windows Explorer;
- **Copy ETL console command (-5s)** copies a command for an already running ETL client. It sets `activeAction`, loads the absolute demo path and seeks five seconds before the action after cgame is ready.

## ETL profile and CFG selection

Use the **Profile: … • … CFG** button in the application header before playing a demo or starting the render queue. Its menu provides:

- **Default / current ETL profile**, which preserves the current ETL profile only when no replacement CFG is selected;
- every valid profile containing `etconfig.cfg` under the selected ETL user-data folder's mod-specific `profiles` or legacy `profile` directories, labelled with its source mod;
- **No startup CFG**, the bundled **Destiny fragmovie example (Israel)**, or any custom `.cfg` file;
- **Use default Documents\ETLegacy**, **Choose ETL user-data folder** and **Refresh detected profiles** when ETL uses an unusual `fs_homepath`;
- **Remove old ff_play / ff_render profiles**, which lists the exact obsolete folders and deletes them only after an explicit confirmation.

The selected profile is used directly for both playback and clip rendering. Frag Finder never creates another profile directory. A startup/fragmovie CFG requires an explicitly selected profile; choosing a CFG while **Default / current ETL profile** is active produces a clear error instead of guessing which profile should be modified. The profile must belong to the same mod as the demo, for example a profile labelled `[legacy]` for a Legacy demo.

The ETL executable directory and user-data directory are separate settings. Frag Finder finds the latter through Windows' actual Documents known-folder location, including redirected/OneDrive Documents folders. An older ambiguous clip-export setting is accepted only if it really contains profiles; otherwise an existing standard Documents profile tree is selected automatically.

Before launch, Frag Finder copies the selected CFG into the selected profile as `etconfig.cfg`. If that profile already has an `etconfig.cfg`, it is first renamed to `etconfigBEFOREfragfinder-YYYYMMDD-HHMMSS.cfg`; a numeric suffix prevents collisions when several launches occur within the same second. The replacement is transactional: Frag Finder writes a temporary file first and restores the previous config if final installation fails. When the selected CFG is already byte-for-byte identical to `etconfig.cfg`, no redundant backup is created. Backups are intentionally not restored or deleted automatically.

Frag Finder explicitly passes `+set fs_homepath`, selects the mod inferred from the demo's `demos` directory and passes the selected profile name through `+set cl_profile`. It does not use `+exec` for the selected CFG because ETL loads the installed `etconfig.cfg` as the profile starts. During rendering, queue FPS, resolution, HUD and safe windowed-render settings remain command-line overrides and are applied before the demo action. The ETL user-data folder, selected profile path and CFG choice are stored in `%LOCALAPPDATA%\ETLFragFinder\settings.ini` and restored in another copy of the same or a newer Frag Finder build.

`presets\destiny-fragmovie.cfg` is an optional example supplied/modified by the player Destiny from Israel. It targets a high-quality 2560×1440 cinematic presentation, hides many HUD elements and includes Destiny's own movie-oriented choices. It is not forced, it is not an ET: Legacy default, and users are encouraged to copy and adapt it for their own fragmovie workflow.

## Automatic clip rendering

Select an action in **Multi-kill finder**, **All kills / events**, **Folder scan** or **Highlights**, then choose its **Render clip** button. The dedicated clip exporter receives the exact demo and action timestamps. **Render entire basket** adds every saved highlight for unattended sequential rendering. In Folder scan, the right-click **Add clip to render queue** command can collect many actions without repeatedly opening the exporter. Select **Render clip queue** below the normal Render clip button at any time to open the existing queue without adding the current selection.

The exporter provides:

- editable pre-roll and post-roll;
- 720p, 1080p, 1440p and 4K presets plus custom width and height;
- editable FPS with 30, 60 and 120 FPS shortcuts;
- Master, High, Balanced and Compact H.264/AAC profiles;
- optional HUD rendering;
- an optional Discord-compatible 1080p constant-60-FPS copy while preserving the original master;
- a persistent output folder, ETL user-data (`fs_homepath`) folder and ffmpeg location, plus the profile/CFG selection made in the main window;
- a render queue with per-item status, cancellation, retry-friendly queued items and ffmpeg logs;
- an embedded Windows Media Foundation MP4 preview plus **Open externally**;
- automatic relocation from ET: Legacy's root or mod-specific `videos` directory to the selected output folder.

Clips are rendered inside ET: Legacy rather than captured from the desktop. This avoids fullscreen/window-capture limitations and sends ETL's internally rendered frames plus its mixed game audio directly to ffmpeg. Frag Finder forces the SDL-compatible sound backend, 16-bit stereo-compatible capture path and disables mute-on-unfocused/minimized for the dedicated render process. Rendering uses the exact selected profile. If a movie CFG is selected, the previous profile config remains available under its timestamped `etconfigBEFOREfragfinder-*` backup name.

Frag Finder launches a dedicated ETL render instance for each queued clip. That render window closes automatically after `stopvideo` so ffmpeg can flush the MP4 and the next queue item can start; this is expected completion rather than a game crash. ETL normally writes to `fs_homepath/fs_game/videos`, for example `Documents\ETLegacy\legacy\videos`. Frag Finder checks the selected user-data folder, the demo's mod directory, the ETL installation and one-level mod folders before deciding that an MP4 is missing.

### Requirements and range controllers

- ET: Legacy 2.83 or newer, because that release introduced `video-pipe`;
- `ffmpeg.exe` copied directly into the selected ET: Legacy installation folder next to `etl.exe` — this is the recommended setup; **Locate ffmpeg** remains available as a fallback;
- the correct ETL user-data / `fs_homepath` folder. Normally this is the Windows known-folder path `Documents\ETLegacy`; it is not the installation directory next to `etl.exe`.

**Stock ETL (corrected time)** works with an unmodified ET: Legacy client. Frag Finder converts the chosen range to `seek startTime`, forces `timescale 1` and the selected `cl_aviFrameRate` after the demo has loaded, then starts `video-pipe`. ETL advances demo time by `1000 / FPS` for each captured frame. Its command buffer runs twice per rendered frame, so Frag Finder waits two command-buffer passes for each requested output frame before `stopvideo`. This is based on demo progression, not wall-clock rendering speed; a clip that takes minutes to render still covers the selected in-demo start and end times.

**Patched ETL (exact serverTime)** calls `video-pipe-range filename startTime endTime`. That command is not present in stock ET: Legacy. It seeks to `startTime` and stops only after the demo's `serverTime` reaches `endTime`, independently of render speed and FPS. Source and build instructions for the optional engine patch are included under `etlegacy-video-pipe-range`. Frag Finder scans the selected `etl.exe` for the command before launch; when it is missing, the user can use the corrected stock controller.

Install `ffmpeg.exe` directly next to the selected `etl.exe`. This is the recommended setup for normal and elevated launches because ETL can find the encoder without depending on another application directory or a temporary `PATH` entry. Do not enable **Launch ETL as administrator** unless that ETL installation genuinely needs elevation.

### Installing FFmpeg on Windows

When the Frag Finder release ZIP contains an `ffmpeg` folder, install its encoder once as follows. If that folder is absent, download the Gyan Windows release essentials build first:

1. Open the release package's `ffmpeg\bin` folder and locate the actual `ffmpeg.exe`. Do not use the outer folder, archive or `ffplay.exe`.
2. Find the ET: Legacy installation selected in Frag Finder. It is the folder containing `etl.exe`, not the user-data folder under `Documents\ETLegacy`.
3. Copy `ffmpeg\bin\ffmpeg.exe` directly into that game installation folder so the runtime layout is:

   ```text
   <ET: Legacy installation>\
     etl.exe
     ffmpeg.exe
   ```

4. Leave the packaged `ffmpeg` folder and its upstream license, README and build-information files intact in the Frag Finder distribution.
5. Start Frag Finder and select the same `etl.exe`. The render queue should detect the adjacent `ffmpeg.exe` automatically.
6. If it is not detected, use **Clip exporter → Locate ffmpeg** and select the copy next to `etl.exe`; that location is saved.
7. To verify the installation, open Command Prompt in the ETL installation folder and run `ffmpeg.exe -version`. If Windows prints the build and library versions, the executable works.

If FFmpeg must be downloaded separately, use the [official FFmpeg download page](https://ffmpeg.org/download.html) and its linked [gyan.dev Windows builds](https://www.gyan.dev/ffmpeg/builds/). `ffmpeg-release-essentials.zip` is sufficient; the full build also works but is not required.

Running `ffmpeg.exe` by itself may end with `At least one output file must be specified`. That is not an installation failure: it means FFmpeg started correctly but was not given an input/output command. Frag Finder supplies those arguments during rendering.

### Original master and Discord copy

The selected resolution, FPS and quality always define the main MP4. For example, a 1920×1080 120 FPS render remains a 120 FPS master suitable for local playback or editing.

Enable **Create Discord copy (1080p / 60 FPS)** to run a second conversion after the master has been finalized. Frag Finder creates a separate `*-discord.mp4` with:

- 1920×1080 output with aspect ratio preserved and black padding when required;
- constant 60 FPS;
- H.264 High profile, level 4.2, `yuv420p`;
- AAC stereo at 48 kHz;
- an MP4 fast-start index for streamed playback.

This avoids the choppy-looking embedded playback that Discord may show for a locally smooth 120 FPS/high-bitrate upload. The option does not overwrite or lower the quality of the master. It targets playback compatibility, not a particular Discord upload-size limit. If the secondary conversion fails or is cancelled, the completed master remains available and the queue reports only the Discord-copy warning.

### Clip-export troubleshooting

- **ETL says it cannot find FFmpeg:** verify that `ffmpeg.exe` is next to the exact `etl.exe` selected in Frag Finder, then select that copy with **Locate ffmpeg**. This adjacent-file setup also works when ETL is elevated.
- **ETL closes after the requested range:** this is normal. Frag Finder launches a dedicated render process and closes it so FFmpeg can finalize the MP4 and the queue can continue.
- **ETL closes too early:** use **Stock ETL (corrected time)** with an unmodified ETL 2.83+ or select a genuinely patched executable for **Patched ETL (exact serverTime)**. Version 1.7.3 accounts for ETL executing two command-buffer `wait` passes per captured frame.
- **Rendering looks slow inside ETL:** offline `video-pipe` capture is frame-based, not wall-clock recording. The selected demo range remains unchanged even if generating it takes much longer than the clip duration.
- **The queue shows `finalizing MP4`:** wait for FFmpeg to close the file and write `moov`. Frag Finder allows up to two minutes and accepts the file only after `ftyp`, `moov` and `mdat` are present.
- **A render or Discord copy fails:** open the selected output folder and inspect the matching `.ffmpeg.log.txt`. An incomplete temporary Discord file is removed; the master is retained.
- **Output seems to be missing:** ETL writes below `fs_homepath\fs_game\videos`. Frag Finder searches the configured ETL user-data folder, the demo's mod folder, the ETL installation and immediate mod folders before relocating the completed file.
- **No profiles are listed:** the disabled path in the header menu must point to ETL user data, normally `Documents\ETLegacy`, not the installation directory containing `etl.exe`. Select **Use default Documents\ETLegacy**, then refresh.
- **A selected profile or CFG cannot be prepared:** open the header setup menu, verify `fs_homepath`, refresh the profile list and reselect any custom CFG that was moved. Frag Finder reports the exact missing source or unwritable target path.

## Persistent demo index

Parsed demo metadata, player sessions, match phase, obituary events and confirmed headshot hits are stored in `demo-index-v3.sqlite3` under `%LOCALAPPDATA%\ETLFragFinder`. SQLite transactions and WAL mode keep the index durable while the folder watcher updates it in the background. The index survives application restarts and is validated against each demo's path, file size, modification timestamp, partial hash and parser revision. Changed files are automatically reparsed even if their size and timestamp were preserved; filter changes never require reparsing. Version 1.7.1 added per-hit data without deleting the existing database, and 1.7.2 reuses that data with the corrected action boundary. Version 1.7.3 keeps the same database and highlight formats, so upgrading does not require another scan.

Each indexed file also receives a partial content hash built from its size plus blocks at the beginning, middle and end. Files with the same size and partial hash are marked as duplicates in **Demo library**. This is fast duplicate detection for managing demo collections, not a cryptographic proof that two files are identical.

Opening an individual demo also uses and updates the same index. If the index is missing or cannot be read, the application safely creates a new one during the next analysis.

## Demo library search

The **Demo library** tab searches the persistent index without opening or reparsing demos. Choose **Everything**, **Nickname**, **Map**, **Date** or **Demo filename**, type any fragment and select **Search index**. Date values use `YYYY-MM-DD`; the recorder date is taken from a date in the demo filename when present and otherwise from the file modification date.

**Selected folder only** restricts the search to the active folder and its subfolders. **Duplicates only** displays partial-hash duplicate groups and the **Duplicates** column shows the number of matching files. Results include filename, date, map, recorded POV, player/event counts, duration and full path. Double-click a row or use **Open selected demo** to inspect it.

## Highlight basket

Select a result in **Multi-kill finder** or **Folder scan**, then choose **Add to highlights**. The dedicated **Highlights** tab stores the demo path, map, recorded POV, timestamps, kill count, confirmed headshot-hit count, victims and weapons. Duplicate entries are rejected.

The basket is saved automatically under `%LOCALAPPDATA%\ETLFragFinder` and restored on the next launch. Saved entries can be sorted, played from five seconds before the first kill, removed individually, cleared after confirmation or exported.

## Graphical timeline

The timeline displays the full demo duration and uses:

- green markers for kills made by the recorded POV;
- red markers for POV deaths or teamkills;
- amber markers for warmup events;
- blue ranges for multi-kill sequences;
- muted markers for other obituary events.

Moving the pointer over the timeline shows the exact demo time. Clicking it selects the closest multi-kill or event in the active table, and a bright marker shows the selected row. In folder view, the timeline is loaded on demand and strictly scoped to the demo belonging to the selected aggregate row: its exact filename is displayed, only that demo's recorded-POV kills/deaths are drawn and the selected multi-kill is emphasized. No markers from another folder demo are combined with it. A saved highlight uses a local range from ten seconds before to ten seconds after the clip instead of compressing it against an invented full-demo duration.

## Export

**Export current view** writes the active filtered results, current event-player log, folder results, highlight basket or demo-library search as either UTF-8 CSV or structured JSON. Export rows follow the table's current visual sort order. CSV text that could be interpreted as a spreadsheet formula is neutralized. Exports include source demo, map, POV, demo/match timestamps, kill and confirmed headshot-hit counts. Per-kill obituary data labels the separate `headshot_kill` flag explicitly.

## Multi-kill detection

- A player's death ends the current life and therefore the current sequence.
- Every obituary from the same server snapshot is processed as one simultaneous group. If a player kills several enemies and also dies in that explosion, all enemy kills are counted before the sequence is closed.
- **Post-death explosives** is optional and disabled by default, so the original death rule is unchanged unless the user enables it. Choose `3.0`, `5.0`, `8.0` seconds or type a custom value. **Window (seconds)** remains editable while the option is off; the checkbox only decides whether the value is applied.
- When enabled, delayed kills from grenades, rifle grenades, landmines, Panzerfaust/Bazooka, artillery, airstrikes, mortars, dynamite, satchels and related explosives may extend the sequence through a death inside that window. A direct-fire frag such as MP40, Thompson or Sten closes the old sequence and starts a new life.
- **Minimum kills** controls how many matching enemy kills are required.
- **Minimum headshots** requires a chosen number of confirmed headshot hits inside the action. Set it to `0` to disable the filter. Every `HIT_HEADSHOT` made by the action's attacker against one of its eventual victims is counted from five seconds before the first kill through the last kill. This matches the **Play selected (−5s)** clip lead-in and includes shots that directly cause the first frag instead of starting too late at its obituary. The window is cut at the attacker's most recent death, so a previous life can never inflate the result. Multiple headshots on the same victim are counted separately; an unrelated opponent who survives the action does not inflate the result. A selected weapon applies to both kills and headshot hits.
- **Maximum gap (seconds)** is measured between matching kills. Set it to `0` to search the player's complete life with no time split.
- When a weapon is selected, only kills from that weapon count toward the result.
- Teamkills are excluded unless **Include teamkills** is enabled. Suicides and world deaths never count as frags.
- Warmup, warmup-countdown, waiting-for-players and reset-state kills are excluded unless **Include warmup kills** is enabled. Warmup and live-play sequences are always kept separate. Events whose phase cannot be identified remain eligible to avoid silently dropping data from compatible third-party mods.

## All kills / events table

The table is populated automatically when a demo is loaded. **Player log** defaults to **All players**, which includes every parsed obituary event with:

- event number and demo timestamp;
- remaining match clock when available;
- attacker and victim;
- weapon;
- event classification: kill, headshot kill, teamkill, suicide or world death, plus a warmup/intermission/unknown-phase label when applicable.

Choose a specific player to show every event in which that player is the attacker or victim. This includes their kills, deaths, suicides and teamkills. Player identity combines the protocol client slot with a parser session derived from config-string presence and the clean nickname. If a slot is reused after disconnect/reconnect, its occupants appear separately instead of having their frag runs merged. The tab displays `visible / total` counts while filtered, the timeline switches to matching player events, and export writes the same visible subset. Select **All players** to restore the complete obituary log for manual verification. Warmup events remain in this manual log and are labelled in amber; red rows identify teamkills or suicides and green rows identify headshot kills.

### Full demo protocol inspector

Select **View full demo protocol** to decode the currently opened `.dm_84` again in an isolated, on-demand pass. The separate window contains file-order records for message headers, reliable acknowledgements, gamestate, every configstring, baselines, raw server commands, snapshots, complete POV player state arrays, changed entity states, download metadata, semantic obituary events and decoded `HEADSHOT_HIT` rows. Every original message payload is also included as ordered `RAW` hex rows, so data not assigned a friendly protocol label remains visible.

The inspector supports case-insensitive **Find next** and **Save text…**. Full dumps can be tens of megabytes, so they are never stored in SQLite and are generated only when requested. Closing the inspector releases its memory; normal demo loading and folder scanning remain unchanged.

## Sorting tables

Every result, detail and highlight table can be sorted by selecting a column header. Select the same header again to reverse the direction. The first selection of **Kills** or **Headshots** sorts from the largest value to the smallest. A blue arrow in the header shows the active direction.

## Playback

The application launches `etl.exe` with the demo's absolute path and normally seeks to five seconds before the selected multi-kill or individual event. The seek is queued through ET: Legacy's `activeAction` and runs only after the first active demo snapshot; this avoids trying to seek while `cgame` is still loading. The selected ETL profile and any transactional `etconfig.cfg` replacement are prepared before this command, and the same selection is passed to clip jobs.

Enable **Launch ETL as administrator** only on systems where direct playback reports missing `cgame` files or otherwise requires elevation. Frag Finder then launches only `etl.exe` with Windows' `runas` verb and the normal UAC prompt; the analyzer itself remains unelevated. This explicit option works even when administrator mode was configured only on a desktop shortcut that Frag Finder does not use. Enable **Launch without seeking** to diagnose startup separately from seeking: ETL receives only the demo command and starts at the beginning.

The most recent executable, working directory, demo path, launch verb, elevation choice, seek mode, ETL user-data path, selected profile folder, launch profile name, installed CFG, timestamped backup path, complete arguments and Windows launch result are written to `%LOCALAPPDATA%\ETLFragFinder\playback-launch.log`. Playback options, the chosen executable/user-data/profile/CFG, selected demo folder and watcher preference are stored in `%LOCALAPPDATA%\ETLFragFinder\settings.ini`. Settings from an older portable `ETLFragFinder.ini` are migrated on first launch.

## Supported format

Version 1.7.3 supports protocol 84 used by ET: Legacy 2.84.x. The parser's network structures and enums follow ET: Legacy. A different mod may use incompatible event values even if its demo has the same extension. ETTV `.tv_84` demos are not supported in this release.

The executable is not code-signed, so Windows SmartScreen may warn about a newly downloaded application. Full source code and SHA-256 checksums are included in the release archive.

## Command line interface

`etl-frag-cli.exe` provides text, JSON and scripted multi-kill searches:

```text
etl-frag-cli.exe demo.dm_84 --json
etl-frag-cli.exe demo.dm_84 --runs --player 3 --min 3 --gap 8 --weapon 53
etl-frag-cli.exe demo.dm_84 --runs --player 3 --min 3 --min-headshots 2
etl-frag-cli.exe demo.dm_84 --runs --player 3 --session 12 --min 2
etl-frag-cli.exe demo.dm_84 --runs --post-death-explosives 5
etl-frag-cli.exe demo.dm_84 --runs --include-warmup
```

`--player`, `--session` and `--weapon` accept IDs visible in JSON output. Numeric options reject trailing garbage, infinity and NaN. `--min-headshots N` filters actions by confirmed headshot-hit count, `--gap 0` disables the time split, `--teamkills` includes teamkills, `--include-warmup` includes warmup kills, and `--post-death-explosives SECONDS` enables the optional delayed-explosive window. JSON exposes aggregate `headshotHits` separately from each obituary's `headshotKill` flag. Text and JSON event output include the detected match phase.

## Building

From **Developer Command Prompt for Visual Studio 2022**:

```text
build_windows.bat
```

Or with CMake:

```text
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release
```

Linux builds the CLI and tests only: `./build_linux.sh`.

## License

ET: Legacy Frag Finder is distributed under GNU GPL v3 or later. See `COPYING.txt` and `THIRD_PARTY_NOTICES.md`.
