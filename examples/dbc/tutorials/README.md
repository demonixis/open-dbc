# DarkBASIC Classic tutorials

Short programs written for OpenDBC, one topic each. Every program runs on its own without input
and switches to the keyboard or mouse as soon as you use them, so you can watch it first.

```bash
opendbc run examples/dbc/tutorials/2d/night_town.dba --dialect dbc
```

Pictures in `bitmap/` and `sprite/` are rendered by `assets-src/blender/classic_tutorials.py` and saved
as BMP, the format DarkBASIC Classic loads. The bobs draw their own images in BASIC.
`scripts/check_example_frames.py` (CTest `example_frames`) runs each tutorial headless and checks its last frame.

## 2D drawing

| Program | What it shows | Commands of this family |
| --- | --- | --- |
| [`colour_picker.dba`](2d/colour_picker.dba) | A palette read back with `POINT` and split into its components. | `BOX`, `INK`, `POINT`, `RGB`, `RGBB`, `RGBG`, `RGBR` |
| [`night_town.dba`](2d/night_town.dba) | A town at night built from lines, dots, circles, ellipses and boxes. | `BOX`, `CIRCLE`, `CLS`, `DOT`, `ELLIPSE`, `INK`, `LINE`, `RGB` |
| [`spirograph.dba`](2d/spirograph.dba) | Rolling-circle curves drawn with coloured line segments. | `CLS`, `INK`, `LINE`, `RGB` |

## Bitmaps

| Program | What it shows | Commands of this family |
| --- | --- | --- |
| [`parallax.dba`](bitmap/parallax.dba) | Two off-screen panoramas scrolled at different speeds with `COPY BITMAP`. | `BITMAP DEPTH`, `BITMAP EXIST`, `BITMAP HEIGHT`, `BITMAP WIDTH`, `COPY BITMAP`, `CREATE BITMAP`, `CURRENT BITMAP`, `DELETE BITMAP`, `SET CURRENT BITMAP` |
| [`picture_effects.dba`](bitmap/picture_effects.dba) | Mirror, flip, blur and fade applied to a loaded picture. | `BITMAP FLIPPED`, `BITMAP MIRRORED`, `BLUR BITMAP`, `COPY BITMAP`, `FADE BITMAP`, `FLIP BITMAP`, `LOAD BITMAP`, `MIRROR BITMAP`, `SET CURRENT BITMAP` |
| [`postcards.dba`](bitmap/postcards.dba) | A picture shrunk, saved, reloaded and dealt out as postcards. | `BITMAP HEIGHT`, `BITMAP WIDTH`, `COPY BITMAP`, `CREATE BITMAP`, `DELETE BITMAP`, `LOAD BITMAP`, `SAVE BITMAP`, `SET CURRENT BITMAP` |

## Bobs

| Program | What it shows | Commands of this family |
| --- | --- | --- |
| [`aquarium.dba`](bobs/aquarium.dba) | Fish drawn in BASIC, grabbed as images and swimming as bobs. | `BOB`, `BOB EXIST`, `BOB HEIGHT`, `BOB IMAGE`, `BOB MIRRORED`, `BOB WIDTH`, `BOB X`, `BOB Y`, `MIRROR BOB`, `SET BOB` |
| [`jelly_lab.dba`](bobs/jelly_lab.dba) | Bob handles, scaling, stretching, flipping, stamps and trails. | `BOB`, `BOB FLIPPED`, `BOB OFFSET X`, `BOB OFFSET Y`, `BOB SCALE X`, `BOB SCALE Y`, `DELETE BOB`, `FLIP BOB`, `OFFSET BOB`, `PASTE BOB`, `SCALE BOB`, `SET BOB`, `STRETCH BOB` |
| [`star_catch.dba`](bobs/star_catch.dba) | A catching game built on bob collisions. | `BOB`, `BOB COLLISION`, `BOB EXIST`, `BOB HIT`, `BOB X`, `DELETE BOB` |

## Sprites and images

