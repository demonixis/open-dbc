# Classic animations

`dbc` only; DBPro animation commands stay unsupported. The `ANIMATION` commands
play video files with the operating system's media framework, sound included, so
OpenDBC ships no codec of its own.

## Decoding

| Platform | Framework | Plays |
| --- | --- | --- |
| macOS, iOS | AVFoundation | MP4, MOV and M4V (H.264, HEVC, ProRes); AVI only with Motion JPEG |
| Windows, Linux, Android | none yet | nothing: every animation is inert |

`LOAD ANIMATION` fails on a missing file. A file the platform cannot decode, such
as the Indeo, Cinepak, MS Video 1 or RLE AVI files of the period, loads as an
inert animation and logs a warning: it exists, measures 0 × 0 until placed and
never plays, so Classic programs keep running. Convert such files to MP4 (H.264)
to see them.

Each platform implements `VideoDecoder` (`src/engine/systems/video_decoder.hpp`)
in its own file, chosen by `src/engine/CMakeLists.txt`.

## Playback

Numbers run from 1 to 32. An animation starts at (0, 0) with its video size.
`PLAY ANIMATION` and `LOOP ANIMATION` start from the first frame, into the bitmap
given by the six-argument form or else the current bitmap. A position moves the
area and keeps its size; two corners set it, right and bottom exclusive, like
`PLACE ANIMATION`. `ANIMATION LOOPED` and `ANIMATION LOOPING` both report looping.

Each rendered frame paints the picture due at that moment, scaled into the area.
Pictures therefore advance at every `SYNC` with `SYNC ON` and at every automatic
refresh otherwise, while the sound plays on its own. A paused animation is
repainted, because a 3D frame clears the screen bitmap. Once stopped, finished or
deleted, the last picture stays in its bitmap, as in DarkBASIC.
