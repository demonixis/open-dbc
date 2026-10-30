# Classic animations

`dbc` only; DBPro animation commands stay unsupported. The `ANIMATION` commands
play video files with the operating system's media framework, sound included, so
OpenDBC ships no codec of its own.

## Decoding

| Platform | Framework | Plays |
| --- | --- | --- |
| macOS, iOS | AVFoundation | MP4, MOV and M4V (H.264, HEVC, ProRes); AVI only with Motion JPEG |
| Linux | GStreamer 1.x | containers and codecs provided by the installed system plugins |
| Windows | Media Foundation | containers and codecs installed with Windows, including MP4/H.264 |
| Android | none yet | nothing: every animation is inert |

`LOAD ANIMATION` fails on a missing file. A file the platform cannot decode,
including period AVI codecs unavailable on that system, loads as an
inert animation and logs a warning: it exists, measures 0 × 0 until placed and
never plays, so Classic programs keep running. Convert such files to a format
supported by the target system; MP4 (H.264) also needs a suitable decoder on Linux.

Each platform implements `VideoDecoder` (`src/engine/systems/video_decoder.hpp`)
in its own file, chosen by `src/engine/CMakeLists.txt`.

## Windows decoding

The Media Engine plays audio and supplies BGRA frames through a WIC bitmap. A
frame pump advances it independently of BASIC rendering; SDL_GPU keeps drawing
the decoded pictures. Windows N editions need the Media Feature Pack. Missing
media components and unsupported codecs follow the inert-animation behavior above.

## Linux dependencies

Linux builds require the system development packages for `gstreamer-1.0`,
`gstreamer-app-1.0` and `gstreamer-video-1.0`, version 1.16 or later, found through
pkg-config. For example:

```bash
# Fedora
sudo dnf install gstreamer1-devel gstreamer1-plugins-base-devel gstreamer1-plugins-good
# Debian / Ubuntu
sudo apt install libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev gstreamer1.0-plugins-good
```

OpenDBC and exported Linux runners dynamically link the system GStreamer libraries;
they bundle no GStreamer or codec binaries. Playback requires the Base plugins
(`playbin`, `appsink`, video conversion) and an audio output plugin, plus demuxers
and decoders for the file. Neither GTK nor Qt is required. The optional system
`libav` plugin may use FFmpeg internally; OpenDBC does not require it or link
FFmpeg directly. Legacy AVI support therefore also depends on installed plugins.

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