| Program | What it shows | Commands of this family |
| --- | --- | --- |
| [`buoy_field.dba`](sprite/buoy_field.dba) | Sprites placed, hidden, shown, queried and deleted. | `DELETE IMAGE`, `DELETE SPRITE`, `HIDE ALL SPRITES`, `HIDE SPRITE`, `IMAGE EXIST`, `LOAD IMAGE`, `SHOW ALL SPRITES`, `SHOW SPRITE`, `SPRITE`, `SPRITE EXIST`, `SPRITE HEIGHT`, `SPRITE IMAGE`, `SPRITE WIDTH`, `SPRITE X`, `SPRITE Y` |
| [`rock_dodge.dba`](sprite/rock_dodge.dba) | A dodging game with a scrolling background and sprite collisions. | `LOAD IMAGE`, `SET SPRITE`, `SPRITE`, `SPRITE COLLISION`, `SPRITE HIT` |
| [`sprite_lab.dba`](sprite/sprite_lab.dba) | Sprite handles, scaling, stretching, sizing, mirroring and pasting. | `FLIP SPRITE`, `LOAD IMAGE`, `MIRROR SPRITE`, `OFFSET SPRITE`, `PASTE IMAGE`, `PASTE SPRITE`, `SCALE SPRITE`, `SIZE SPRITE`, `SPRITE`, `SPRITE FLIPPED`, `SPRITE HEIGHT`, `SPRITE MIRRORED`, `SPRITE OFFSET X`, `SPRITE OFFSET Y`, `SPRITE SCALE X`, `SPRITE SCALE Y`, `SPRITE WIDTH`, `STRETCH SPRITE` |

## Text and strings

| Program | What it shows | Commands of this family |
| --- | --- | --- |
| [`secret_messages.dba`](text/secret_messages.dba) | A Caesar cipher and the string functions that take text apart. | `ASC`, `BIN$`, `CHR$`, `HEX$`, `LEFT$`, `LEN`, `LOWER$`, `MID$`, `PRINT`, `RIGHT$`, `SET CURSOR`, `SET TEXT FONT`, `STR$`, `TEXT`, `UPPER$`, `VAL` |
| [`type_specimen.dba`](text/type_specimen.dba) | Fonts, styles, sizes and opaque text on a specimen sheet. | `CENTER TEXT`, `PERFORM CHECKLIST FOR FONTS`, `SET TEXT FONT`, `SET TEXT OPAQUE`, `SET TEXT SIZE`, `SET TEXT TO BOLD`, `SET TEXT TO BOLDITALIC`, `SET TEXT TO ITALIC`, `SET TEXT TO NORMAL`, `SET TEXT TRANSPARENT`, `STR$`, `TEXT`, `TEXT BACKGROUND TYPE`, `TEXT FONT$`, `TEXT HEIGHT`, `TEXT SIZE`, `TEXT STYLE`, `TEXT WIDTH` |
| [`typewriter.dba`](text/typewriter.dba) | A story typed letter by letter with word wrapping, and a news ticker. | `CENTER TEXT`, `LEN`, `MID$`, `SET TEXT FONT`, `SET TEXT SIZE`, `TEXT`, `TEXT HEIGHT`, `TEXT WIDTH` |

## The language

| Program | What it shows | Commands of this family |
| --- | --- | --- |
| [`high_scores.dba`](language/high_scores.dba) | A score table with #INCLUDE, SAVE ARRAY, LOAD ARRAY, UNDIM and CL$. | `CL$`, `DATA`, `GET DATE$`, `GET TIME$`, `LOAD ARRAY`, `READ`, `SAVE ARRAY`, `SYNC`, `SYNC RATE`, `TIMER`, `UNDIM` |
| [`maze_walker.dba`](language/maze_walker.dba) | DATA, arrays, functions, SELECT, every loop, GOSUB and GOTO solving a maze. | `DATA`, `READ`, `RESTORE`, `SLEEP`, `SYNC`, `SYNC RATE` |
| [`score_helpers.dba`](language/score_helpers.dba) | The functions high_scores.dba includes; it shows nothing on its own. | statements only |
| [`station_clock.dba`](language/station_clock.dba) | TIMER, the clock, and every way a program waits. | `GET DATE$`, `GET TIME$`, `SLEEP`, `SUSPEND FOR KEY`, `SUSPEND FOR MOUSE`, `SYNC`, `SYNC RATE`, `TIMER`, `WAIT`, `WAIT KEY` |

## Maths

| Program | What it shows | Commands of this family |
| --- | --- | --- |
| [`cannon.dba`](maths/cannon.dba) | Ballistics: ASIN aims, SQRT measures, EXP fades the smoke. | `ABS`, `ASIN`, `ATANFULL`, `COS`, `EXP`, `INT`, `RANDOMIZE`, `RND`, `SIN`, `SQRT` |
| [`random_garden.dba`](maths/random_garden.dba) | RANDOMIZE and RND growing the same garden from the same seed. | `COS`, `INT`, `RANDOMIZE`, `RND`, `SIN`, `SQRT` |
| [`wave_lab.dba`](maths/wave_lab.dba) | Every trig, hyperbolic and inverse function plotted side by side. | `ABS`, `ACOS`, `ASIN`, `ATAN`, `ATANFULL`, `COS`, `EXP`, `HCOS`, `HSIN`, `HTAN`, `INT`, `SIN`, `TAN` |

