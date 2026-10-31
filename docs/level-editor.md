# Scene view

The Studio's **Code | Scene** switch, in the middle of the toolbar (**Ctrl+1**,
**Ctrl+2**), opens a level editor with its own dock layout: Hierarchy,
Viewport, Inspector, Level Settings, Assets and Output. Every panel docks,
tabs and closes like the Code mode's; **View → Reset Layout** restores the
current mode's layout.

The mode needs an open, writable project whose main source is a `.dba` file.
Otherwise the switch is disabled and its tooltip says why.

The switch shows the active tab. A level's file opens that level, and so does
the main source of a project with levels; any other `.dba` opens as
[any program](#any-program). Without a `.dba` tab, or while a program runs, it
shows the current level, the first one, or a new `Level1`.

## Files

A level is BASIC, and nothing else is stored. Each level has its own folder:

| File | Holds |
| --- | --- |
| `levels/<name>/<name>.dba` | `FUNCTION LoadLevel_<name>()`: images, objects, matrices, Pro terrains, lights and 3D sounds |
| `levels/<name>/world_settings.dba` | `FUNCTION SetupWorld_<name>()`: display mode, Pro vsync, window, `SYNC ON`/`SYNC RATE`, `HIDE MOUSE`, the environment and the level camera |

**File → New Level**, or the same item in the **Scene** menu, asks for a name and
saves the new level at once; a name already taken opens that level.

Saving (**Ctrl+S** in the Scene mode, or **Scene → Save Level**) writes both
files, then completes the main source's header: each missing `#include` joins
the include block under the leading comment, and each missing call follows that
block, `SetupWorld_<name>()` first, then `LoadLevel_<name>()`. What the program
already imports, directly or through its other includes, is left alone, call
included. Once it imports a level, another level adds nothing: Output names the
functions to call where the game loads it, and **F5** still runs the imported
level. When the main source is open in a tab, the edit goes to that tab, undoable, and is
saved with it. **F5** in the Scene mode saves the level, then runs the main
source.

A project from before these folders moves its `levels/<name>.dba` into place when
the Scene mode opens, and the main source's `#include` follows. The next save moves
the level's environment to its world settings. A root `opendbc_game.dba` stays where
it is, and its `SetupGame()` still runs before them; delete both once its display
settings are in the level's.

The Studio reads a level back with the language frontend. It accepts only the
statements it writes, with literal arguments and `RGB()` of literal channels;
`REM @…` lines carry names, number ranges and which objects are painted. Any
other statement, in either file, makes the level read-only, and the Hierarchy and
Output list the offending lines. A level is written with the project's profile, and a Pro-only
command makes it read-only in a Classic project.

Each level starts its numbers at a configurable base per kind: objects and
images at 1000, matrices, terrains, lights and sounds at 1. New nodes take the lowest
free number from there, and images are numbered in order of first use, one per
file.

## Any program

The Code | Scene switch, or **Open in Scene View** in the Project panel's menu on a
`.dba` file, shows what any program builds, including a main source with levels
when the menu asks. The Studio runs it headless and silent with `--capture-scene`,
for 30 seconds at most. **Clicking Scene on such a tab therefore executes the
program**, like any run ([security](SECURITY.md)); the mode changes when the
capture ends, and an error leaves you in Code. Coming back to the program on show
runs nothing unless its text changed. **Setup frames** says how many frames build
the scene; what later frames set belongs to the game loop. **Refresh** runs it
again. An included file is seen through the program including it, with only the
nodes it creates.

The banner lists what is left out: limbs, meshes, particles, vertex data, BSP
worlds and `APPEND OBJECT`. Custom geometry shows as a box. Hidden and
screen-locked objects are dimmed in the Hierarchy and not drawn; a row's tooltip
gives its source line, and a double-click opens it.

Only transforms change. **Ctrl+S** writes them to the source:

| The axis | What happens |
| --- | --- |
| Set by one statement, run once during setup | Its argument is rewritten; Output names an expression a number replaced |
| Set by nothing | A statement follows the creation, on its own line, or after ` : ` when the line holds more |
| Anything else: a statement that runs repeatedly, the game loop, another command changing it, `FIX OBJECT PIVOT`, a light's roll | Locked, and the Inspector says why |

The edit goes to the file's tab, undoable, and stays there until that tab is
saved. The program then runs again, and Output reports any edit it does not
keep. A source that changed since the scene was read is refused, as is a number
that may differ when read again, like a function's result; **Refresh** then
drops the edits.

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
| Audio → 3D Sound | A sound file from the asset folder | `LOAD 3DSOUND`, `POSITION SOUND`, `SET SOUND VOLUME`, `PLAY SOUND` or `LOOP SOUND` |
| Camera | The level camera, on or off | see Level Settings |

Models and sounds from the menu land in front of the editor camera.

The Inspector edits the selected node:
- name, number, position, rotation (degrees, applied X, then Y, then Z) and scale (%).
  **Uniform scale** keeps the scale's proportions, in the Inspector and with the gizmo;
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
- a 3D sound's file, whether it plays once or loops when the level loads, and
  its volume. The program hears it from `POSITION LISTENER`, which stays at
  0, 0, 0 until the program moves it;
- texture, `COLOR OBJECT`, `GHOST OBJECT ON` and the `SET OBJECT` flags;
- in Pro, also diffuse, ambience, emissive, specular colour and power, and `SET SHADOW SHADING ON`.

Level Settings holds:
- ambient light and colour, backdrop colour and fog;
- light 0's visibility, rotation and colour;
- the Pro shadow source (`SET SHADOW POSITION`);
- the number bases;
- the level camera, off by default. When on, it writes `AUTOCAM OFF` then
  `POSITION`/`ROTATE CAMERA`, FOV and range. The viewport can look through it;
- the display: mode, window, title, sync and mouse.

A checked field writes a statement; an unchecked one leaves the engine default.
Only the number bases stay in the level file.

With several nodes selected, the Inspector shows the active node (the last one
selected) and copies each field you change to every selected node that has it:
position for all, rotation for objects and lights, scale and object flags for
objects, texture for all but lights and sounds, and shape fields when the nodes
share a kind other than matrix or terrain. Vectors copy by axis, so typing X
sets only X. Names and numbers stay per node.

## Viewport

| Input | Action |
| --- | --- |
| Right drag, with W A S D, Q E (Shift: faster) | Look around and fly |
| Middle drag | Pan |
| Alt + left drag | Orbit around the pivot |
| Wheel | Move toward or away from the pivot |
| Left click | Select (lights, sounds and the level camera by their icon) |
| Ctrl/Cmd or Shift + left click | Add to or remove from the selection; in the Hierarchy, Shift selects the rows from the active node |
| Ctrl/Cmd+A | Select every node |
| Q W E R | View, move, rotate, scale tools |
| F | Frame the selection |
| G | Align the selection to the grid |
| - and = | Smaller or larger grid step: 1, 2.5, 5, 10, 25... |
| Hold Ctrl/Cmd during a drag | Invert snapping |
| Hold V and drag | Move the selection by its vertex nearest the mouse onto another node's vertex |
| B, then 1 2 3 | The paint tool, then its One, Several and Brush modes |
| Shift + left click or drag, with the paint tool | Erase the painted copies within the radius |
| Ctrl+Z, Ctrl+Shift+Z or Ctrl+Y, Ctrl+D, Delete | Undo, redo, duplicate, delete |

Matrices, terrains and sounds only move, lights and the camera move and turn,
and objects also scale. **Local** turns the gizmo with the node. With several
nodes, the gizmo sits at their centre (turned like the active node in
**Local**) and moves, turns and scales them about it. A duplicate of `tree` or
`tree_2` is named `tree_N`, one past the highest `tree_` number in the level.

**Snap** is on by default. It moves world positions and scales to whole
multiples of their steps, on the axes the drag changes, and moves along local
axes or turns by steps from where the drag began. A group moves so that its
active node lands on the grid, and keeps its layout. Models and sounds dropped
in the scene land on the grid too. The number beside the box is the move step,
which is also a cell of the grid drawn around the camera's pivot; the arrow sets
the steps (10 units, 15 degrees and 10 % by default). They last until the Studio
closes. On macOS the key that inverts snapping is Cmd: ImGui turns Ctrl+click
into a right click. **Grid** shows or hides the grid.

A vertex drag reaches 40 pixels around the mouse, and the selection waits on its
last vertex in between. It works on the Move, Rotate and Scale tools.

Dropping an image on a node textures it. Dropping a
model or a sound places it on the ground under the cursor. In Assets, a
double-click applies the image to the selection, gives the sound to a selected
3D sound, or adds the model or sound in front of the camera.

The viewport is a preview drawn by the Studio, not by the engine. Its
primitives, matrices, terrains and models use the engine's geometry and
import, so sizes, origins and UVs match the program. Lighting is simple
per-pixel shading with ambient, up to eight lights, specular and linear fog.
Faces are drawn from both sides. Pro shadows use the engine's method: stencil
volumes extruded 2000 units from the shadow source (light 0 at the origin
unless `SET SHADOW POSITION` says otherwise), darkened by 25 %. A source on the
ground, such as light 0 or a light that does not exist, lays every volume along
the floor, which comes out as streaks and a grid of z-fighting in the Scene view
and the game alike. Ticking **Casts a shadow** in a level without a source
therefore sets a fixed one at (0, 100, 0). A model the import cannot read shows
as a 10-unit box.

## Paint

**Paint** (**B**) places copies of an object over the level, for trees or grass.
Entering the tool takes the selected object; the row it adds above the viewport
also lists the level's objects and the project's model files.

| Mode | What it does |
| --- | --- |
| One | A click places one object, on the grid while snapping |
| Several | A click scatters **Count** objects over the **Radius** |
| Brush | A drag fills the **Radius** up to the **Density**, in objects per 100 x 100 units |

Objects rest by their lowest point on what lies under them: any level geometry,
else the ground at Y = 0. Past the edge of a mesh, a scattered object is left out.
Painted objects are not surfaces themselves, and the brush keeps them apart, so
painting over an area does not pile them up. A stamp places at most 2000 objects.
**Random turn** spins each object about Y, and **Random scale** multiplies its
scale by a value in the range it shows, 75 % to 125 % by default. A click or a
drag is one undo step.

A painted object is a node like any other: the gizmo, the Inspector and undo work
on it. The Hierarchy folds objects painted alike into one row, whose click selects
them all and whose arrow lists them.

In Pro, a painted object that looks like an earlier one, with the same shape,
file, material and static state, is written as `INSTANCE OBJECT` of it, then its
own transform and animation. So editing one out of the group, or deleting the
original, only changes how the next save writes them. Classic has no such command,
so its copies are written in full. Either way they draw as instances of one
geometry, as [rendering](rendering.md) explains. A level file grows by about 100
bytes per object, and the Studio's preview, which draws them one by one, is meant
for thousands of them.
