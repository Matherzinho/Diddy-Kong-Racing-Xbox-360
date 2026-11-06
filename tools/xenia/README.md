# Testing dkr360 in Xenia

Xenia Canary runs the `.xex`, but xsdk's display never opens there (the LUT autofill times out, `0xC00000B5`), so the
port goes on **without a display**: no picture in Xenia's window. Everything else runs as on the console, and the port
leaves files behind to judge a run by:

| File beside the `.xex` | What it does |
|---|---|
| `dkr360_autopad.txt` | Golden Balloon's input script, one `frame BUTTONS [hold] [P1-P4]` line each. Golden Balloon's own routes work as they are, e.g. `grep -v '^#' tests/input_scripts/nav_to_time_trial_race.txt` (title → Time Trial race on Ancient Lake) or `race_4p_split.txt` (four players). |
| `dkr360_autopilot.txt` | Any content: `MDKR_AUTOPILOT=1`, the racer drives the racing line by itself. |
| `dkr360_capture.txt` | Rendered frame numbers, one a line: each goes to `dkr360_frame_<n>.ppm` (1280x720). `ppm2png.py <folder> <out>` makes half-size PNGs of them. Needs `--readback_resolve=full`. |
| `dkr360_menu_open.txt` | A tab number: the console menu starts open on it (for capturing it). |
| `dkr360_audio_dump.txt` | Any content: the first 8 MB played go to `dkr360_audio.raw` (22050 Hz, stereo, s16 big-endian). `audio_an.py <raw> <wav>` checks it and writes a WAV. |

`runx.ps1 -Seconds 120` starts Xenia (`--allow_game_relative_writes=true --readback_resolve=full`) and prints the log.
The log's heartbeat line every 5 s gives the rendered fps and the loudest audio sample since the one before:

    alive: 903 renderer steps since the last beat, 60.0 fps, audio peak 19099

Xenia has no controller unless the PC has one plugged in (`--hid=keyboard` didn't make XAM report one either), so the
controls can only be checked on the console; the menu logs which XAM ports and how many SDL joysticks it sees.

The build's `xenia_scan` lines list functions with instructions Xenia skips (`rldic` and the like): what goes through
them is unreliable in Xenia only.