## Sound

| Program | What it shows | Commands of this family |
| --- | --- | --- |
| [`bee_garden.dba`](sound/bee_garden.dba) | 3D sound: buzzing bees around a listener you can walk and turn. | `LISTENER ANGLE X`, `LISTENER ANGLE Y`, `LISTENER ANGLE Z`, `LISTENER POSITION X`, `LISTENER POSITION Y`, `LISTENER POSITION Z`, `LOAD 3DSOUND`, `LOOP SOUND`, `POSITION LISTENER`, `POSITION SOUND`, `ROTATE LISTENER`, `SET SOUND SPEED`, `SOUND POSITION X`, `SOUND POSITION Y`, `SOUND POSITION Z`, `SOUND TYPE` |
| [`recorder.dba`](sound/recorder.dba) | Recording from the microphone, playback and SAVE SOUND. | `DELETE SOUND`, `LOAD SOUND`, `PLAY SOUND`, `RECORD SOUND`, `SAVE SOUND`, `SET EAX`, `SOUND EXIST`, `SOUND PLAYING`, `STOP RECORDING SOUND` |
| [`sound_board.dba`](sound/sound_board.dba) | Six pads, a drum pattern and a looping synth line with volume, pan and speed. | `CLONE SOUND`, `DELETE SOUND`, `GET SOUND PAN`, `GET SOUND SPEED`, `GET SOUND VOLUME`, `LOAD SOUND`, `LOOP SOUND`, `PAUSE SOUND`, `PLAY SOUND`, `RESUME SOUND`, `SET SOUND PAN`, `SET SOUND SPEED`, `SET SOUND VOLUME`, `SOUND EXIST`, `SOUND LOOPING`, `SOUND PAUSED`, `SOUND PLAYING`, `SOUND TYPE`, `STOP SOUND` |

## Music

| Program | What it shows | Commands of this family |
| --- | --- | --- |
| [`jukebox.dba`](music/jukebox.dba) | Two tunes played, looped, paused and deleted with the music commands. | `DELETE MUSIC`, `LOAD MUSIC`, `LOOP MUSIC`, `MUSIC EXIST`, `MUSIC LOOPING`, `MUSIC PAUSED`, `MUSIC PLAYING`, `PAUSE MUSIC`, `PLAY MUSIC`, `RESUME MUSIC`, `STOP MUSIC` |

## Keyboard, mouse and controllers

| Program | What it shows | Commands of this family |
| --- | --- | --- |
| [`joystick_lab.dba`](input/joystick_lab.dba) | Every axis, button, slider and hat of a game controller, and force feedback. | `CONTROL DEVICE NAME$`, `CONTROL DEVICE X`, `CONTROL DEVICE Y`, `CONTROL DEVICE Z`, `FORCE ANGLE`, `FORCE AUTO CENTER OFF`, `FORCE AUTO CENTER ON`, `FORCE CHAINSAW`, `FORCE DOWN`, `FORCE IMPACT`, `FORCE LEFT`, `FORCE NO EFFECT`, `FORCE RIGHT`, `FORCE SHOOT`, `FORCE UP`, `FORCE WATER EFFECT`, `JOYSTICK DOWN`, `JOYSTICK FIRE A`, `JOYSTICK FIRE B`, `JOYSTICK FIRE C`, `JOYSTICK FIRE D`, `JOYSTICK FIRE X`, `JOYSTICK HAT ANGLE`, `JOYSTICK LEFT`, `JOYSTICK RIGHT`, `JOYSTICK SLIDER A`, `JOYSTICK SLIDER B`, `JOYSTICK SLIDER C`, `JOYSTICK SLIDER D`, `JOYSTICK TWIST X`, `JOYSTICK TWIST Y`, `JOYSTICK TWIST Z`, `JOYSTICK UP`, `JOYSTICK X`, `JOYSTICK Y`, `JOYSTICK Z`, `PERFORM CHECKLIST FOR CONTROL DEVICES`, `SET CONTROL DEVICE` |
| [`keyboard_lab.dba`](input/keyboard_lab.dba) | KEYSTATE, the named keys, INKEY$, SCANCODE, ENTRY$ and INPUT on an on-screen keyboard. | `CLEAR ENTRY BUFFER`, `CONTROLKEY`, `DOWNKEY`, `ENTRY$`, `ESCAPEKEY`, `INKEY$`, `INPUT`, `KEYSTATE`, `LEFTKEY`, `RETURNKEY`, `RIGHTKEY`, `SCANCODE`, `SHIFTKEY`, `SPACEKEY`, `UPKEY` |
| [`mouse_lab.dba`](input/mouse_lab.dba) | A paint program with the mouse buttons, movement speed and a drawn pointer. | `HIDE MOUSE`, `INKEY$`, `MOUSECLICK`, `MOUSEMOVEX`, `MOUSEMOVEY`, `MOUSEX`, `MOUSEY`, `POSITION MOUSE`, `SHOW MOUSE` |

