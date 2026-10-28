# Scene view

The Studio's **Code | Scene** switch, in the middle of the toolbar (**Ctrl+1**,
**Ctrl+2**), opens a level editor with its own dock layout: Hierarchy,
Viewport, Inspector, Level Settings, Assets and Output. Every panel docks,
tabs and closes like the Code mode's; **View → Reset Layout** restores the
current mode's layout.

The mode needs an open, writable project whose main source is a `.dba` file.
Otherwise the switch is disabled and its tooltip says why.

## Files

A level is BASIC, and nothing else is stored:

| File | Holds |
| --- | --- |
| `levels/<name>.dba` | `FUNCTION LoadLevel_<name>()`: images, objects, matrices, Pro terrains, lights, the environment and the level camera |
| `opendbc_game.dba` | `FUNCTION SetupGame()`: display mode, Pro vsync, window, `SYNC ON`/`SYNC RATE`, `HIDE MOUSE`; shared by every level |

Saving (**Ctrl+S** in the Scene mode, or **Scene → Save Level**) writes both
files, then completes the main source's header: each missing `#include` joins
the include block under the leading comment, and each missing call follows that
block, `SetupGame()` first, then the levels in order. An include or call
already present anywhere is left where it is. When
the main source is open in a tab, the edit goes to that tab, undoable, and is
saved with it. **F5** in the Scene mode saves the level, then runs the main
source.

The Studio reads a level back with the language frontend. It accepts only the
statements it writes, with literal arguments and `RGB()` of literal channels;
`REM @…` lines carry names and number ranges. Any other statement makes the
level read-only, and the Hierarchy and Output list the offending lines. A
level is written with the project's profile, and a Pro-only command makes it
read-only in a Classic project.

Each level starts its numbers at a configurable base per kind: objects and
images at 1000, matrices, terrains and lights at 1. New nodes take the lowest
free number from there, and images are numbered in order of first use, one per
file.

## Content

The **Create** menu, shown in the Scene mode's menu bar and on a right-click in
the Hierarchy, groups what a level can hold by type; items the profile does
not implement are disabled:

| Menu | Kind | Commands |
| --- | --- | --- |
| 3D Object | Cube, box, sphere, cylinder, cone, plain, triangle | `MAKE OBJECT …` |
| 3D Object → Model | A model file from the asset folder | `LOAD OBJECT` |
| Landscape | Matrix | `MAKE MATRIX`, `PREPARE MATRIX TEXTURE`, `FILL MATRIX`, `SET MATRIX HEIGHT`, `UPDATE MATRIX`, `POSITION MATRIX` |
| Landscape | Terrain (Pro) | `MAKE TERRAIN`, `TEXTURE TERRAIN`, `POSITION TERRAIN` |
| Light | Point, spot or directional light | `MAKE LIGHT`, `SET … LIGHT`, `POSITION`/`ROTATE LIGHT`, `SET LIGHT RANGE`, `COLOR LIGHT` |
| Camera | The level camera, on or off | see Level Settings |

Models from the menu land in front of the editor camera.

The Inspector edits the selected node:
- name, number, position, rotation (degrees, applied X, then Y, then Z) and scale (%);
- in Classic, **Make Static Object**: `MAKE STATIC OBJECT` then `DELETE OBJECT`,
  so the object becomes fixed scenery for the static collision commands and its
  number is free at run time. Pro does not implement the command;
- shape;
- a matrix's heights: **Fill** sets every height and the tile (`FILL MATRIX`),
  **Randomize** draws whole heights below a range (`RANDOMIZE MATRIX`). The
  result is baked into the level as `FILL MATRIX` with the most common height,
  one `SET MATRIX HEIGHT` per other vertex and `UPDATE MATRIX`, so every run
  shows the authored matrix. Changing the tile count leaves the old heights
  aside until **Flatten**;
- a terrain's heightmap: **Fill** and **Randomize** (rolling hills below a
  range) write a square grey PNG, 64 to 512 pixels, to
  `heightmaps/<level>_terrain<number>.png` under the asset root and point the
  terrain at it. Grey levels 0–255 are heights 0–100; a heightmap picked by
  hand is never overwritten;
- a model's animation: none, or a range of the frames of the file's first
  animation (the only one `LOAD OBJECT` keeps, at 25 frames a second), played
  once (`PLAY OBJECT`) or looped (`LOOP OBJECT`), at an optional
  `SET OBJECT SPEED`. The viewport shows the model still;
- texture, `COLOR OBJECT`, `GHOST OBJECT ON` and the `SET OBJECT` flags;
- in Pro, also diffuse, ambience, emissive, specular colour and power, and `SET SHADOW SHADING ON`.

Level Settings holds:
- ambient light and colour, backdrop colour and fog;
- light 0's visibility, rotation and colour;
- the Pro shadow source (`SET SHADOW POSITION`);
- the number bases;
- the level camera, off by default. When on, it writes `AUTOCAM OFF` first and
  `POSITION`/`ROTATE CAMERA`, FOV and range last. The viewport can look through it.

A checked field writes a statement; an unchecked one leaves the engine default.

## Viewport

| Input | Action |
| --- | --- |
| Right drag, with W A S D, Q E (Shift: faster) | Look around and fly |
| Middle drag | Pan |
| Alt + left drag | Orbit around the pivot |
| Wheel | Move toward or away from the pivot |
| Left click | Select (lights and the level camera by their icon) |
| Q W E R | View, move, rotate, scale tools |
| F | Frame the selection |
| Ctrl+Z, Ctrl+Shift+Z or Ctrl+Y, Ctrl+D, Delete | Undo, redo, duplicate, delete |

Matrices and terrains only move, lights and the camera move and turn, and
objects also scale. Dropping an image on a node textures it. Dropping a model
places it on the ground under the cursor. In Assets, a double-click applies
the image to the selection, or adds the model in front of the camera.

The viewport is a preview drawn by the Studio, not by the engine. Its
primitives, matrices, terrains and models use the engine's geometry and
import, so sizes, origins and UVs match the program. Lighting is simple
per-pixel shading with ambient, up to eight lights, specular and linear fog.
Faces are drawn from both sides, and shadows are not drawn. A model the import
cannot read shows as a 10-unit box.
