# DarkBASIC Classic compatibility reference

This file is generated from `src/language/language_catalogue.inc` by
`scripts/generate_language_references.py`. It describes OpenDBC support; edit the catalogue, not this file.

`.dba` is the source extension for both profiles. The project, detector or `--dialect` selects one
profile before lexing; includes inherit it and OpenDBC never mixes profiles.
A recognized `Unsupported` form fails binding with `DBC2043`. A `Compatibility stub` executes its
documented neutral behavior and is never presented as implemented.
Inside a function, ordinary scalar names are local; arrays retain their historical shared namespace.
Function return types come from `ENDFUNCTION` and `EXITFUNCTION` values.

Type legend: `number` accepts historical numeric coercion; `output` is assigned by the command;
`array` requires a whole-array marker. Arity is the number of source arguments.

| Symbol | Kind | Arity | Typed overloads | OpenDBC status | Provenance | Description |
|---|---|---:|---|---|---|---|
| `ADD LIMB` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `ALWAYS ACTIVE OFF` | Command | 0 | none -> void | Implemented | Classic help | DarkBASIC Classic command implemented with an observable contract. |
| `ALWAYS ACTIVE ON` | Command | 0 | none -> void | Implemented | Classic help | DarkBASIC Classic command implemented with an observable contract. |
| `APPEND OBJECT` | Command | 3 | string, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `APPEND OBJECT ANIMATION` | Command | 4 | string, string, number, number -> void | Implemented | Classic help | DarkBASIC Classic command implemented with an observable contract. |
| `ATTACH OBJECT TO STATIC` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `AUTOCAM OFF` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `AUTOCAM ON` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `BACKDROP OFF` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `BACKDROP ON` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `BLUR BITMAP` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `BOB` | Command | 4 | number, number, number, number -> void | Implemented | Classic help | DarkBASIC Classic command implemented with an observable contract. |
| `BOX` | Command | 4 | number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `CALL DLL` | Command | 2-11 | number, string, any, any, any, any, any, any, any, any, any -> void | Compatibility stub (permanent) | Classic help, DBPro snapshot | Historical DarkBASIC Classic command callable through a documented neutral compatibility stub. |
| `CD` | Command | 1 | string -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `CENTER TEXT` | Command | 3 | number, number, string -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `CHANGE MESH` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `CHANGE MESH FROM MEMBLOCK` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `CIRCLE` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `CLEAR ALL OBJECT KEYFRAMES` | Command | 1 | number -> void | Implemented | Classic help | DarkBASIC Classic command implemented with an observable contract. |
| `CLEAR CAMERA VIEW` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `CLEAR ENTRY BUFFER` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `CLEAR OBJECT KEYFRAME` | Command | 2 | number, number -> void | Implemented | Classic help | DarkBASIC Classic command implemented with an observable contract. |
| `CLONE SOUND` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `CLOSE FILE` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `CLS` | Command | 0-1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `COLOR AMBIENT LIGHT` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `COLOR BACKDROP` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `COLOR LIGHT` | Command | 2, 4 | number, number -> void; number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `COLOR LIMB` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `COLOR OBJECT` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `COPY BITMAP` | Command | 2, 10 | number, number -> void; number, number, number, number, number, number, number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `COPY FILE` | Command | 2 | string, string -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `COPY MEMBLOCK` | Command | 5 | number, number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `CREATE BITMAP` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `CREATE NET GAME` | Command | 3-4 | string, string, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `CREATE NET PLAYER` | Command | 1 | string -> void | Implemented | Classic help | DarkBASIC Classic command implemented with an observable contract. |
| `DATA` | Command | 1-99 | any... -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `DELETE ANIMATION` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `DELETE BITMAP` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `DELETE BOB` | Command | 1 | number -> void | Implemented | Classic help | DarkBASIC Classic command implemented with an observable contract. |
| `DELETE DIRECTORY` | Command | 1 | string -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `DELETE DLL` | Command | 1 | number -> void | Compatibility stub (permanent) | Classic help, DBPro snapshot | Historical DarkBASIC Classic command callable through a documented neutral compatibility stub. |
| `DELETE FILE` | Command | 1 | string -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `DELETE IMAGE` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `DELETE LIGHT` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `DELETE MATRIX` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `DELETE MEMBLOCK` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `DELETE MESH` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `DELETE MUSIC` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `DELETE OBJECT` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `DELETE OBJECT COLLISION BOX` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `DELETE SOUND` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `DELETE SPRITE` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `DELETE STATIC OBJECTS` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `DETACH OBJECT FROM STATIC` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `DIR` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `DISABLE ESCAPEKEY` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `DISABLE OBJECT ZDEPTH` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `DISABLE STATIC OCCLUSION` | Command | 0 | none -> void | Implemented | Classic help | DarkBASIC Classic command implemented with an observable contract. |
| `DISABLE SYSTEMKEYS` | Command | 0 | none -> void | Compatibility stub (permanent) | Classic help, DBPro snapshot | Historical DarkBASIC Classic command callable through a documented neutral compatibility stub. |
| `DISABLE TNL` | Command | 0 | none -> void | Compatibility stub (permanent) | Classic help | Historical DarkBASIC Classic command callable through a documented neutral compatibility stub. |
| `DOT` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `DRAW TO BACK` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `DRAW TO FRONT` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `DRIVELIST` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `ELLIPSE` | Command | 4 | number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `EMPTY CHECKLIST` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `ENABLE ESCAPEKEY` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `ENABLE OBJECT ZDEPTH` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `ENABLE STATIC OCCLUSION` | Command | 0 | none -> void | Implemented | Classic help | DarkBASIC Classic command implemented with an observable contract. |
| `ENABLE SYSTEMKEYS` | Command | 0 | none -> void | Compatibility stub (permanent) | Classic help, DBPro snapshot | Historical DarkBASIC Classic command callable through a documented neutral compatibility stub. |
| `ENABLE TNL` | Command | 0 | none -> void | Compatibility stub (permanent) | Classic help | Historical DarkBASIC Classic command callable through a documented neutral compatibility stub. |
| `EXECUTE FILE` | Command | 3-4 | string, string, string, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `EXIT PROMPT` | Command | 2 | string, string -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `FADE BITMAP` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `FADE OBJECT` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `FILL MATRIX` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `FIND FIRST` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `FIND NEXT` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `FIX OBJECT PIVOT` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `FLIP BITMAP` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `FLIP BOB` | Command | 1 | number -> void | Implemented | Classic help | DarkBASIC Classic command implemented with an observable contract. |
| `FLIP SPRITE` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `FOG COLOR` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `FOG DISTANCE` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `FOG OFF` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `FOG ON` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `FORCE ANGLE` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `FORCE AUTO CENTER OFF` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `FORCE AUTO CENTER ON` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `FORCE CHAINSAW` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `FORCE DOWN` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `FORCE IMPACT` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `FORCE LEFT` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `FORCE NO EFFECT` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `FORCE RIGHT` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `FORCE SHOOT` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `FORCE UP` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `FORCE WATER EFFECT` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `FREE NET GAME` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `FREE NET PLAYER` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `FTP CONNECT` | Command | 3 | string, string, string -> void | Compatibility stub (permanent) | Classic help, DBPro snapshot | Historical DarkBASIC Classic command callable through a documented neutral compatibility stub. |
| `FTP DELETE FILE` | Command | 1 | string -> void | Compatibility stub (permanent) | Classic help, DBPro snapshot | Historical DarkBASIC Classic command callable through a documented neutral compatibility stub. |
| `FTP DISCONNECT` | Command | 0-1 | number -> void | Compatibility stub (permanent) | Classic help, DBPro snapshot | Historical DarkBASIC Classic command callable through a documented neutral compatibility stub. |
| `FTP FIND FIRST` | Command | 0 | none -> void | Compatibility stub (permanent) | Classic help, DBPro snapshot | Historical DarkBASIC Classic command callable through a documented neutral compatibility stub. |
| `FTP FIND NEXT` | Command | 0 | none -> void | Compatibility stub (permanent) | Classic help, DBPro snapshot | Historical DarkBASIC Classic command callable through a documented neutral compatibility stub. |
| `FTP GET FILE` | Command | 2-3 | string, string, number -> void | Compatibility stub (permanent) | Classic help, DBPro snapshot | Historical DarkBASIC Classic command callable through a documented neutral compatibility stub. |
| `FTP PROCEED` | Command | 0 | none -> void | Compatibility stub (permanent) | Classic help, DBPro snapshot | Historical DarkBASIC Classic command callable through a documented neutral compatibility stub. |
| `FTP PUT FILE` | Command | 1 | string -> void | Compatibility stub (permanent) | Classic help, DBPro snapshot | Historical DarkBASIC Classic command callable through a documented neutral compatibility stub. |
| `FTP SET DIR` | Command | 1 | string -> void | Compatibility stub (permanent) | Classic help, DBPro snapshot | Historical DarkBASIC Classic command callable through a documented neutral compatibility stub. |
| `FTP TERMINATE` | Command | 0 | none -> void | Compatibility stub (permanent) | Classic help, DBPro snapshot | Historical DarkBASIC Classic command callable through a documented neutral compatibility stub. |
| `GET IMAGE` | Command | 5-6 | number, number, number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `GET NET MESSAGE` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `GET VR CONTROLLER POSITION` | Command | 2 | integer, integer -> void | Implemented | OpenDBC extension | Shared OpenDBC VR addition; see docs/vr.md for its observable contract. |
| `GET VR CONTROLLER ROTATION` | Command | 2 | integer, integer -> void | Implemented | OpenDBC extension | Shared OpenDBC VR addition; see docs/vr.md for its observable contract. |
| `GET VR HEAD POSITION` | Command | 1 | integer -> void | Implemented | OpenDBC extension | Shared OpenDBC VR addition; see docs/vr.md for its observable contract. |
| `GET VR ROTATION` | Command | 1 | integer -> void | Implemented | OpenDBC extension | Shared OpenDBC VR addition; see docs/vr.md for its observable contract. |
| `GHOST MATRIX OFF` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `GHOST MATRIX ON` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `GHOST OBJECT OFF` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `GHOST OBJECT ON` | Command | 1-2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `GLUE OBJECT TO LIMB` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `HIDE ALL SPRITES` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `HIDE LIGHT` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `HIDE LIMB` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `HIDE MOUSE` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `HIDE OBJECT` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `HIDE SPRITE` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `HIDE WINDOW` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `INK` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `INPUT` | Command | 1-2 | any, any output -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `JOIN NET GAME` | Command | 2 | number, string -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `LINE` | Command | 4 | number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `LINK LIMB` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `LOAD 3DSOUND` | Command | 2 | string, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `LOAD ANIMATION` | Command | 2 | string, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `LOAD ARRAY` | Command | 2 | string, any array -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `LOAD BITMAP` | Command | 1-2 | string, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `LOAD CDMUSIC` | Command | 2 | number, number -> void | Compatibility stub (permanent) | Classic help, DBPro snapshot | Historical DarkBASIC Classic command callable through a documented neutral compatibility stub. |
| `LOAD DLL` | Command | 2 | string, number -> void | Compatibility stub (permanent) | Classic help, DBPro snapshot | Historical DarkBASIC Classic command callable through a documented neutral compatibility stub. |
| `LOAD IMAGE` | Command | 2-3 | string, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `LOAD MESH` | Command | 2 | string, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `LOAD MUSIC` | Command | 2 | string, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `LOAD OBJECT` | Command | 2 | string, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `LOAD SOUND` | Command | 2-3 | string, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `LOAD STATIC OBJECTS` | Command | 1 | string -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `LOCK BACKBUFFER` | Command | 0 | none -> void | Compatibility stub (permanent) | Classic help, DBPro snapshot | Historical DarkBASIC Classic command callable through a documented neutral compatibility stub. |
| `LOCK OBJECT OFF` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `LOCK OBJECT ON` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `LOOP ANIMATION` | Command | 1, 6 | number -> void; number, number, number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `LOOP MUSIC` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `LOOP OBJECT` | Command | 1-3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `LOOP SOUND` | Command | 1-4 | number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `MAKE BITMAP FROM MEMBLOCK` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `MAKE DIRECTORY` | Command | 1 | string -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `MAKE FILE` | Command | 1 | string -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `MAKE IMAGE FROM MEMBLOCK` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `MAKE LIGHT` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `MAKE MATRIX` | Command | 5 | number, number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `MAKE MEMBLOCK` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `MAKE MEMBLOCK FROM BITMAP` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `MAKE MEMBLOCK FROM FILE` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `MAKE MEMBLOCK FROM IMAGE` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `MAKE MEMBLOCK FROM MESH` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `MAKE MEMBLOCK FROM SOUND` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `MAKE MESH FROM MEMBLOCK` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `MAKE MESH FROM OBJECT` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `MAKE OBJECT` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `MAKE OBJECT BOX` | Command | 4 | number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `MAKE OBJECT COLLISION BOX` | Command | 8 | number, number, number, number, number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `MAKE OBJECT CONE` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `MAKE OBJECT CUBE` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `MAKE OBJECT CYLINDER` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `MAKE OBJECT PLAIN` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `MAKE OBJECT SPHERE` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `MAKE OBJECT TRIANGLE` | Command | 10 | number, number, number, number, number, number, number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `MAKE SOUND FROM MEMBLOCK` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `MAKE STATIC COLLISION BOX` | Command | 6 | number, number, number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `MAKE STATIC OBJECT` | Command | 1-2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `MAXIMIZE WINDOW` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `MINIMIZE WINDOW` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `MIRROR BITMAP` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `MIRROR BOB` | Command | 1 | number -> void | Implemented | Classic help | DarkBASIC Classic command implemented with an observable contract. |
| `MIRROR SPRITE` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `MOVE CAMERA` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `MOVE FILE` | Command | 2 | string, string -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `MOVE OBJECT` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `NET MESSAGE BITMAP` | Command | 1 | number -> void | Compatibility stub (permanent) | Classic help, DBPro snapshot | Historical DarkBASIC Classic command callable through a documented neutral compatibility stub. |
| `NET MESSAGE IMAGE` | Command | 1 | number -> void | Compatibility stub (permanent) | Classic help, DBPro snapshot | Historical DarkBASIC Classic command callable through a documented neutral compatibility stub. |
| `NET MESSAGE MEMBLOCK` | Command | 1 | number -> void | Compatibility stub (permanent) | Classic help, DBPro snapshot | Historical DarkBASIC Classic command callable through a documented neutral compatibility stub. |
| `NET MESSAGE MESH` | Command | 1 | number -> void | Compatibility stub (permanent) | Classic help, DBPro snapshot | Historical DarkBASIC Classic command callable through a documented neutral compatibility stub. |
| `NET MESSAGE SOUND` | Command | 1 | number -> void | Compatibility stub (permanent) | Classic help, DBPro snapshot | Historical DarkBASIC Classic command callable through a documented neutral compatibility stub. |
| `OFFSET BOB` | Command | 3 | number, number, number -> void | Implemented | Classic help | DarkBASIC Classic command implemented with an observable contract. |
| `OFFSET LIMB` | Command | 5 | number, number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `OFFSET SPRITE` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `OPEN TO READ` | Command | 2 | number, string -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `OPEN TO WRITE` | Command | 2 | number, string -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `PASTE BOB` | Command | 3 | number, number, number -> void | Implemented | Classic help | DarkBASIC Classic command implemented with an observable contract. |
| `PASTE IMAGE` | Command | 3-4 | number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `PASTE SPRITE` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `PAUSE ANIMATION` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `PAUSE MUSIC` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `PAUSE SOUND` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `PERFORM CHECKLIST FOR CONTROL DEVICES` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `PERFORM CHECKLIST FOR DISPLAY MODES` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `PERFORM CHECKLIST FOR DRIVES` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `PERFORM CHECKLIST FOR FILES` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `PERFORM CHECKLIST FOR FONTS` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `PERFORM CHECKLIST FOR GRAPHICS CARDS` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `PERFORM CHECKLIST FOR NET CONNECTIONS` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `PERFORM CHECKLIST FOR NET PLAYERS` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `PERFORM CHECKLIST FOR NET SESSIONS` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `PERFORM CHECKLIST FOR OBJECT LIMBS` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `PITCH CAMERA DOWN` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `PITCH CAMERA UP` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `PITCH OBJECT DOWN` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `PITCH OBJECT UP` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `PLACE ANIMATION` | Command | 5 | number, number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `PLAY ANIMATION` | Command | 1, 3, 5, 6 | number -> void; number, number, number -> void; number, number, number, number, number -> void; number, number, number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `PLAY MUSIC` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `PLAY OBJECT` | Command | 1-3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `PLAY SOUND` | Command | 1-2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `POINT CAMERA` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `POINT LIGHT` | Command | 4 | number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `POINT OBJECT` | Command | 4 | number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `POSITION CAMERA` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `POSITION LIGHT` | Command | 4 | number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `POSITION LISTENER` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `POSITION MATRIX` | Command | 4 | number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `POSITION MOUSE` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `POSITION OBJECT` | Command | 4 | number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `POSITION SOUND` | Command | 4 | number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `PREPARE MATRIX TEXTURE` | Command | 4 | number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `PRINT` | Command | 0-99 | any... -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `RANDOMIZE` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `RANDOMIZE MATRIX` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `READ` | Command | 1-99 | any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `READ BYTE` | Command | 2 | number, integer output -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `READ DIRBLOCK` | Command | 2 | number, string -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `READ FILE` | Command | 2 | number, integer output -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `READ FILEBLOCK` | Command | 2 | number, string -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `READ FLOAT` | Command | 2 | number, real output -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `READ LONG` | Command | 2 | number, integer output -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `READ MEMBLOCK` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `READ STRING` | Command | 2 | number, string output -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `READ WORD` | Command | 2 | number, integer output -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `RECENTER VR CAMERA` | Command | 0 | none -> void | Implemented | OpenDBC extension | Shared OpenDBC VR addition; see docs/vr.md for its observable contract. |
| `RECORD SOUND` | Command | 1-2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `RENAME FILE` | Command | 2 | string, string -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `RESTORE` | Command | 0-1 | any... -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `RESTORE WINDOW` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `RESUME ANIMATION` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `RESUME MUSIC` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `RESUME SOUND` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `ROLL CAMERA LEFT` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `ROLL CAMERA RIGHT` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `ROLL OBJECT LEFT` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `ROLL OBJECT RIGHT` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `ROTATE CAMERA` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `ROTATE LIGHT` | Command | 4 | number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `ROTATE LIMB` | Command | 5 | number, number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `ROTATE LISTENER` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `ROTATE OBJECT` | Command | 4 | number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SAVE ARRAY` | Command | 2 | string, any array -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SAVE BITMAP` | Command | 1-2 | string, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SAVE IMAGE` | Command | 2 | string, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SAVE OBJECT ANIMATION` | Command | 2 | string, number -> void | Implemented | Classic help | DarkBASIC Classic command implemented with an observable contract. |
| `SAVE SOUND` | Command | 2 | string, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SAVE STATIC OBJECTS` | Command | 1 | string -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SCALE BOB` | Command | 2 | number, number -> void | Implemented | Classic help | DarkBASIC Classic command implemented with an observable contract. |
| `SCALE LIMB` | Command | 5 | number, number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SCALE LIMB TEXTURE` | Command | 4 | number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SCALE OBJECT` | Command | 4 | number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SCALE OBJECT TEXTURE` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SCALE SPRITE` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SCROLL BACKDROP` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SCROLL LIMB TEXTURE` | Command | 4 | number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SCROLL OBJECT TEXTURE` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SEND NET MESSAGE BITMAP` | Command | 3 | number, number, number -> void | Compatibility stub (permanent) | Classic help, DBPro snapshot | Historical DarkBASIC Classic command callable through a documented neutral compatibility stub. |
| `SEND NET MESSAGE FLOAT` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SEND NET MESSAGE IMAGE` | Command | 3 | number, number, number -> void | Compatibility stub (permanent) | Classic help, DBPro snapshot | Historical DarkBASIC Classic command callable through a documented neutral compatibility stub. |
| `SEND NET MESSAGE INTEGER` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SEND NET MESSAGE MEMBLOCK` | Command | 2-3 | number, number, number -> void | Compatibility stub (permanent) | Classic help, DBPro snapshot | Historical DarkBASIC Classic command callable through a documented neutral compatibility stub. |
| `SEND NET MESSAGE MESH` | Command | 3 | number, number, number -> void | Compatibility stub (permanent) | Classic help, DBPro snapshot | Historical DarkBASIC Classic command callable through a documented neutral compatibility stub. |
| `SEND NET MESSAGE SOUND` | Command | 3 | number, number, number -> void | Compatibility stub (permanent) | Classic help, DBPro snapshot | Historical DarkBASIC Classic command callable through a documented neutral compatibility stub. |
| `SEND NET MESSAGE STRING` | Command | 2 | number, string -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET AMBIENT LIGHT` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET BOB` | Command | 3 | number, number, number -> void | Implemented | Classic help | DarkBASIC Classic command implemented with an observable contract. |
| `SET CAMERA FOV` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET CAMERA RANGE` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET CAMERA ROTATION XYZ` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET CAMERA ROTATION ZYX` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET CAMERA TO FOLLOW` | Command | 8 | number, number, number, number, number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET CAMERA TO OBJECT ORIENTATION` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET CAMERA VIEW` | Command | 4 | number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET CONTROL DEVICE` | Command | 1 | string -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET CURRENT BITMAP` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET CURSOR` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET DIR` | Command | 1 | string -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET DIRECTIONAL LIGHT` | Command | 4 | number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET DISPLAY MODE` | Command | 2-3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET EAX` | Command | 1 | number -> void | Compatibility stub (permanent) | Classic help | Historical DarkBASIC Classic command callable through a documented neutral compatibility stub. |
| `SET EMULATION OFF` | Command | 0 | none -> void | Compatibility stub (permanent) | Classic help | Historical DarkBASIC Classic command callable through a documented neutral compatibility stub. |
| `SET EMULATION ON` | Command | 0 | none -> void | Compatibility stub (permanent) | Classic help | Historical DarkBASIC Classic command callable through a documented neutral compatibility stub. |
| `SET GAMMA` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET GLOBAL COLLISION OFF` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET GLOBAL COLLISION ON` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET GRAPHICS CARD` | Command | 1 | string -> void | Compatibility stub (permanent) | Classic help, DBPro snapshot | Historical DarkBASIC Classic command callable through a documented neutral compatibility stub. |
| `SET LIGHT RANGE` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET LIGHT TO OBJECT ORIENTATION` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET LIGHT TO OBJECT POSITION` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET MATRIX` | Command | 8 | number, number, number, number, number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET MATRIX HEIGHT` | Command | 4 | number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET MATRIX NORMAL` | Command | 6 | number, number, number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET MATRIX TEXTURE` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET MATRIX TILE` | Command | 4 | number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET MATRIX WIREFRAME OFF` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET MATRIX WIREFRAME ON` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET MIPMAP MODE` | Command | 1 | number -> void | Implemented | Classic help | DarkBASIC Classic command implemented with an observable contract. |
| `SET NET CONNECTION` | Command | 1-2 | number, string -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET NORMALIZATION OFF` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET NORMALIZATION ON` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET OBJECT` | Command | 4-8 | number, number, number, number, number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET OBJECT COLLISION OFF` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET OBJECT COLLISION ON` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET OBJECT COLLISION TO BOXES` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET OBJECT COLLISION TO POLYGONS` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET OBJECT COLLISION TO SPHERES` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET OBJECT FRAME` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET OBJECT INTERPOLATION` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET OBJECT KEYFRAME` | Command | 2 | number, number -> void | Implemented | Classic help | DarkBASIC Classic command implemented with an observable contract. |
| `SET OBJECT ROTATION XYZ` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET OBJECT ROTATION ZYX` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET OBJECT SPEED` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET OBJECT TEXTURE` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET OBJECT TO CAMERA ORIENTATION` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET OBJECT TO OBJECT ORIENTATION` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET POINT LIGHT` | Command | 4 | number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET SOUND PAN` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET SOUND SPEED` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET SOUND VOLUME` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET SPOT LIGHT` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET SPRITE` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET STATIC OBJECTS TEXTURE` | Command | 1 | number -> void | Implemented | Classic help | DarkBASIC Classic command implemented with an observable contract. |
| `SET STATIC OBJECTS WIREFRAME OFF` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET STATIC OBJECTS WIREFRAME ON` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET TEXT FONT` | Command | 1-2 | string, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET TEXT OPAQUE` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET TEXT SIZE` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET TEXT TO BOLD` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET TEXT TO BOLDITALIC` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET TEXT TO ITALIC` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET TEXT TO NORMAL` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET TEXT TRANSPARENT` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET TRACKING SPACE` | Command | 1 | integer -> void | Implemented | OpenDBC extension | Shared OpenDBC VR addition; see docs/vr.md for its observable contract. |
| `SET VR CAMERA HEIGHT` | Command | 1 | real -> void | Implemented | OpenDBC extension | Shared OpenDBC VR addition; see docs/vr.md for its observable contract. |
| `SET VR ENABLED` | Command | 1 | integer -> void | Implemented | OpenDBC extension | Shared OpenDBC VR addition; see docs/vr.md for its observable contract. |
| `SET VR PREVIEW MODE` | Command | 1 | integer -> void | Implemented | OpenDBC extension | Shared OpenDBC VR addition; see docs/vr.md for its observable contract. |
| `SET WINDOW LAYOUT` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET WINDOW OFF` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET WINDOW ON` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET WINDOW POSITION` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET WINDOW SIZE` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SET WINDOW TITLE` | Command | 1 | string -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SHIFT MATRIX DOWN` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SHIFT MATRIX LEFT` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SHIFT MATRIX RIGHT` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SHIFT MATRIX UP` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SHOW ALL SPRITES` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SHOW LIGHT` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SHOW LIMB` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SHOW MOUSE` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SHOW OBJECT` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SHOW SPRITE` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SHOW WINDOW` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SIZE SPRITE` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SLEEP` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SPRITE` | Command | 4 | number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `STOP ANIMATION` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `STOP MUSIC` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `STOP OBJECT` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `STOP RECORDING SOUND` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `STOP SOUND` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `STRETCH BOB` | Command | 3 | number, number, number -> void | Implemented | Classic help | DarkBASIC Classic command implemented with an observable contract. |
| `STRETCH SPRITE` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SUSPEND FOR KEY` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SUSPEND FOR MOUSE` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SYNC` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SYNC OFF` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SYNC ON` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `SYNC RATE` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `TEXT` | Command | 3 | number, number, string -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `TEXTURE BACKDROP` | Command | 1 | number -> void | Implemented | Classic help | DarkBASIC Classic command implemented with an observable contract. |
| `TEXTURE LIMB` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `TEXTURE OBJECT` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `TURN CAMERA LEFT` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `TURN CAMERA RIGHT` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `TURN OBJECT LEFT` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `TURN OBJECT RIGHT` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `UNDIM` | Command | 1 | any array -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `UNGLUE OBJECT` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `UNLOCK BACKBUFFER` | Command | 0 | none -> void | Compatibility stub (permanent) | Classic help, DBPro snapshot | Historical DarkBASIC Classic command callable through a documented neutral compatibility stub. |
| `UPDATE MATRIX` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `WAIT` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `WAIT KEY` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `WRITE BYTE` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `WRITE DIRBLOCK` | Command | 2 | number, string -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `WRITE FILE` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `WRITE FILEBLOCK` | Command | 2 | number, string -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `WRITE FLOAT` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `WRITE LONG` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `WRITE MEMBLOCK` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `WRITE MEMBLOCK BYTE` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `WRITE MEMBLOCK DWORD` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `WRITE MEMBLOCK FLOAT` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `WRITE MEMBLOCK WORD` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `WRITE STRING` | Command | 2 | number, string -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `WRITE TO CLIPBOARD` | Command | 1 | string -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `WRITE TO REGISTRY` | Command | 3 | string, string, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `WRITE WORD` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `XROTATE CAMERA` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `XROTATE OBJECT` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `YROTATE CAMERA` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `YROTATE OBJECT` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `ZROTATE CAMERA` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `ZROTATE OBJECT` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic command implemented with an observable contract. |
| `#INCLUDE` | Directive | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic directive implemented with an observable contract. |
| `3DBLIT AVAILABLE` | Function | 0 | none -> real | Implemented | Classic help | DarkBASIC Classic function implemented with an observable contract. |
| `ABS` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `ACOS` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `ALPHABLENDING AVAILABLE` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `ANIMATION EXIST` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `ANIMATION HEIGHT` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `ANIMATION LOOPED` | Function | 1 | number -> real | Implemented | Classic help | DarkBASIC Classic function implemented with an observable contract. |
| `ANIMATION LOOPING` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `ANIMATION PAUSED` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `ANIMATION PLAYING` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `ANIMATION POSITION X` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `ANIMATION POSITION Y` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `ANIMATION WIDTH` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `APPNAME$` | Function | 0 | none -> string | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `ASC` | Function | 1 | string -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `ASIN` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `ATAN` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `ATANFULL` | Function | 2 | number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `BIN$` | Function | 1 | number -> string | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `BITMAP DEPTH` | Function | 0-1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `BITMAP EXIST` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `BITMAP FLIPPED` | Function | 0-1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `BITMAP HEIGHT` | Function | 0-1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `BITMAP MIRRORED` | Function | 0-1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `BITMAP WIDTH` | Function | 0-1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `BOB COLLISION` | Function | 2 | number, number -> real | Implemented | Classic help | DarkBASIC Classic function implemented with an observable contract. |
| `BOB EXIST` | Function | 1 | number -> real | Implemented | Classic help | DarkBASIC Classic function implemented with an observable contract. |
| `BOB FLIPPED` | Function | 1 | number -> real | Implemented | Classic help | DarkBASIC Classic function implemented with an observable contract. |
| `BOB HEIGHT` | Function | 1 | number -> real | Implemented | Classic help | DarkBASIC Classic function implemented with an observable contract. |
| `BOB HIT` | Function | 2 | number, number -> real | Implemented | Classic help | DarkBASIC Classic function implemented with an observable contract. |
| `BOB IMAGE` | Function | 1 | number -> real | Implemented | Classic help | DarkBASIC Classic function implemented with an observable contract. |
| `BOB MIRRORED` | Function | 1 | number -> real | Implemented | Classic help | DarkBASIC Classic function implemented with an observable contract. |
| `BOB OFFSET X` | Function | 1 | number -> real | Implemented | Classic help | DarkBASIC Classic function implemented with an observable contract. |
| `BOB OFFSET Y` | Function | 1 | number -> real | Implemented | Classic help | DarkBASIC Classic function implemented with an observable contract. |
| `BOB SCALE X` | Function | 1 | number -> real | Implemented | Classic help | DarkBASIC Classic function implemented with an observable contract. |
| `BOB SCALE Y` | Function | 1 | number -> real | Implemented | Classic help | DarkBASIC Classic function implemented with an observable contract. |
| `BOB WIDTH` | Function | 1 | number -> real | Implemented | Classic help | DarkBASIC Classic function implemented with an observable contract. |
| `BOB X` | Function | 1 | number -> real | Implemented | Classic help | DarkBASIC Classic function implemented with an observable contract. |
| `BOB Y` | Function | 1 | number -> real | Implemented | Classic help | DarkBASIC Classic function implemented with an observable contract. |
| `CALL DLL` | Function | 2-11 | number, string, any, any, any, any, any, any, any, any, any -> real | Compatibility stub (permanent) | Classic help | Historical DarkBASIC Classic function callable through a documented neutral compatibility stub. |
| `CAMERA ANGLE X` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `CAMERA ANGLE Y` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `CAMERA ANGLE Z` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `CAMERA POSITION X` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `CAMERA POSITION Y` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `CAMERA POSITION Z` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `CHECK DISPLAY MODE` | Function | 3 | number, number, number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `CHECKLIST QUANTITY` | Function | 0 | none -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `CHECKLIST STRING$` | Function | 1 | number -> string | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `CHECKLIST VALUE A` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `CHECKLIST VALUE B` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `CHECKLIST VALUE C` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `CHECKLIST VALUE D` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `CHR$` | Function | 1 | number -> string | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `CL$` | Function | 0 | none -> string | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `CONTROL DEVICE NAME$` | Function | 0 | none -> string | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `CONTROL DEVICE X` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `CONTROL DEVICE Y` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `CONTROL DEVICE Z` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `CONTROLKEY` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `COS` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `CREATE NET PLAYER` | Function | 1 | string -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `CURRENT BITMAP` | Function | 0 | none -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `CURRENT GRAPHICS CARD$` | Function | 0 | none -> string | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `CURVEANGLE` | Function | 3 | number, number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `CURVEVALUE` | Function | 3 | number, number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `DLL CALL EXIST` | Function | 2 | number, string -> real | Compatibility stub (permanent) | Classic help, DBPro snapshot | Historical DarkBASIC Classic function callable through a documented neutral compatibility stub. |
| `DLL EXIST` | Function | 1 | number -> real | Compatibility stub (permanent) | Classic help, DBPro snapshot | Historical DarkBASIC Classic function callable through a documented neutral compatibility stub. |
| `DOWNKEY` | Function | 0 | none -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `EMULATION MODE` | Function | 0 | none -> real | Compatibility stub (permanent) | Classic help | Historical DarkBASIC Classic function callable through a documented neutral compatibility stub. |
| `ENTRY$` | Function | 0 | none -> string | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `ESCAPEKEY` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `EXP` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `FILE END` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `FILE EXIST` | Function | 1 | string -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `FILE OPEN` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `FILE SIZE` | Function | 1 | string -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `FILTERING AVAILABLE` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `FOG AVAILABLE` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `GET BACKBUFFER DEPTH` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `GET BACKBUFFER HEIGHT` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `GET BACKBUFFER PITCH` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `GET BACKBUFFER PTR` | Function | 0 | none -> real | Compatibility stub (permanent) | Classic help, DBPro snapshot | Historical DarkBASIC Classic function callable through a documented neutral compatibility stub. |
| `GET BACKBUFFER WIDTH` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `GET CLIPBOARD$` | Function | 0 | none -> string | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `GET DATE$` | Function | 0 | none -> string | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `GET DIR$` | Function | 0 | none -> string | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `GET FILE DATE$` | Function | 0 | none -> string | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `GET FILE NAME$` | Function | 0 | none -> string | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `GET FILE TYPE` | Function | 0 | none -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `GET FTP DIR$` | Function | 0 | none -> string | Compatibility stub (permanent) | Classic help, DBPro snapshot | Historical DarkBASIC Classic function callable through a documented neutral compatibility stub. |
| `GET FTP ERROR$` | Function | 0 | none -> string | Compatibility stub (permanent) | Classic help, DBPro snapshot | Historical DarkBASIC Classic function callable through a documented neutral compatibility stub. |
| `GET FTP FAILURE` | Function | 0 | none -> real | Compatibility stub (permanent) | Classic help, DBPro snapshot | Historical DarkBASIC Classic function callable through a documented neutral compatibility stub. |
| `GET FTP FILE NAME$` | Function | 0 | none -> string | Compatibility stub (permanent) | Classic help, DBPro snapshot | Historical DarkBASIC Classic function callable through a documented neutral compatibility stub. |
| `GET FTP FILE SIZE` | Function | 0 | none -> real | Compatibility stub (permanent) | Classic help, DBPro snapshot | Historical DarkBASIC Classic function callable through a documented neutral compatibility stub. |
| `GET FTP FILE TYPE` | Function | 0 | none -> real | Compatibility stub (permanent) | Classic help, DBPro snapshot | Historical DarkBASIC Classic function callable through a documented neutral compatibility stub. |
| `GET FTP PROGRESS` | Function | 0 | none -> real | Compatibility stub (permanent) | Classic help, DBPro snapshot | Historical DarkBASIC Classic function callable through a documented neutral compatibility stub. |
| `GET FTP STATUS` | Function | 0 | none -> real | Compatibility stub (permanent) | Classic help, DBPro snapshot | Historical DarkBASIC Classic function callable through a documented neutral compatibility stub. |
| `GET GROUND HEIGHT` | Function | 3 | number, number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `GET MATRIX HEIGHT` | Function | 3 | number, number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `GET MEMBLOCK PTR` | Function | 1 | number -> real | Compatibility stub (permanent) | Classic help, DBPro snapshot | Historical DarkBASIC Classic function callable through a documented neutral compatibility stub. |
| `GET MEMBLOCK SIZE` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `GET OBJECT COLLISION X` | Function | 0-1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `GET OBJECT COLLISION Y` | Function | 0-1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `GET OBJECT COLLISION Z` | Function | 0-1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `GET REGISTRY` | Function | 2 | string, string -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `GET SOUND PAN` | Function | 1 | number -> integer | Implemented | Classic help | DarkBASIC Classic function implemented with an observable contract. |
| `GET SOUND SPEED` | Function | 1 | number -> integer | Implemented | Classic help | DarkBASIC Classic function implemented with an observable contract. |
| `GET SOUND VOLUME` | Function | 1 | number -> integer | Implemented | Classic help | DarkBASIC Classic function implemented with an observable contract. |
| `GET STATIC COLLISION HIT` | Function | 12 | number, number, number, number, number, number, number, number, number, number, number, number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `GET STATIC COLLISION X` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `GET STATIC COLLISION Y` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `GET STATIC COLLISION Z` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `GET TIME$` | Function | 0 | none -> string | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `GET VR CAMERA HEIGHT` | Function | 0 | none -> real | Implemented | OpenDBC extension | Shared OpenDBC VR addition; see docs/vr.md for its observable contract. |
| `HCOS` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `HEX$` | Function | 1 | number -> string | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `HSIN` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `HTAN` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `IMAGE EXIST` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `INKEY$` | Function | 0 | none -> string | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `INSTR` | Function | 2 | string, string -> integer | Implemented | legacy unverified | DarkBASIC Classic function implemented with an observable contract. |
| `INT` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `IS VR CONTROLLER TRACKED` | Function | 1 | integer -> integer | Implemented | OpenDBC extension | Shared OpenDBC VR addition; see docs/vr.md for its observable contract. |
| `IS VR ENABLED` | Function | 0 | none -> integer | Implemented | OpenDBC extension | Shared OpenDBC VR addition; see docs/vr.md for its observable contract. |
| `IS VR HEAD TRACKED` | Function | 0 | none -> integer | Implemented | OpenDBC extension | Shared OpenDBC VR addition; see docs/vr.md for its observable contract. |
| `IS VR SUPPORTED` | Function | 0 | none -> integer | Implemented | OpenDBC extension | Shared OpenDBC VR addition; see docs/vr.md for its observable contract. |
| `JOYSTICK DOWN` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `JOYSTICK FIRE A` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `JOYSTICK FIRE B` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `JOYSTICK FIRE C` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `JOYSTICK FIRE D` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `JOYSTICK FIRE X` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `JOYSTICK HAT ANGLE` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `JOYSTICK LEFT` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `JOYSTICK RIGHT` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `JOYSTICK SLIDER A` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `JOYSTICK SLIDER B` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `JOYSTICK SLIDER C` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `JOYSTICK SLIDER D` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `JOYSTICK TWIST X` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `JOYSTICK TWIST Y` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `JOYSTICK TWIST Z` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `JOYSTICK UP` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `JOYSTICK X` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `JOYSTICK Y` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `JOYSTICK Z` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `KEYSTATE` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `LEFT$` | Function | 2 | string, number -> string | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `LEFTKEY` | Function | 0 | none -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `LEN` | Function | 1 | string -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `LIGHT DIRECTION X` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `LIGHT DIRECTION Y` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `LIGHT DIRECTION Z` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `LIGHT EXIST` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `LIGHT POSITION X` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `LIGHT POSITION Y` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `LIGHT POSITION Z` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `LIGHT RANGE` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `LIGHT TYPE` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `LIGHT VISIBLE` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `LIMB ANGLE X` | Function | 2 | number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `LIMB ANGLE Y` | Function | 2 | number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `LIMB ANGLE Z` | Function | 2 | number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `LIMB DIRECTION X` | Function | 2 | number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `LIMB DIRECTION Y` | Function | 2 | number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `LIMB DIRECTION Z` | Function | 2 | number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `LIMB EXIST` | Function | 2 | number, number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `LIMB OFFSET X` | Function | 2 | number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `LIMB OFFSET Y` | Function | 2 | number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `LIMB OFFSET Z` | Function | 2 | number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `LIMB POSITION X` | Function | 2 | number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `LIMB POSITION Y` | Function | 2 | number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `LIMB POSITION Z` | Function | 2 | number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `LIMB TEXTURE` | Function | 2 | number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `LIMB TEXTURE NAME$` | Function | 2 | number, number -> string | Implemented | Classic help | DarkBASIC Classic function implemented with an observable contract. |
| `LIMB VISIBLE` | Function | 2 | number, number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `LISTENER ANGLE X` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `LISTENER ANGLE Y` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `LISTENER ANGLE Z` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `LISTENER POSITION X` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `LISTENER POSITION Y` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `LISTENER POSITION Z` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `LOWER$` | Function | 1 | string -> string | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `MATRIX EXIST` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `MATRIX POSITION X` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `MATRIX POSITION Y` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `MATRIX POSITION Z` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `MATRIX TILE COUNT` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `MATRIX TILES EXIST` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `MATRIX WIREFRAME STATE` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `MEMBLOCK BYTE` | Function | 2 | number, number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `MEMBLOCK DWORD` | Function | 2 | number, number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `MEMBLOCK EXIST` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `MEMBLOCK FLOAT` | Function | 2 | number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `MEMBLOCK WORD` | Function | 2 | number, number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `MEMORY FREE` | Function | 0 | none -> integer | Implemented | Classic help, legacy unverified | DarkBASIC Classic function implemented with an observable contract. |
| `MESH EXIST` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `MID$` | Function | 2 | string, number -> string | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `MOUSECLICK` | Function | 0 | none -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `MOUSEMOVEX` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `MOUSEMOVEY` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `MOUSEMOVEZ` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `MOUSEX` | Function | 0 | none -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `MOUSEY` | Function | 0 | none -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `MOUSEZ` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `MUSIC EXIST` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `MUSIC LOOPING` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `MUSIC PAUSED` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `MUSIC PLAYING` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `NET GAME EXISTS` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `NET GAME LOST` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `NET GAME NOW HOSTING` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `NET MESSAGE EXISTS` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `NET MESSAGE FLOAT` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `NET MESSAGE INTEGER` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `NET MESSAGE PLAYER FROM` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `NET MESSAGE PLAYER TO` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `NET MESSAGE STRING$` | Function | 0 | none -> string | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `NET MESSAGE TYPE` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `NET PLAYER CREATED` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `NET PLAYER DESTROYED` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `NEWXVALUE` | Function | 3 | number, number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `NEWYVALUE` | Function | 3 | number, number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `NEWZVALUE` | Function | 3 | number, number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `OBJECT ANGLE X` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `OBJECT ANGLE Y` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `OBJECT ANGLE Z` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `OBJECT COLLISION` | Function | 2 | number, number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `OBJECT EXIST` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `OBJECT FRAME` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `OBJECT HIT` | Function | 2 | number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `OBJECT IN SCREEN` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `OBJECT INTERPOLATION` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `OBJECT LOOPING` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `OBJECT PLAYING` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `OBJECT POSITION X` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `OBJECT POSITION Y` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `OBJECT POSITION Z` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `OBJECT SCREEN X` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `OBJECT SCREEN Y` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `OBJECT SIZE` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `OBJECT SIZE X` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `OBJECT SIZE Y` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `OBJECT SIZE Z` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `OBJECT SPEED` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `OBJECT VISIBLE` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `PATH EXIST` | Function | 1 | string -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `POINT` | Function | 2 | number, number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `READ BYTE` | Function | 1 | integer output -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `READ FLOAT` | Function | 1 | real output -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `READ LONG` | Function | 1 | integer output -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `READ WORD` | Function | 1 | integer output -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `RETURNKEY` | Function | 0 | none -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `RGB` | Function | 3 | number, number, number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `RGBB` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `RGBG` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `RGBR` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `RIGHT$` | Function | 2 | string, number -> string | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `RIGHTKEY` | Function | 0 | none -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `RND` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `SCANCODE` | Function | 0 | none -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `SCREEN DEPTH` | Function | 0 | none -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `SCREEN FPS` | Function | 0 | none -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `SCREEN HEIGHT` | Function | 0 | none -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `SCREEN INVALID` | Function | 0 | none -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `SCREEN TYPE` | Function | 0 | none -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `SCREEN WIDTH` | Function | 0 | none -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `SHIFTKEY` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `SIN` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `SOUND EXIST` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `SOUND LOOPING` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `SOUND PAUSED` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `SOUND PLAYING` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `SOUND POSITION X` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `SOUND POSITION Y` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `SOUND POSITION Z` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `SOUND TYPE` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `SPACEKEY` | Function | 0 | none -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `SPRITE COLLISION` | Function | 2 | number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `SPRITE EXIST` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `SPRITE FLIPPED` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `SPRITE HEIGHT` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `SPRITE HIT` | Function | 2 | number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `SPRITE IMAGE` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `SPRITE MIRRORED` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `SPRITE OFFSET X` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `SPRITE OFFSET Y` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `SPRITE SCALE X` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `SPRITE SCALE Y` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `SPRITE WIDTH` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `SPRITE X` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `SPRITE Y` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `SQRT` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `STATIC LINE OF SIGHT` | Function | 8 | number, number, number, number, number, number, number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `STATIC LINE OF SIGHT X` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `STATIC LINE OF SIGHT Y` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `STATIC LINE OF SIGHT Z` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `STATISTIC` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `STR$` | Function | 1 | any -> string | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `SYSTEM DMEM AVAILABLE` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `SYSTEM SMEM AVAILABLE` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `SYSTEM TMEM AVAILABLE` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `TAN` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `TEXT BACKGROUND TYPE` | Function | 0 | none -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `TEXT FONT$` | Function | 0 | none -> string | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `TEXT HEIGHT` | Function | 1 | string -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `TEXT SIZE` | Function | 0 | none -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `TEXT STYLE` | Function | 0 | none -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `TEXT WIDTH` | Function | 1 | string -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `TIMER` | Function | 0 | none -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `TNL AVAILABLE` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `TOTAL OBJECT FRAMES` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `UPKEY` | Function | 0 | none -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `UPPER$` | Function | 1 | string -> string | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `VAL` | Function | 1 | string -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `WINDIR$` | Function | 0 | none -> string | Compatibility stub (permanent) | Classic help, DBPro snapshot | Historical DarkBASIC Classic function callable through a documented neutral compatibility stub. |
| `WRAPVALUE` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic function implemented with an observable contract. |
| `X VECTOR3` | Function | 1 | number -> real | Implemented | DBPro snapshot, OpenDBC extension | Shared OpenDBC VR addition; see docs/vr.md for its observable contract. |
| `Y VECTOR3` | Function | 1 | number -> real | Implemented | DBPro snapshot, OpenDBC extension | Shared OpenDBC VR addition; see docs/vr.md for its observable contract. |
| `Z VECTOR3` | Function | 1 | number -> real | Implemented | DBPro snapshot, OpenDBC extension | Shared OpenDBC VR addition; see docs/vr.md for its observable contract. |
| `*` | Operator | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic operator implemented with an observable contract. |
| `+` | Operator | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic operator implemented with an observable contract. |
| `-` | Operator | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic operator implemented with an observable contract. |
| `/` | Operator | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic operator implemented with an observable contract. |
| `<` | Operator | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic operator implemented with an observable contract. |
| `<=` | Operator | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic operator implemented with an observable contract. |
| `<>` | Operator | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic operator implemented with an observable contract. |
| `=` | Operator | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic operator implemented with an observable contract. |
| `>` | Operator | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic operator implemented with an observable contract. |
| `>=` | Operator | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic operator implemented with an observable contract. |
| `AND` | Operator | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic operator implemented with an observable contract. |
| `MOD` | Operator | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic operator implemented with an observable contract. |
| `NOT` | Operator | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic operator implemented with an observable contract. |
| `OR` | Operator | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic operator implemented with an observable contract. |
| `^` | Operator | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic operator implemented with an observable contract. |
| `CASE` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic statement implemented with an observable contract. |
| `CASE DEFAULT` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic statement implemented with an observable contract. |
| `DATA` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic statement implemented with an observable contract. |
| `DEC` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic statement implemented with an observable contract. |
| `DIM` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic statement implemented with an observable contract. |
| `DO` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic statement implemented with an observable contract. |
| `ELSE` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic statement implemented with an observable contract. |
| `END` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic statement implemented with an observable contract. |
| `ENDCASE` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic statement implemented with an observable contract. |
| `ENDFUNCTION` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic statement implemented with an observable contract. |
| `ENDIF` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic statement implemented with an observable contract. |
| `ENDSELECT` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic statement implemented with an observable contract. |
| `ENDWHILE` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic statement implemented with an observable contract. |
| `EXIT` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic statement implemented with an observable contract. |
| `EXITFUNCTION` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic statement implemented with an observable contract. |
| `FOR` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic statement implemented with an observable contract. |
| `FUNCTION` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic statement implemented with an observable contract. |
| `GOSUB` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic statement implemented with an observable contract. |
| `GOTO` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic statement implemented with an observable contract. |
| `IF` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic statement implemented with an observable contract. |
| `INC` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic statement implemented with an observable contract. |
| `LOOP` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic statement implemented with an observable contract. |
| `NEXT` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic statement implemented with an observable contract. |
| `READ` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic statement implemented with an observable contract. |
| `REM` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic statement implemented with an observable contract. |
| `REMEND` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic statement implemented with an observable contract. |
| `REMSTART` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic statement implemented with an observable contract. |
| `REPEAT` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic statement implemented with an observable contract. |
| `RESTORE` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic statement implemented with an observable contract. |
| `RETURN` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic statement implemented with an observable contract. |
| `SELECT` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic statement implemented with an observable contract. |
| `STEP` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic statement implemented with an observable contract. |
| `THEN` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic statement implemented with an observable contract. |
| `UNDIM` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic statement implemented with an observable contract. |
| `UNTIL` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic statement implemented with an observable contract. |
| `WHILE` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Classic statement implemented with an observable contract. |