## Files and folders

| Program | What it shows | Commands of this family |
| --- | --- | --- |
| [`explorer.dba`](files/explorer.dba) | DIR, DRIVELIST, file checklists, FIND FIRST/NEXT, CD and EXECUTE FILE. | `CD`, `DIR`, `DOWNKEY`, `DRIVELIST`, `EXECUTE FILE`, `FILE SIZE`, `FIND FIRST`, `FIND NEXT`, `GET DIR$`, `GET FILE DATE$`, `GET FILE NAME$`, `GET FILE TYPE`, `INKEY$`, `PATH EXIST`, `PERFORM CHECKLIST FOR DRIVES`, `PERFORM CHECKLIST FOR FILES`, `UPKEY` |
| [`notebook.dba`](files/notebook.dba) | Writing and reading typed values, then copying, renaming, moving and deleting files. | `CLOSE FILE`, `COPY FILE`, `DELETE DIRECTORY`, `DELETE FILE`, `FILE END`, `FILE EXIST`, `FILE OPEN`, `FILE SIZE`, `GET DIR$`, `MAKE DIRECTORY`, `MAKE FILE`, `MOVE FILE`, `OPEN TO READ`, `OPEN TO WRITE`, `PATH EXIST`, `READ BYTE`, `READ FILE`, `READ FLOAT`, `READ LONG`, `READ STRING`, `READ WORD`, `RENAME FILE`, `SET DIR`, `WRITE BYTE`, `WRITE FILE`, `WRITE FLOAT`, `WRITE LONG`, `WRITE STRING`, `WRITE WORD` |
| [`packer.dba`](files/packer.dba) | File, folder and memblock blocks packed into one archive, a hex view and the clipboard. | `CLOSE FILE`, `DELETE DIRECTORY`, `DELETE FILE`, `FILE EXIST`, `FILE SIZE`, `GET CLIPBOARD$`, `INKEY$`, `MAKE DIRECTORY`, `MAKE MEMBLOCK FROM FILE`, `OPEN TO READ`, `OPEN TO WRITE`, `PATH EXIST`, `READ DIRBLOCK`, `READ FILEBLOCK`, `READ MEMBLOCK`, `WRITE BYTE`, `WRITE DIRBLOCK`, `WRITE FILEBLOCK`, `WRITE MEMBLOCK`, `WRITE STRING`, `WRITE TO CLIPBOARD` |

## Memblocks

| Program | What it shows | Commands of this family |
| --- | --- | --- |
| [`plasma.dba`](memblock/plasma.dba) | A plasma painted pixel by pixel in an image memblock, and every memblock reader. | `COPY MEMBLOCK`, `DELETE MEMBLOCK`, `GET MEMBLOCK SIZE`, `MAKE BITMAP FROM MEMBLOCK`, `MAKE IMAGE FROM MEMBLOCK`, `MAKE MEMBLOCK`, `MAKE MEMBLOCK FROM BITMAP`, `MAKE MEMBLOCK FROM IMAGE`, `MEMBLOCK BYTE`, `MEMBLOCK DWORD`, `MEMBLOCK EXIST`, `MEMBLOCK FLOAT`, `MEMBLOCK WORD`, `WRITE MEMBLOCK BYTE`, `WRITE MEMBLOCK DWORD`, `WRITE MEMBLOCK FLOAT`, `WRITE MEMBLOCK WORD` |
| [`tone_maker.dba`](memblock/tone_maker.dba) | Sounds built sample by sample in a memblock and drawn from MAKE MEMBLOCK FROM SOUND. | `DELETE MEMBLOCK`, `GET BACKBUFFER DEPTH`, `GET BACKBUFFER HEIGHT`, `GET BACKBUFFER PITCH`, `GET BACKBUFFER WIDTH`, `MAKE MEMBLOCK`, `MAKE MEMBLOCK FROM SOUND`, `MAKE SOUND FROM MEMBLOCK`, `MEMBLOCK DWORD`, `MEMBLOCK EXIST`, `MEMBLOCK WORD`, `WRITE MEMBLOCK DWORD`, `WRITE MEMBLOCK WORD` |

