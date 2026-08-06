# ET: Legacy Frag Finder by ght

ET: Legacy Frag Finder is a native Windows tool for indexing demo collections, finding multi-kills, building a clip shortlist and manually reviewing every obituary event stored in ET: Legacy `.dm_84` demos. It reads the binary protocol directly; no server log and no running game client are required for analysis.

## Quick start

1. Run `ETLFragFinder.exe` on Windows 10 or 11 x64.
2. Drop a `.dm_84` file onto the window or select **Open demo**.
3. Use **Multi-kill finder** to select a player, minimum kill count, minimum headshot count, maximum gap and weapon. Warmup kills are excluded by default; optional delayed-explosive handling is also disabled by default.
4. Open **All kills / events** to inspect the complete chronological obituary stream manually. Select **View full demo protocol** for every decoded message and the complete raw payload in a separate searchable window.
5. Select a row and choose **Play selected (−5s)**. On first use, locate your `etl.exe`.
6. Use **Add to highlights** to save a promising sequence, or **Export current view** to create CSV/JSON output.
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

## Persistent demo index

Parsed demo metadata, player sessions, match phase, obituary events and confirmed headshot hits are stored in `demo-index-v3.sqlite3` under `%LOCALAPPDATA%\ETLFragFinder`. SQLite transactions and WAL mode keep the index durable while the folder watcher updates it in the background. The index survives application restarts and is validated against each demo's path, file size, modification timestamp, partial hash and parser revision. Changed files are automatically reparsed even if their size and timestamp were preserved; filter changes never require reparsing. Version 1.7.1 added per-hit data without deleting the existing database, and 1.7.2 reuses that data with the corrected action boundary. Collections indexed by 1.7.0 or older require one **Update index** pass; unchanged files are then reused normally.

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

The application launches `etl.exe` with the demo's absolute path and normally seeks to five seconds before the selected multi-kill or individual event. The seek is queued through ET: Legacy's `activeAction` and runs only after the first active demo snapshot; this avoids trying to seek while `cgame` is still loading.

Enable **Launch ETL as administrator** only on systems where direct playback reports missing `cgame` files or otherwise requires elevation. Frag Finder then launches only `etl.exe` with Windows' `runas` verb and the normal UAC prompt; the analyzer itself remains unelevated. This explicit option works even when administrator mode was configured only on a desktop shortcut that Frag Finder does not use. Enable **Launch without seeking** to diagnose startup separately from seeking: ETL receives only the demo command and starts at the beginning.

The most recent executable, working directory, demo path, launch verb, elevation choice, seek mode, arguments and Windows launch result are written to `%LOCALAPPDATA%\ETLFragFinder\playback-launch.log`. Playback options, the chosen ET: Legacy path, selected demo folder and watcher preference are stored in `%LOCALAPPDATA%\ETLFragFinder\settings.ini`. Settings from an older portable `ETLFragFinder.ini` are migrated on first launch.

## Supported format

Version 1.7.2 supports protocol 84 used by ET: Legacy 2.84.x. The parser's network structures and enums follow ET: Legacy. A different mod may use incompatible event values even if its demo has the same extension. ETTV `.tv_84` demos are not supported in this release.

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
