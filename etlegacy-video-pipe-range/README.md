# Optional native `video-pipe-range` command

ET: Legacy Frag Finder 1.7.4 works with an unmodified ET: Legacy 2.83 or newer through the corrected stock time controller. The optional patch provides direct engine-side `serverTime` range control.

The **Native video-pipe-range** controller is an optional engine-side implementation. It accepts exactly:

```text
video-pipe-range filename startTime endTime
```

`startTime` and `endTime` are seconds from the first demo snapshot. The command seeks, starts the normal ET: Legacy video/audio pipe and closes it after the first captured frame at the requested end time. Frag Finder sets `cl_videoPipeRangeQuit 1`, which makes the dedicated render client exit after ffmpeg has been closed and flushed. The patch also closes pipe processes with `_pclose`/`pclose`, ensuring ffmpeg writes the final MP4 `moov` atom before ETL exits.

## Build

1. Clone the current ET: Legacy source tree.
2. From its repository root, apply `video-pipe-range.patch`:

   ```text
   git apply video-pipe-range.patch
   ```

3. Build ET: Legacy normally and select the resulting `etl.exe` in Frag Finder.

Frag Finder scans the chosen executable for the registered command before rendering. If the selected binary is not patched, it offers to switch that queue to **Stock ETL (corrected time)**. No modified engine is required for the stock controller.

This patch is source code only and is provided under the same GPL-compatible terms as the application. It was prepared against ET: Legacy upstream commit `7a784b4504977caf1c44acf668f02cacd2153632`.