## The window

| Program | What it shows | Commands of this family |
| --- | --- | --- |
| [`window_lab.dba`](system/window_lab.dba) | Window mode, title, size, position and layout, always active and the exit prompt. | `ALWAYS ACTIVE OFF`, `ALWAYS ACTIVE ON`, `DISABLE ESCAPEKEY`, `ENABLE ESCAPEKEY`, `EXIT PROMPT`, `HIDE WINDOW`, `MAXIMIZE WINDOW`, `MINIMIZE WINDOW`, `RESTORE WINDOW`, `SET WINDOW LAYOUT`, `SET WINDOW OFF`, `SET WINDOW ON`, `SET WINDOW POSITION`, `SET WINDOW SIZE`, `SET WINDOW TITLE`, `SHOW WINDOW` |

## The display

| Program | What it shows | Commands of this family |
| --- | --- | --- |
| [`display_lab.dba`](screen/display_lab.dba) | Display modes, screen properties, graphics card, video memory and gamma. | `CHECK DISPLAY MODE`, `PERFORM CHECKLIST FOR DISPLAY MODES`, `SCREEN DEPTH`, `SCREEN FPS`, `SCREEN HEIGHT`, `SCREEN TYPE`, `SCREEN WIDTH`, `SET DISPLAY MODE`, `SET GAMMA` |

## 3D

| Program | What it shows | Commands of this family |
| --- | --- | --- |
| [`animation.dba`](3d/animation.dba) | Appended animation clips with speed and interpolation, and keyframes saved and loaded. | `APPEND OBJECT`, `APPEND OBJECT ANIMATION`, `AUTOCAM OFF`, `CLEAR ALL OBJECT KEYFRAMES`, `CLEAR OBJECT KEYFRAME`, `COLOR BACKDROP`, `LOAD OBJECT`, `LOOP OBJECT`, `OBJECT FRAME`, `OBJECT INTERPOLATION`, `OBJECT LOOPING`, `OBJECT PLAYING`, `OBJECT SPEED`, `PLAY OBJECT`, `POINT CAMERA`, `POSITION CAMERA`, `POSITION OBJECT`, `ROTATE LIMB`, `SAVE OBJECT ANIMATION`, `SCALE OBJECT`, `SET AMBIENT LIGHT`, `SET OBJECT FRAME`, `SET OBJECT INTERPOLATION`, `SET OBJECT KEYFRAME`, `SET OBJECT SPEED`, `STOP OBJECT`, `TOTAL OBJECT FRAMES`, `YROTATE OBJECT` |
| [`cameras.dba`](3d/cameras.dba) | Five ways to drive the camera, a letterboxed view and a zooming field of view. | `AUTOCAM OFF`, `AUTOCAM ON`, `CAMERA ANGLE X`, `CAMERA ANGLE Y`, `CAMERA ANGLE Z`, `CAMERA POSITION X`, `CAMERA POSITION Y`, `CAMERA POSITION Z`, `CLEAR CAMERA VIEW`, `COLOR OBJECT`, `MAKE OBJECT BOX`, `MAKE OBJECT CONE`, `MAKE OBJECT PLAIN`, `MAKE OBJECT SPHERE`, `MOVE CAMERA`, `PITCH CAMERA DOWN`, `PITCH CAMERA UP`, `POINT CAMERA`, `POSITION CAMERA`, `POSITION OBJECT`, `ROLL CAMERA LEFT`, `ROLL CAMERA RIGHT`, `ROTATE CAMERA`, `SET CAMERA FOV`, `SET CAMERA RANGE`, `SET CAMERA ROTATION XYZ`, `SET CAMERA ROTATION ZYX`, `SET CAMERA TO FOLLOW`, `SET CAMERA TO OBJECT ORIENTATION`, `SET CAMERA VIEW`, `TEXTURE OBJECT`, `TURN CAMERA LEFT`, `TURN CAMERA RIGHT`, `WRAPVALUE`, `XROTATE CAMERA`, `XROTATE OBJECT`, `YROTATE CAMERA`, `YROTATE OBJECT`, `ZROTATE CAMERA` |
| [`collisions.dba`](3d/collisions.dba) | A ball among box, sphere, polygon and collision-box obstacles. | `AUTOCAM OFF`, `COLOR BACKDROP`, `COLOR OBJECT`, `DELETE OBJECT COLLISION BOX`, `GET OBJECT COLLISION X`, `GET OBJECT COLLISION Y`, `GET OBJECT COLLISION Z`, `MAKE OBJECT BOX`, `MAKE OBJECT COLLISION BOX`, `MAKE OBJECT CONE`, `MAKE OBJECT CUBE`, `MAKE OBJECT SPHERE`, `OBJECT COLLISION`, `OBJECT HIT`, `OBJECT IN SCREEN`, `OBJECT SCREEN X`, `OBJECT SCREEN Y`, `POINT CAMERA`, `POSITION CAMERA`, `POSITION OBJECT`, `SET GLOBAL COLLISION OFF`, `SET GLOBAL COLLISION ON`, `SET OBJECT COLLISION OFF`, `SET OBJECT COLLISION ON`, `SET OBJECT COLLISION TO BOXES`, `SET OBJECT COLLISION TO POLYGONS`, `SET OBJECT COLLISION TO SPHERES` |
| [`flight.dba`](3d/flight.dba) | A glider flown with MOVE, TURN, PITCH and ROLL, a chaser, a shadow and a trailing camera. | `AUTOCAM OFF`, `BACKDROP ON`, `COLOR BACKDROP`, `COLOR OBJECT`, `CURVEANGLE`, `CURVEVALUE`, `FIX OBJECT PIVOT`, `LOCK OBJECT OFF`, `LOCK OBJECT ON`, `MAKE OBJECT CONE`, `MAKE OBJECT PLAIN`, `MAKE OBJECT SPHERE`, `MOVE OBJECT`, `NEWXVALUE`, `NEWYVALUE`, `NEWZVALUE`, `OBJECT ANGLE X`, `OBJECT ANGLE Y`, `OBJECT ANGLE Z`, `OBJECT IN SCREEN`, `OBJECT POSITION X`, `OBJECT POSITION Y`, `OBJECT POSITION Z`, `OBJECT SCREEN X`, `OBJECT SCREEN Y`, `PITCH OBJECT DOWN`, `PITCH OBJECT UP`, `POINT CAMERA`, `POINT OBJECT`, `POSITION CAMERA`, `POSITION OBJECT`, `ROLL OBJECT LEFT`, `ROLL OBJECT RIGHT`, `SCALE OBJECT`, `SCALE OBJECT TEXTURE`, `SET CAMERA RANGE`, `SET OBJECT ROTATION XYZ`, `SET OBJECT ROTATION ZYX`, `SET OBJECT TO CAMERA ORIENTATION`, `SET OBJECT TO OBJECT ORIENTATION`, `TEXTURE OBJECT`, `TURN OBJECT LEFT`, `TURN OBJECT RIGHT`, `WRAPVALUE`, `XROTATE OBJECT`, `ZROTATE OBJECT` |
| [`landscape.dba`](3d/landscape.dba) | An endless matrix landscape shifted row by row, with a ghosted rippling lake. | `AUTOCAM OFF`, `COLOR BACKDROP`, `COLOR OBJECT`, `DELETE MATRIX`, `FILL MATRIX`, `FOG AVAILABLE`, `FOG COLOR`, `FOG DISTANCE`, `FOG ON`, `GET GROUND HEIGHT`, `GET MATRIX HEIGHT`, `GHOST MATRIX OFF`, `GHOST MATRIX ON`, `MAKE MATRIX`, `MAKE OBJECT BOX`, `MATRIX EXIST`, `MATRIX POSITION X`, `MATRIX POSITION Y`, `MATRIX POSITION Z`, `MATRIX TILE COUNT`, `MATRIX TILES EXIST`, `MATRIX WIREFRAME STATE`, `POINT CAMERA`, `POSITION CAMERA`, `POSITION MATRIX`, `POSITION OBJECT`, `PREPARE MATRIX TEXTURE`, `RANDOMIZE MATRIX`, `SET CAMERA RANGE`, `SET MATRIX`, `SET MATRIX HEIGHT`, `SET MATRIX NORMAL`, `SET MATRIX TEXTURE`, `SET MATRIX TILE`, `SET MATRIX WIREFRAME OFF`, `SET MATRIX WIREFRAME ON`, `SHIFT MATRIX DOWN`, `SHIFT MATRIX LEFT`, `SHIFT MATRIX RIGHT`, `SHIFT MATRIX UP`, `UPDATE MATRIX` |
| [`lights.dba`](3d/lights.dba) | Point, spot and directional lights on a gallery of spheres, with the ambient light. | `AUTOCAM OFF`, `COLOR AMBIENT LIGHT`, `COLOR BACKDROP`, `COLOR LIGHT`, `COLOR OBJECT`, `DELETE LIGHT`, `HIDE LIGHT`, `LIGHT DIRECTION X`, `LIGHT DIRECTION Y`, `LIGHT DIRECTION Z`, `LIGHT EXIST`, `LIGHT POSITION X`, `LIGHT POSITION Y`, `LIGHT POSITION Z`, `LIGHT RANGE`, `LIGHT TYPE`, `LIGHT VISIBLE`, `MAKE LIGHT`, `MAKE OBJECT BOX`, `MAKE OBJECT CONE`, `MAKE OBJECT SPHERE`, `POINT CAMERA`, `POINT LIGHT`, `POINT OBJECT`, `POSITION CAMERA`, `POSITION LIGHT`, `POSITION OBJECT`, `ROTATE LIGHT`, `SCALE OBJECT TEXTURE`, `SET AMBIENT LIGHT`, `SET DIRECTIONAL LIGHT`, `SET LIGHT RANGE`, `SET LIGHT TO OBJECT ORIENTATION`, `SET LIGHT TO OBJECT POSITION`, `SET OBJECT`, `SET POINT LIGHT`, `SET SPOT LIGHT`, `SHOW LIGHT`, `TEXTURE OBJECT`, `WRAPVALUE` |
| [`limbs.dba`](3d/limbs.dba) | A rider glued to a scooter: limbs moved, turned, scaled, coloured, textured and hidden. | `AUTOCAM OFF`, `COLOR BACKDROP`, `COLOR LIMB`, `GLUE OBJECT TO LIMB`, `HIDE LIMB`, `LIMB ANGLE X`, `LIMB ANGLE Y`, `LIMB ANGLE Z`, `LIMB DIRECTION X`, `LIMB DIRECTION Y`, `LIMB DIRECTION Z`, `LIMB EXIST`, `LIMB OFFSET X`, `LIMB OFFSET Y`, `LIMB OFFSET Z`, `LIMB POSITION X`, `LIMB POSITION Y`, `LIMB POSITION Z`, `LIMB TEXTURE`, `LIMB TEXTURE NAME$`, `LIMB VISIBLE`, `LOAD OBJECT`, `OFFSET LIMB`, `PERFORM CHECKLIST FOR OBJECT LIMBS`, `POINT CAMERA`, `POSITION CAMERA`, `POSITION OBJECT`, `ROTATE LIMB`, `SCALE LIMB`, `SCALE LIMB TEXTURE`, `SCALE OBJECT`, `SCROLL LIMB TEXTURE`, `SET AMBIENT LIGHT`, `SHOW LIMB`, `TEXTURE LIMB`, `UNGLUE OBJECT`, `WRAPVALUE`, `YROTATE OBJECT` |
| [`materials.dba`](3d/materials.dba) | Textures, wireframe, transparency, ghosting, fading, lighting and depth on eight cubes. | `3DBLIT AVAILABLE`, `ALPHABLENDING AVAILABLE`, `AUTOCAM OFF`, `COLOR BACKDROP`, `COLOR OBJECT`, `DISABLE OBJECT ZDEPTH`, `ENABLE OBJECT ZDEPTH`, `FADE OBJECT`, `FILTERING AVAILABLE`, `GHOST OBJECT OFF`, `GHOST OBJECT ON`, `MAKE OBJECT BOX`, `MAKE OBJECT CUBE`, `OBJECT IN SCREEN`, `OBJECT SCREEN X`, `OBJECT SCREEN Y`, `POINT CAMERA`, `POSITION CAMERA`, `POSITION OBJECT`, `SCALE OBJECT TEXTURE`, `SCROLL OBJECT TEXTURE`, `SET AMBIENT LIGHT`, `SET MIPMAP MODE`, `SET NORMALIZATION ON`, `SET OBJECT`, `SET OBJECT TEXTURE`, `TEXTURE OBJECT`, `WRAPVALUE`, `XROTATE OBJECT`, `YROTATE OBJECT` |
| [`mesh_builder.dba`](3d/mesh_builder.dba) | Meshes from shapes and files, limbs stacked and linked, and a blob deformed through a memblock. | `ADD LIMB`, `AUTOCAM OFF`, `CHANGE MESH`, `COLOR BACKDROP`, `COLOR OBJECT`, `DELETE MESH`, `DELETE OBJECT`, `LINK LIMB`, `LOAD MESH`, `MAKE MESH FROM OBJECT`, `MAKE OBJECT`, `MAKE OBJECT CUBE`, `MAKE OBJECT CYLINDER`, `MAKE OBJECT SPHERE`, `MESH EXIST`, `OFFSET LIMB`, `POINT CAMERA`, `POSITION CAMERA`, `POSITION OBJECT`, `ROTATE LIMB`, `YROTATE OBJECT` |
| [`scenery.dba`](3d/scenery.dba) | A forest with a scrolling sky backdrop, fog and a day-night ambient light cycle. | `AUTOCAM OFF`, `BACKDROP OFF`, `BACKDROP ON`, `COLOR BACKDROP`, `COLOR OBJECT`, `DRAW TO BACK`, `DRAW TO FRONT`, `FOG AVAILABLE`, `FOG COLOR`, `FOG DISTANCE`, `FOG OFF`, `FOG ON`, `MAKE OBJECT CONE`, `MAKE OBJECT CYLINDER`, `MAKE OBJECT PLAIN`, `POINT CAMERA`, `POSITION CAMERA`, `POSITION OBJECT`, `SCALE OBJECT`, `SCROLL BACKDROP`, `SET AMBIENT LIGHT`, `SET CAMERA RANGE`, `SET NORMALIZATION OFF`, `TEXTURE BACKDROP`, `XROTATE OBJECT` |
| [`shapes.dba`](3d/shapes.dba) | Every MAKE OBJECT shape turning on its own axis, with its position, angles and size. | `AUTOCAM OFF`, `BACKDROP ON`, `COLOR BACKDROP`, `COLOR OBJECT`, `DELETE OBJECT`, `HIDE OBJECT`, `MAKE OBJECT BOX`, `MAKE OBJECT CONE`, `MAKE OBJECT CUBE`, `MAKE OBJECT CYLINDER`, `MAKE OBJECT PLAIN`, `MAKE OBJECT SPHERE`, `MAKE OBJECT TRIANGLE`, `OBJECT ANGLE X`, `OBJECT ANGLE Y`, `OBJECT ANGLE Z`, `OBJECT EXIST`, `OBJECT POSITION X`, `OBJECT POSITION Y`, `OBJECT POSITION Z`, `OBJECT SIZE`, `OBJECT SIZE X`, `OBJECT SIZE Y`, `OBJECT SIZE Z`, `OBJECT VISIBLE`, `POINT CAMERA`, `POSITION CAMERA`, `POSITION OBJECT`, `ROTATE OBJECT`, `SCALE OBJECT`, `SET OBJECT`, `SHOW OBJECT`, `WRAPVALUE`, `XROTATE OBJECT`, `YROTATE OBJECT`, `ZROTATE OBJECT` |
| [`static_world.dba`](3d/static_world.dba) | A static maze with sliding collisions, line of sight and a saved level. | `ATTACH OBJECT TO STATIC`, `AUTOCAM OFF`, `COLOR BACKDROP`, `COLOR OBJECT`, `DELETE OBJECT`, `DELETE STATIC OBJECTS`, `DETACH OBJECT FROM STATIC`, `DISABLE STATIC OCCLUSION`, `ENABLE STATIC OCCLUSION`, `GET STATIC COLLISION HIT`, `GET STATIC COLLISION X`, `GET STATIC COLLISION Y`, `GET STATIC COLLISION Z`, `LOAD STATIC OBJECTS`, `MAKE OBJECT BOX`, `MAKE OBJECT CYLINDER`, `MAKE OBJECT SPHERE`, `MAKE STATIC COLLISION BOX`, `MAKE STATIC OBJECT`, `NEWXVALUE`, `NEWZVALUE`, `POINT CAMERA`, `POSITION CAMERA`, `POSITION OBJECT`, `SAVE STATIC OBJECTS`, `SCALE OBJECT TEXTURE`, `SET STATIC OBJECTS TEXTURE`, `STATIC LINE OF SIGHT`, `STATIC LINE OF SIGHT X`, `STATIC LINE OF SIGHT Y`, `STATIC LINE OF SIGHT Z`, `TEXTURE OBJECT`, `WRAPVALUE` |

## Compatibility stubs

Audio CDs, FTP, the Windows folder and old hardware switches have no portable
equivalent. OpenDBC keeps their commands as documented stubs: commands do nothing,
numbers read 0 and strings are empty.

| Program | What it shows |
| --- | --- |
| [`compat_stubs.dba`](legacy/compat_stubs.dba) | Every command OpenDBC accepts but cannot perform today, with the neutral answer it gives. |

Together they cover 696 of the 696 Classic commands, functions and statements of these
help families, and every compatibility stub;
the language statements (`FOR`, `SELECT`, `GOSUB` and so on) appear in the sources rather than in the tables.
