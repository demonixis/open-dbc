# DarkBASIC Professional compatibility reference

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
| `ADD LIGHT MAP LIGHT` | Command | 9 | number, number, number, number, number, number, number, number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ADD LIMB` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `ADD LIMB TO LIGHT MAP POOL` | Command | 2 | number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ADD LOD TO OBJECT` | Command | 4 | number, number, number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ADD MATRIX4` | Command | 3 | number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `ADD MEMBLOCK TO OBJECT` | Command | 2 | number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ADD MESH TO VERTEXDATA` | Command | 1 | number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `ADD OBJECT TO LIGHT MAP POOL` | Command | 1 | number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ADD STATIC OBJECTS TO LIGHT MAP POOL` | Command | 0 | none -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ADD STATIC SCORCH` | Command | 2 | number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ADD TO QUEUE` | Command | 1 | any array -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `ADD TO STACK` | Command | 1 | any array -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `ADD VECTOR2` | Command | 3 | number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `ADD VECTOR3` | Command | 3 | number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `ADD VECTOR4` | Command | 3 | number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `APPEND OBJECT` | Command | 3 | number, number, number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ARRAY DELETE ELEMENT` | Command | 1-2 | any array, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `ARRAY INDEX TO BOTTOM` | Command | 1 | any array -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `ARRAY INDEX TO QUEUE` | Command | 1 | any array -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `ARRAY INDEX TO STACK` | Command | 1 | any array -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `ARRAY INDEX TO TOP` | Command | 1 | any array -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `ARRAY INSERT AT BOTTOM` | Command | 1-2 | any array, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `ARRAY INSERT AT ELEMENT` | Command | 2 | any array, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `ARRAY INSERT AT TOP` | Command | 1-2 | any array, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `ATTACH OBJECT TO STATIC` | Command | 1 | number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `AUTOCAM OFF` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `AUTOCAM ON` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `AUTOMATIC CAMERA COLLISION` | Command | 3-4 | number, number, number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `AUTOMATIC OBJECT COLLISION` | Command | 3 | number, number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `BACKDROP OFF` | Command | 0-1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `BACKDROP ON` | Command | 0-1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `BCC VECTOR2` | Command | 6 | number, number, number, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `BCC VECTOR3` | Command | 6 | number, number, number, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `BCC VECTOR4` | Command | 6 | number, number, number, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `BLUR BITMAP` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `BOX` | Command | 4, 8 | number, number, number, number -> void; number, number, number, number, number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `BREAK` | Command | 0-1 | number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `BUILD FOV LHMATRIX4` | Command | 5 | number, number, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `BUILD FOV RHMATRIX4` | Command | 5 | number, number, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `BUILD LOOKAT LHMATRIX4` | Command | 4 | number, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `BUILD LOOKAT RHMATRIX4` | Command | 4 | number, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `BUILD ORTHO LHMATRIX4` | Command | 5 | number, number, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `BUILD ORTHO RHMATRIX4` | Command | 5 | number, number, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `BUILD PERSPECTIVE LHMATRIX4` | Command | 5 | number, number, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `BUILD PERSPECTIVE RHMATRIX4` | Command | 5 | number, number, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `BUILD REFLECTION MATRIX4` | Command | 5 | number, number, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `BUILD ROTATION AXIS MATRIX4` | Command | 3 | number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `BUILD STATIC PORTALS` | Command | 0 | none -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `BUILD TERRAIN` | Command | 1 | number -> void | Unsupported (DBC2043) | DBPro snapshot, legacy unverified | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `CALCULATE AUTOMATIC COLLISION` | Command | 0 | none -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `CALCULATE OBJECT BOUNDS` | Command | 1 | number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `CALL DLL` | Command | 2-3 | number, number, number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `CALL DLL X` | Command | 4 | number, number, number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `CATMULLROM VECTOR2` | Command | 6 | number, number, number, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `CATMULLROM VECTOR3` | Command | 6 | number, number, number, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `CATMULLROM VECTOR4` | Command | 6 | number, number, number, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `CD` | Command | 1 | string -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `CENTER TEXT` | Command | 3 | number, number, string -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `CHANGE MESH` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `CHANGE MESH FROM MEMBLOCK` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `CHANGE MOUSE` | Command | 1-2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `CIRCLE` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `CLEAR CAMERA VIEW` | Command | 1-2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `CLEAR ENTRY BUFFER` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `CLONE OBJECT` | Command | 2-3 | number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `CLONE SOUND` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `CLONE SPRITE` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `CLOSE FILE` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `CLOSE IMAGEBLOCK` | Command | 0 | none -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `CLS` | Command | 0-1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `COLOR AMBIENT LIGHT` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `COLOR BACKDROP` | Command | 1-2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `COLOR LIGHT` | Command | 2, 4 | number, number -> void; number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `COLOR LIMB` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `COLOR OBJECT` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `COLOR PARTICLES` | Command | 4 | number, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `CONTROL CAMERA USING ARROWKEYS` | Command | 3 | number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `CONVERT MESH TO VERTEXDATA` | Command | 1 | number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `CONVERT OBJECT FVF` | Command | 2 | number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `COPY BITMAP` | Command | 2, 10 | number, number -> void; number, number, number, number, number, number, number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `COPY FILE` | Command | 2 | string, string -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `COPY MATRIX4` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `COPY MEMBLOCK` | Command | 5 | number, number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `COPY MEMORY` | Command | 3 | number, number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `COPY VECTOR2` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `COPY VECTOR3` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `COPY VECTOR4` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `CREATE ANIMATED SPRITE` | Command | 5 | number, string, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `CREATE BITMAP` | Command | 3-4 | number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `CREATE LIGHT MAPS` | Command | 3 | number, number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `CREATE NET GAME` | Command | 3-4 | number, number, number, number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `CREATE PIXEL SHADER FROM FILE` | Command | 2 | number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `CREATE VERTEX SHADER FROM FILE` | Command | 2 | number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `CROSS PRODUCT VECTOR3` | Command | 3 | number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `DATA` | Command | 1-99 | any... -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `DELETE ANIMATION` | Command | 1 | number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `DELETE BITMAP` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `DELETE BSP` | Command | 0 | none -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `DELETE CAMERA` | Command | 1 | number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `DELETE DIRECTORY` | Command | 1 | string -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `DELETE DLL` | Command | 1 | number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `DELETE EFFECT` | Command | 1 | number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `DELETE FILE` | Command | 1 | string -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `DELETE ICON` | Command | 1 | number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `DELETE IMAGE` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `DELETE LIGHT` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `DELETE LIGHT MAP LIGHTS` | Command | 0 | none -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `DELETE MATRIX` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `DELETE MATRIX4` | Command | 1 | number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `DELETE MEMBLOCK` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `DELETE MEMBLOCK FROM OBJECT` | Command | 1 | number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `DELETE MEMORY` | Command | 1 | number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `DELETE MESH` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `DELETE MESH FROM VERTEXDATA` | Command | 4 | number, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `DELETE MUSIC` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `DELETE OBJECT` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `DELETE OBJECT COLLISION BOX` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `DELETE OBJECTS` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `DELETE PARTICLES` | Command | 1 | number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `DELETE PIXEL SHADER` | Command | 1 | number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `DELETE SOUND` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `DELETE SPRITE` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `DELETE STATIC OBJECT` | Command | 1 | number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `DELETE STATIC OBJECTS` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `DELETE TERRAIN` | Command | 1 | number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `DELETE VECTOR2` | Command | 1 | number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `DELETE VECTOR3` | Command | 1 | number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `DELETE VECTOR4` | Command | 1 | number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `DELETE VERTEX SHADER` | Command | 1 | number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `DESTROY TERRAIN` | Command | 0 | none -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `DETACH OBJECT FROM STATIC` | Command | 1 | number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `DIR` | Command | 0 | none -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `DISABLE ESCAPEKEY` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `DISABLE OBJECT ZBIAS` | Command | 1 | number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `DISABLE OBJECT ZDEPTH` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `DISABLE OBJECT ZREAD` | Command | 1 | number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `DISABLE OBJECT ZWRITE` | Command | 1 | number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `DISABLE SYSTEMKEYS` | Command | 0 | none -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `DIVIDE MATRIX4` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `DIVIDE VECTOR2` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `DIVIDE VECTOR3` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `DIVIDE VECTOR4` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `DOT` | Command | 2-3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `DRAW SPRITES FIRST` | Command | 0 | none -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `DRAW SPRITES LAST` | Command | 0 | none -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `DRAW TO BACK` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `DRAW TO CAMERA` | Command | 0 | none -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `DRAW TO FRONT` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `DRAW TO SCREEN` | Command | 0 | none -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `DRIVELIST` | Command | 0 | none -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ELLIPSE` | Command | 4 | number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `EMPTY ARRAY` | Command | 1 | any array -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `EMPTY CHECKLIST` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `ENABLE ESCAPEKEY` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `ENABLE OBJECT ZBIAS` | Command | 3 | number, number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ENABLE OBJECT ZDEPTH` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `ENABLE OBJECT ZREAD` | Command | 1 | number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `ENABLE OBJECT ZWRITE` | Command | 1 | number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `ENABLE SYSTEMKEYS` | Command | 0 | none -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `EXCLUDE FROM IMAGEBLOCK` | Command | 1 | number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `EXCLUDE LIMB OFF` | Command | 2 | number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `EXCLUDE LIMB ON` | Command | 2 | number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `EXCLUDE OBJECT OFF` | Command | 1 | number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `EXCLUDE OBJECT ON` | Command | 1 | number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `EXECUTE FILE` | Command | 3-4 | string, string, string, number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `EXIT PROMPT` | Command | 2 | string, string -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `FADE BITMAP` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `FADE OBJECT` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `FASTSYNC` | Command | 0-1 | number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `FILL MATRIX` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `FILL MEMORY` | Command | 3 | number, number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `FIND FIRST` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `FIND NEXT` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `FIX OBJECT PIVOT` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `FLIP BITMAP` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `FLIP SPRITE` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `FLUSH LIGHT MAPS` | Command | 0 | none -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `FLUSH VIDEO MEMORY` | Command | 0 | none -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `FOG COLOR` | Command | 1, 3 | number -> void; number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `FOG DISTANCE` | Command | 1-2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `FOG OFF` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `FOG ON` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `FORCE ANGLE` | Command | 3 | number, number, number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `FORCE AUTO CENTER OFF` | Command | 0 | none -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `FORCE AUTO CENTER ON` | Command | 0 | none -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `FORCE CHAINSAW` | Command | 2 | number, number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `FORCE DOWN` | Command | 1 | number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `FORCE IMPACT` | Command | 2 | number, number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `FORCE LEFT` | Command | 1 | number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `FORCE NO EFFECT` | Command | 0 | none -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `FORCE RIGHT` | Command | 1 | number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `FORCE SHOOT` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `FORCE UP` | Command | 1 | number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `FORCE WATER EFFECT` | Command | 2 | number, number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `FREE NET GAME` | Command | 0 | none -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `FREE NET PLAYER` | Command | 1 | number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `FTP CONNECT` | Command | 3-4 | string, string, string, number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `FTP DELETE FILE` | Command | 1 | string -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `FTP DISCONNECT` | Command | 0-1 | number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `FTP FIND FIRST` | Command | 0 | none -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `FTP FIND NEXT` | Command | 0 | none -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `FTP GET FILE` | Command | 1, 3 | string -> void; string, string, number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `FTP PROCEED` | Command | 0 | none -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `FTP PUT FILE` | Command | 1 | string -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `FTP SET DIR` | Command | 1 | string -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `FTP TERMINATE` | Command | 0 | none -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `GET IMAGE` | Command | 5-6 | number, number, number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `GET NET MESSAGE` | Command | 0 | none -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `GET VR CONTROLLER POSITION` | Command | 2 | integer, integer -> void | Implemented | OpenDBC extension | Shared OpenDBC VR addition; see docs/vr.md for its observable contract. |
| `GET VR CONTROLLER ROTATION` | Command | 2 | integer, integer -> void | Implemented | OpenDBC extension | Shared OpenDBC VR addition; see docs/vr.md for its observable contract. |
| `GET VR HEAD POSITION` | Command | 1 | integer -> void | Implemented | OpenDBC extension | Shared OpenDBC VR addition; see docs/vr.md for its observable contract. |
| `GET VR ROTATION` | Command | 1 | integer -> void | Implemented | OpenDBC extension | Shared OpenDBC VR addition; see docs/vr.md for its observable contract. |
| `GHOST MATRIX OFF` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `GHOST MATRIX ON` | Command | 1-2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `GHOST OBJECT OFF` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `GHOST OBJECT ON` | Command | 1-2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `GHOST PARTICLES OFF` | Command | 1 | number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `GHOST PARTICLES ON` | Command | 2-3 | number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `GLUE OBJECT TO LIMB` | Command | 3-4 | number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `HERMITE VECTOR2` | Command | 6 | number, number, number, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `HERMITE VECTOR3` | Command | 6 | number, number, number, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `HERMITE VECTOR4` | Command | 6 | number, number, number, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `HIDE ALL SPRITES` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `HIDE LIGHT` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `HIDE LIMB` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `HIDE MOUSE` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `HIDE OBJECT` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `HIDE OBJECT BOUNDS` | Command | 1 | number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `HIDE PARTICLES` | Command | 1 | number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `HIDE SPRITE` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `HIDE WINDOW` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `HTTP CONNECT` | Command | 1 | number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `HTTP DISCONNECT` | Command | 0 | none -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `INK` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `INPUT` | Command | 2 | any, any output -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `INSTANCE OBJECT` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `INVERSE MATRIX4` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `JOIN NET GAME` | Command | 2 | number, number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `LINE` | Command | 4 | number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `LINEAR INTERPOLATE VECTOR2` | Command | 4 | number, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `LINEAR INTERPOLATE VECTOR3` | Command | 4 | number, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `LINEAR INTERPOLATE VECTOR4` | Command | 4 | number, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `LINK LIMB` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `LOAD 3DSOUND` | Command | 2-4 | string, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `LOAD ANIMATION` | Command | 2 | number, number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `LOAD ARRAY` | Command | 2 | string, any array -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `LOAD BITMAP` | Command | 1-2 | string, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `LOAD BSP` | Command | 2 | string, string -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `LOAD CDMUSIC` | Command | 2 | number, number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `LOAD DLL` | Command | 2 | number, number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `LOAD DVD ANIMATION` | Command | 1 | number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `LOAD EFFECT` | Command | 3-4 | string, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `LOAD IMAGE` | Command | 2-4 | string, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `LOAD MESH` | Command | 2 | string, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `LOAD MUSIC` | Command | 2 | string, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `LOAD OBJECT` | Command | 2-4 | string, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `LOAD SOUND` | Command | 2-5 | string, number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `LOAD STATIC OBJECTS` | Command | 2 | string, number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `LOAD TERRAIN` | Command | 2 | number, number -> void | Unsupported (DBC2043) | DBPro snapshot, legacy unverified | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `LOCK BACKBUFFER` | Command | 0 | none -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `LOCK OBJECT OFF` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `LOCK OBJECT ON` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `LOCK PIXELS` | Command | 0 | none -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `LOCK VERTEXDATA FOR LIMB` | Command | 2-3 | number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `LOCK VERTEXDATA FOR MESH` | Command | 1 | number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `LOOP ANIMATION` | Command | 1, 6 | number -> void; number, number, number, number, number, number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `LOOP MUSIC` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `LOOP OBJECT` | Command | 1-3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `LOOP SOUND` | Command | 1-4 | number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MAKE ARRAY FROM MEMBLOCK` | Command | 2 | number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `MAKE BITMAP FROM MEMBLOCK` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MAKE CAMERA` | Command | 1 | number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MAKE DIRECTORY` | Command | 1 | string -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MAKE EMITTER` | Command | 2 | number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `MAKE FILE` | Command | 1 | string -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MAKE FILE FROM MEMBLOCK` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MAKE FIRE PARTICLES` | Command | 9 | number, number, number, number, number, number, number, number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `MAKE IMAGE FROM MEMBLOCK` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MAKE LIGHT` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MAKE MATRIX` | Command | 5 | number, number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MAKE MATRIX4` | Command | 1 | number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MAKE MEMBLOCK` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MAKE MEMBLOCK FROM ARRAY` | Command | 2 | number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `MAKE MEMBLOCK FROM BITMAP` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MAKE MEMBLOCK FROM FILE` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MAKE MEMBLOCK FROM IMAGE` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MAKE MEMBLOCK FROM MESH` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MAKE MEMBLOCK FROM SOUND` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MAKE MESH FROM MEMBLOCK` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MAKE MESH FROM OBJECT` | Command | 2-3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MAKE OBJECT` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MAKE OBJECT BOX` | Command | 4 | number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MAKE OBJECT COLLISION BOX` | Command | 8 | number, number, number, number, number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MAKE OBJECT CONE` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MAKE OBJECT CUBE` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MAKE OBJECT CYLINDER` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MAKE OBJECT FROM LIMB` | Command | 3-4 | number, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MAKE OBJECT PLAIN` | Command | 3-4 | number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MAKE OBJECT PLANE` | Command | 3-4 | number, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MAKE OBJECT SPHERE` | Command | 2, 4 | number, number -> void; number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MAKE OBJECT TERRAIN` | Command | 1 | number -> void | Unsupported (DBC2043) | DBPro snapshot, legacy unverified | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `MAKE OBJECT TRIANGLE` | Command | 10 | number, number, number, number, number, number, number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MAKE PARTICLES` | Command | 4 | number, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MAKE SNOW PARTICLES` | Command | 9 | number, number, number, number, number, number, number, number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `MAKE SOUND FROM MEMBLOCK` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MAKE STATIC COLLISION BOX` | Command | 6 | number, number, number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MAKE STATIC LIMB` | Command | 6 | number, number, number, number, number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `MAKE STATIC OBJECT` | Command | 5 | number, number, number, number, number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `MAKE TERRAIN` | Command | 2 | number, string -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MAKE VECTOR2` | Command | 1 | number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MAKE VECTOR3` | Command | 1 | number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MAKE VECTOR4` | Command | 1 | number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MAXIMIZE VECTOR2` | Command | 3 | number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MAXIMIZE VECTOR3` | Command | 3 | number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MAXIMIZE VECTOR4` | Command | 3 | number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MAXIMIZE WINDOW` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MINIMIZE VECTOR2` | Command | 3 | number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MINIMIZE VECTOR3` | Command | 3 | number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MINIMIZE VECTOR4` | Command | 3 | number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MINIMIZE WINDOW` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MIRROR BITMAP` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MIRROR SPRITE` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MOVE CAMERA` | Command | 1-2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MOVE FILE` | Command | 2 | string, string -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MOVE OBJECT` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MOVE OBJECT DOWN` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MOVE OBJECT LEFT` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MOVE OBJECT RIGHT` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MOVE OBJECT UP` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MOVE SPRITE` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MULTIPLY MATRIX4` | Command | 2-3 | number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MULTIPLY VECTOR2` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MULTIPLY VECTOR3` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `MULTIPLY VECTOR4` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `NET MESSAGE BITMAP` | Command | 1 | number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `NET MESSAGE IMAGE` | Command | 1 | number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `NET MESSAGE MEMBLOCK` | Command | 1 | number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `NET MESSAGE MESH` | Command | 1 | number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `NET MESSAGE SOUND` | Command | 1 | number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `NEXT ARRAY INDEX` | Command | 1 | any array -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `NORMALIZE VECTOR2` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `NORMALIZE VECTOR3` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `NORMALIZE VECTOR4` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `OFFSET LIMB` | Command | 5-6 | number, number, number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `OFFSET SPRITE` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `OPEN IMAGEBLOCK` | Command | 2 | number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `OPEN TO READ` | Command | 2 | number, string -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `OPEN TO WRITE` | Command | 2 | number, string -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `PASTE IMAGE` | Command | 3-4 | number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `PASTE SPRITE` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `PAUSE ANIMATION` | Command | 1 | number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `PAUSE MUSIC` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `PAUSE SOUND` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `PERFORM CHECKLIST FOR CONTROL DEVICES` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `PERFORM CHECKLIST FOR DISPLAY MODES` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `PERFORM CHECKLIST FOR DRIVES` | Command | 0 | none -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `PERFORM CHECKLIST FOR EFFECT ERRORS` | Command | 0-1 | number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `PERFORM CHECKLIST FOR EFFECT VALUES` | Command | 1 | number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `PERFORM CHECKLIST FOR FILES` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `PERFORM CHECKLIST FOR FONTS` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `PERFORM CHECKLIST FOR GRAPHICS CARDS` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `PERFORM CHECKLIST FOR IMAGEBLOCK FILES` | Command | 0 | none -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `PERFORM CHECKLIST FOR NET CONNECTIONS` | Command | 0 | none -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `PERFORM CHECKLIST FOR NET PLAYERS` | Command | 0 | none -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `PERFORM CHECKLIST FOR NET SESSIONS` | Command | 0 | none -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `PERFORM CHECKLIST FOR OBJECT LIMBS` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `PERFORM CHECKLIST FOR ONSCREEN OBJECTS` | Command | 1 | number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `PERFORM CSG CLIP` | Command | 2 | number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `PERFORM CSG DIFFERENCE` | Command | 2 | number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `PERFORM CSG DIFFERENCE ON VERTEXDATA` | Command | 1 | number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `PERFORM CSG INTERSECTION` | Command | 2 | number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `PERFORM CSG INTERSECTION ON VERTEXDATA` | Command | 1 | number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `PERFORM CSG UNION` | Command | 2 | number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `PERFORM CSG UNION ON VERTEXDATA` | Command | 1 | number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `PICK OBJECT` | Command | 4 | number, number, number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `PICK SCREEN` | Command | 3 | number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `PITCH CAMERA DOWN` | Command | 1-2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `PITCH CAMERA UP` | Command | 1-2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `PITCH OBJECT DOWN` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `PITCH OBJECT UP` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `PLACE ANIMATION` | Command | 5 | number, number, number, number, number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `PLAY ANIMATION` | Command | 1, 3, 5, 6 | number -> void; number, number, number -> void; number, number, number, number, number -> void; number, number, number, number, number, number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `PLAY ANIMATION TO IMAGE` | Command | 6 | number, number, number, number, number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `PLAY MUSIC` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `PLAY OBJECT` | Command | 1-3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `PLAY SOUND` | Command | 1-2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `PLAY SPRITE` | Command | 4 | number, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `POINT CAMERA` | Command | 3-4 | number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `POINT LIGHT` | Command | 4 | number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `POINT OBJECT` | Command | 4 | number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `POSITION CAMERA` | Command | 2-4 | number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `POSITION LIGHT` | Command | 2, 4 | number, number -> void; number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `POSITION LISTENER` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `POSITION MATRIX` | Command | 2, 4 | number, number -> void; number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `POSITION MOUSE` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `POSITION OBJECT` | Command | 4 | number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `POSITION PARTICLE EMISSIONS` | Command | 4 | number, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `POSITION PARTICLES` | Command | 2, 4 | number, number -> void; number, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `POSITION SOUND` | Command | 4 | number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `POSITION TERRAIN` | Command | 4 | number, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `PREPARE MATRIX TEXTURE` | Command | 4 | number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `PREVIOUS ARRAY INDEX` | Command | 1 | any array -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `PRINT` | Command | 0-99 | any... -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `PRINTC` | Command | 1 | any -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `PROCESS BSP COLLISION` | Command | 1 | number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `PROJECT VECTOR3` | Command | 5 | number, number, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `PROJECTION MATRIX4` | Command | 1 | number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `RANDOMIZE` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `RANDOMIZE MATRIX` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `READ` | Command | 1-99 | any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output, any output -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `READ BYTE` | Command | 2 | number, integer output -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `READ DIRBLOCK` | Command | 2 | number, string -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `READ FILE` | Command | 2 | number, integer output -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `READ FILEBLOCK` | Command | 2 | number, string -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `READ FLOAT` | Command | 2 | number, real output -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `READ LONG` | Command | 2 | number, integer output -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `READ MEMBLOCK` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `READ STRING` | Command | 2 | number, string output -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `READ WORD` | Command | 2 | number, integer output -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `RECENTER VR CAMERA` | Command | 0 | none -> void | Implemented | OpenDBC extension | Shared OpenDBC VR addition; see docs/vr.md for its observable contract. |
| `RECORD SOUND` | Command | 1-2 | number, number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `REDUCE MESH` | Command | 6 | number, number, number, number, number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `REMOVE FROM QUEUE` | Command | 1 | any array -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `REMOVE FROM STACK` | Command | 1 | any array -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `REMOVE LIMB` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `RENAME FILE` | Command | 2 | string, string -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `RESTORE` | Command | 1 | any... -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `RESTORE WINDOW` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `RESUME ANIMATION` | Command | 1 | number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `RESUME MUSIC` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `RESUME SOUND` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `ROLL CAMERA LEFT` | Command | 1-2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `ROLL CAMERA RIGHT` | Command | 1-2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `ROLL OBJECT LEFT` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `ROLL OBJECT RIGHT` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `ROTATE CAMERA` | Command | 2-4 | number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `ROTATE LIGHT` | Command | 2, 4 | number, number -> void; number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `ROTATE LIMB` | Command | 5-6 | number, number, number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `ROTATE LISTENER` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `ROTATE OBJECT` | Command | 4 | number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `ROTATE PARTICLES` | Command | 2, 4 | number, number -> void; number, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `ROTATE SPRITE` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `ROTATE X MATRIX4` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `ROTATE Y MATRIX4` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `ROTATE YPR MATRIX4` | Command | 4 | number, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `ROTATE Z MATRIX4` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SAVE ARRAY` | Command | 2 | string, any array -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SAVE BITMAP` | Command | 1-2 | string, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SAVE IMAGE` | Command | 2-3 | string, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SAVE MESH` | Command | 2 | string, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SAVE OBJECT` | Command | 2 | string, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SAVE SOUND` | Command | 2 | string, number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SAVE STATIC OBJECTS` | Command | 1 | string -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SAVE TERRAIN` | Command | 2 | number, number -> void | Unsupported (DBC2043) | DBPro snapshot, legacy unverified | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SCALE LIMB` | Command | 5-6 | number, number, number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SCALE LIMB TEXTURE` | Command | 4-5 | number, number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SCALE LISTENER` | Command | 1 | number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SCALE MATRIX4` | Command | 4 | number, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SCALE OBJECT` | Command | 4 | number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SCALE OBJECT TEXTURE` | Command | 3-4 | number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SCALE SPRITE` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SCALE VECTOR2` | Command | 3 | number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SCALE VECTOR3` | Command | 3 | number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SCALE VECTOR4` | Command | 3 | number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SCROLL BACKDROP` | Command | 2-3 | number, number, number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SCROLL LIMB TEXTURE` | Command | 4-5 | number, number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SCROLL OBJECT TEXTURE` | Command | 3-4 | number, number, number, number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SEND NET MESSAGE BITMAP` | Command | 3 | number, number, number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SEND NET MESSAGE FLOAT` | Command | 2 | number, number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SEND NET MESSAGE IMAGE` | Command | 3 | number, number, number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SEND NET MESSAGE INTEGER` | Command | 2 | number, number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SEND NET MESSAGE MEMBLOCK` | Command | 2-3 | number, number, number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SEND NET MESSAGE MESH` | Command | 3 | number, number, number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SEND NET MESSAGE SOUND` | Command | 3 | number, number, number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SEND NET MESSAGE STRING` | Command | 2 | number, number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SET ALPHA MAPPING ON` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET AMBIENT LIGHT` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET ANIMATION SPEED` | Command | 2 | number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SET ANIMATION VOLUME` | Command | 2 | number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SET BITMAP FORMAT` | Command | 1 | number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET BLEND MAPPING ON` | Command | 3, 5 | number, number, number -> void; number, number, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET BSP CAMERA` | Command | 1 | number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET BSP CAMERA COLLISION` | Command | 4 | number, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET BSP CAMERA COLLISION RADIUS` | Command | 5 | number, number, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET BSP COLLISION HEIGHT ADJUSTMENT` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET BSP COLLISION OFF` | Command | 1 | number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET BSP COLLISION THRESHHOLD` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET BSP MULTITEXTURING OFF` | Command | 0 | none -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SET BSP MULTITEXTURING ON` | Command | 0 | none -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SET BSP OBJECT COLLISION` | Command | 4 | number, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET BSP OBJECT COLLISION RADIUS` | Command | 5 | number, number, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET BUMP MAPPING ON` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET CAMERA ASPECT` | Command | 1-2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET CAMERA CLIP` | Command | 8 | number, number, number, number, number, number, number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SET CAMERA FOV` | Command | 1-2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET CAMERA RANGE` | Command | 2-3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET CAMERA ROTATION XYZ` | Command | 0-1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET CAMERA ROTATION ZYX` | Command | 0-1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET CAMERA TO FOLLOW` | Command | 8-9 | number, number, number, number, number, number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET CAMERA TO IMAGE` | Command | 4-6 | number, number, number, number, number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SET CAMERA TO OBJECT ORIENTATION` | Command | 1-2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET CAMERA VIEW` | Command | 4-5 | number, number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET CAMERAS TO STEREOSCOPIC` | Command | 5 | number, number, number, number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SET CARTOON SHADING ON` | Command | 3 | number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET CONTROL DEVICE` | Command | 1-2 | string, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET CONTROL DEVICE INDEX` | Command | 1 | number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET CUBE MAPPING ON` | Command | 7 | number, number, number, number, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET CURRENT BITMAP` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET CURRENT CAMERA` | Command | 1 | number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET CURSOR` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET DETAIL MAPPING ON` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET DIR` | Command | 1 | string -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET DIRECTIONAL LIGHT` | Command | 4 | number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET DISPLAY MODE` | Command | 3, 4, 6 | number, number, number -> void; number, number, number, number -> void; number, number, number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET DVD CHAPTER` | Command | 3 | number, number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SET EFFECT CONSTANT BOOLEAN` | Command | 3 | number, string, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET EFFECT CONSTANT FLOAT` | Command | 3 | number, string, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET EFFECT CONSTANT INTEGER` | Command | 3 | number, string, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET EFFECT CONSTANT MATRIX` | Command | 3 | number, string, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET EFFECT CONSTANT VECTOR` | Command | 3 | number, string, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET EFFECT ON` | Command | 3 | number, string, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET EFFECT TECHNIQUE` | Command | 2 | number, string -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET EFFECT TRANSPOSE` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET GAMMA` | Command | 3 | number, number, number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SET GLOBAL COLLISION OFF` | Command | 0 | none -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SET GLOBAL COLLISION ON` | Command | 0 | none -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SET GLOBAL OBJECT CREATION` | Command | 1 | number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SET GLOBAL SHADOW COLOR` | Command | 4 | number, number, number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SET GLOBAL SHADOW SHADES` | Command | 1 | number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SET GLOBAL SHADOWS OFF` | Command | 0 | none -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SET GLOBAL SHADOWS ON` | Command | 0 | none -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SET GRAPHICS CARD` | Command | 1 | number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SET IDENTITY MATRIX4` | Command | 1 | number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET IMAGE COLORKEY` | Command | 3 | number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET INDEXDATA` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET JOYSTICK DEADZONE` | Command | 1 | number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET KEY STATE TOGGLE` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET LIGHT MAPPING ON` | Command | 2-3 | number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET LIGHT RANGE` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET LIGHT TO OBJECT ORIENTATION` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET LIGHT TO OBJECT POSITION` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET LIMB EFFECT` | Command | 3 | number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET LIMB NORMALS` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET LIMB SMOOTHING` | Command | 3 | number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET MATRIX` | Command | 8 | number, number, number, number, number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET MATRIX HEIGHT` | Command | 4 | number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET MATRIX NORMAL` | Command | 6 | number, number, number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET MATRIX PRIORITY` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET MATRIX TEXTURE` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET MATRIX TILE` | Command | 4 | number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET MATRIX TRIM` | Command | 3 | number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET MATRIX WIREFRAME OFF` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET MATRIX WIREFRAME ON` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET MUSIC SPEED` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET MUSIC VOLUME` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET NET CONNECTION` | Command | 1-2 | number, number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SET NORMALIZATION OFF` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET NORMALIZATION ON` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET OBJECT` | Command | 4-8 | number, number, number, number, number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET OBJECT AMBIENCE` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET OBJECT AMBIENT` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET OBJECT COLLISION OFF` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET OBJECT COLLISION ON` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET OBJECT COLLISION TO BOXES` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET OBJECT COLLISION TO POLYGONS` | Command | 1 | number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SET OBJECT COLLISION TO SPHERES` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET OBJECT CULL` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET OBJECT DIFFUSE` | Command | 2-3 | number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET OBJECT EFFECT` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET OBJECT EMISSIVE` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET OBJECT FILTER` | Command | 2-3 | number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET OBJECT FOG` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET OBJECT FOV` | Command | 2 | number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SET OBJECT FRAME` | Command | 2-3 | number, number, number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SET OBJECT INTERPOLATION` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET OBJECT LIGHT` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET OBJECT MASK` | Command | 2 | number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SET OBJECT NORMALS` | Command | 1 | number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET OBJECT RADIUS` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET OBJECT ROTATION XYZ` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET OBJECT ROTATION ZYX` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET OBJECT SMOOTHING` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET OBJECT SPECULAR` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET OBJECT SPECULAR POWER` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET OBJECT SPEED` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET OBJECT TEXTURE` | Command | 3-4 | number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET OBJECT TO CAMERA ORIENTATION` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET OBJECT TO OBJECT ORIENTATION` | Command | 2-3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET OBJECT TRANSPARENCY` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET OBJECT WIREFRAME` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET PARTICLE CHAOS` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET PARTICLE EMISSIONS` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET PARTICLE FLOOR` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET PARTICLE GRAVITY` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET PARTICLE LIFE` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET PARTICLE SPEED` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET PARTICLE VELOCITY` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET PIXEL SHADER OFF` | Command | 1 | number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SET PIXEL SHADER ON` | Command | 2 | number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SET PIXEL SHADER TEXTURE` | Command | 3 | number, number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SET POINT LIGHT` | Command | 4 | number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET RAINBOW SHADING ON` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET REFLECTION SHADING ON` | Command | 1 | number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET SCREEN EFFECT` | Command | 1 | number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SET SHADING OFF` | Command | 1 | number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET SHADOW LIGHT` | Command | 5 | number, number, number, number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SET SHADOW POSITION` | Command | 4 | number, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET SHADOW SHADING OFF` | Command | 1 | number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET SHADOW SHADING ON` | Command | 1, 4 | number -> void; number, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET SOUND PAN` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET SOUND SPEED` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET SOUND VOLUME` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET SPHERE MAPPING ON` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET SPOT LIGHT` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET SPRITE` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET SPRITE ALPHA` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET SPRITE DIFFUSE` | Command | 4 | number, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET SPRITE FRAME` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET SPRITE IMAGE` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET SPRITE PRIORITY` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET SPRITE RESIZE` | Command | 1, 3 | number -> void; number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET SPRITE TEXTURE COORD` | Command | 4 | number, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET STATIC OBJECTS WIREFRAME OFF` | Command | 0 | none -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SET STATIC OBJECTS WIREFRAME ON` | Command | 0 | none -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SET STATIC PORTALS OFF` | Command | 0 | none -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SET STATIC PORTALS ON` | Command | 0 | none -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SET STATIC SCORCH` | Command | 3 | number, number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SET STATIC UNIVERSE` | Command | 3 | number, number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SET TERRAIN HEIGHTMAP` | Command | 2 | number, number -> void | Unsupported (DBC2043) | DBPro snapshot, legacy unverified | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SET TERRAIN LIGHT` | Command | 8 | number, number, number, number, number, number, number, number -> void | Unsupported (DBC2043) | DBPro snapshot, legacy unverified | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SET TERRAIN SCALE` | Command | 4 | number, number, number, number -> void | Unsupported (DBC2043) | DBPro snapshot, legacy unverified | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SET TERRAIN SPLIT` | Command | 2 | number, number -> void | Unsupported (DBC2043) | DBPro snapshot, legacy unverified | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SET TERRAIN TEXTURE` | Command | 3 | number, number, number -> void | Unsupported (DBC2043) | DBPro snapshot, legacy unverified | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SET TERRAIN TILING` | Command | 3 | number, number, number -> void | Unsupported (DBC2043) | DBPro snapshot, legacy unverified | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SET TEXT FONT` | Command | 1-2 | string, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET TEXT OPAQUE` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET TEXT SIZE` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET TEXT TO BOLD` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET TEXT TO BOLDITALIC` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET TEXT TO ITALIC` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET TEXT TO NORMAL` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET TEXT TRANSPARENT` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET TEXTURE MD3` | Command | 7 | number, number, number, number, number, number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SET TRACKING SPACE` | Command | 1 | integer -> void | Implemented | OpenDBC extension | Shared OpenDBC VR addition; see docs/vr.md for its observable contract. |
| `SET VECTOR2` | Command | 3 | number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET VECTOR3` | Command | 4 | number, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET VECTOR3 TO CAMERA POSITION` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET VECTOR3 TO CAMERA ROTATION` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET VECTOR3 TO LIGHT POSITION` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET VECTOR3 TO LIGHT ROTATION` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET VECTOR3 TO MATRIX POSITION` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET VECTOR3 TO PARTICLES POSITION` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET VECTOR3 TO PARTICLES ROTATION` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET VECTOR4` | Command | 5 | number, number, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET VERTEX SHADER MATRIX` | Command | 4 | number, number, number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SET VERTEX SHADER OFF` | Command | 1 | number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SET VERTEX SHADER ON` | Command | 2 | number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SET VERTEX SHADER STREAM` | Command | 4 | number, number, number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SET VERTEX SHADER STREAMCOUNT` | Command | 2 | number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SET VERTEX SHADER VECTOR` | Command | 4 | number, number, number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SET VERTEXDATA DIFFUSE` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET VERTEXDATA NORMALS` | Command | 4 | number, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET VERTEXDATA POSITION` | Command | 4 | number, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET VERTEXDATA UV` | Command | 3-4 | number, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET VR CAMERA HEIGHT` | Command | 1 | real -> void | Implemented | OpenDBC extension | Shared OpenDBC VR addition; see docs/vr.md for its observable contract. |
| `SET VR ENABLED` | Command | 1 | integer -> void | Implemented | OpenDBC extension | Shared OpenDBC VR addition; see docs/vr.md for its observable contract. |
| `SET VR PREVIEW MODE` | Command | 1 | integer -> void | Implemented | OpenDBC extension | Shared OpenDBC VR addition; see docs/vr.md for its observable contract. |
| `SET WINDOW LAYOUT` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET WINDOW OFF` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET WINDOW ON` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET WINDOW POSITION` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET WINDOW SIZE` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SET WINDOW TITLE` | Command | 1 | string -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SHIFT MATRIX DOWN` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SHIFT MATRIX LEFT` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SHIFT MATRIX RIGHT` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SHIFT MATRIX UP` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SHOW ALL SPRITES` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SHOW LIGHT` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SHOW LIMB` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SHOW MOUSE` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SHOW OBJECT` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SHOW OBJECT BOUNDS` | Command | 1-2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SHOW PARTICLES` | Command | 1 | number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SHOW SPRITE` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SHOW WINDOW` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SIZE SPRITE` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SKIP BYTES` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SLEEP` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SPRITE` | Command | 4 | number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `STOP ANIMATION` | Command | 1 | number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `STOP EXECUTABLE` | Command | 1 | number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `STOP MUSIC` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `STOP OBJECT` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `STOP RECORDING SOUND` | Command | 0 | none -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `STOP SOUND` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `STRETCH SPRITE` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SUBTRACT MATRIX4` | Command | 3 | number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SUBTRACT VECTOR2` | Command | 3 | number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SUBTRACT VECTOR3` | Command | 3 | number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SUBTRACT VECTOR4` | Command | 3 | number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SUSPEND FOR KEY` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SUSPEND FOR MOUSE` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SYNC` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SYNC MASK` | Command | 1 | number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `SYNC OFF` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SYNC ON` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SYNC RATE` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `SYNC SLEEP` | Command | 1 | number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `TEXT` | Command | 3 | number, number, string -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `TEXTURE LIMB` | Command | 3-4 | number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `TEXTURE OBJECT` | Command | 2-4 | number, number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `TEXTURE SCREEN` | Command | 1-2 | number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `TEXTURE TERRAIN` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `TRANSFORM COORDS VECTOR2` | Command | 3 | number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `TRANSFORM COORDS VECTOR3` | Command | 3 | number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `TRANSFORM NORMALS VECTOR3` | Command | 3 | number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `TRANSFORM VECTOR4` | Command | 3 | number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `TRANSLATE MATRIX4` | Command | 4 | number, number, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `TRANSPOSE MATRIX4` | Command | 2 | number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `TURN CAMERA LEFT` | Command | 1-2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `TURN CAMERA RIGHT` | Command | 1-2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `TURN OBJECT LEFT` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `TURN OBJECT RIGHT` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `UNDIM` | Command | 1 | any array -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `UNGLUE OBJECT` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `UNLOCK BACKBUFFER` | Command | 0 | none -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `UNLOCK PIXELS` | Command | 0 | none -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `UNLOCK VERTEXDATA` | Command | 0 | none -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `UPDATE MATRIX` | Command | 1 | number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `UPDATE TERRAIN` | Command | 0 | none -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `VIEW MATRIX4` | Command | 1 | number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `WAIT` | Command | 1 | number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `WAIT KEY` | Command | 0 | none -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `WAIT MOUSE` | Command | 0 | none -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `WINDOW TO BACK` | Command | 0-1 | number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `WINDOW TO FRONT` | Command | 0-1 | string -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `WORLD MATRIX4` | Command | 1 | number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `WRITE BYTE` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `WRITE BYTE TO FILE` | Command | 3 | string, number, number -> void | Implemented | DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `WRITE DIRBLOCK` | Command | 2 | number, string -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `WRITE FILE` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `WRITE FILEBLOCK` | Command | 2 | number, string -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `WRITE FILEMAP STRING` | Command | 2 | number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `WRITE FILEMAP VALUE` | Command | 2 | number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `WRITE FLOAT` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `WRITE LONG` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `WRITE MEMBLOCK` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `WRITE MEMBLOCK BYTE` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `WRITE MEMBLOCK DWORD` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `WRITE MEMBLOCK FLOAT` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `WRITE MEMBLOCK WORD` | Command | 3 | number, number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `WRITE STRING` | Command | 2 | number, string -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `WRITE STRING TO REGISTRY` | Command | 3 | number, number, number -> void | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `WRITE TO CLIPBOARD` | Command | 1 | string -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `WRITE TO REGISTRY` | Command | 3 | number, number, number -> void | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `WRITE WORD` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `XROTATE CAMERA` | Command | 1-2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `XROTATE OBJECT` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `YROTATE CAMERA` | Command | 1-2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `YROTATE OBJECT` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `ZROTATE CAMERA` | Command | 1-2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `ZROTATE OBJECT` | Command | 2 | number, number -> void | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional command implemented with an observable contract. |
| `#CONSTANT` | Directive | 0 | grammar form | Implemented | DBPro snapshot | DarkBASIC Professional directive implemented with an observable contract. |
| `#INCLUDE` | Directive | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional directive implemented with an observable contract. |
| `ABS` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `ACOS` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `ALPHA AVAILABLE` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `ALPHABLENDING AVAILABLE` | Function | 0 | none -> real | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `ALPHACOMPARISON AVAILABLE` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `AMBIENT LIGHT` | Function | 0 | none -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `ANIMATION EXIST` | Function | 1 | number -> real | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `ANIMATION HEIGHT` | Function | 1 | number -> real | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `ANIMATION LOOPING` | Function | 1 | number -> real | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `ANIMATION PAUSED` | Function | 1 | number -> real | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `ANIMATION PLAYING` | Function | 1 | number -> real | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `ANIMATION POSITION X` | Function | 1 | number -> real | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `ANIMATION POSITION Y` | Function | 1 | number -> real | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `ANIMATION SPEED` | Function | 1 | number -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `ANIMATION VOLUME` | Function | 1 | number -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `ANIMATION WIDTH` | Function | 1 | number -> real | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `ANISTROPICFILTERING AVAILABLE` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `ANTIALIAS AVAILABLE` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `APPEND$` | Function | 2 | string, string -> string | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `APPNAME$` | Function | 0 | none -> string | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `ARRAY COUNT` | Function | 1 | any array -> integer | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `ARRAY INDEX VALID` | Function | 1 | any array -> integer | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `ASC` | Function | 1 | string -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `ASIN` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `ATAN` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `ATANFULL` | Function | 2 | number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `BIN$` | Function | 1 | number -> string | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `BITMAP DEPTH` | Function | 0-1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `BITMAP EXIST` | Function | 0-1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `BITMAP FLIPPED` | Function | 0-1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `BITMAP HEIGHT` | Function | 0-1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `BITMAP MIRRORED` | Function | 0-1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `BITMAP WIDTH` | Function | 0-1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `BLITSYSTOLOCAL AVAILABLE` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `BSP COLLISION HIT` | Function | 1 | number -> integer | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `BSP COLLISION X` | Function | 1 | number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `BSP COLLISION Y` | Function | 1 | number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `BSP COLLISION Z` | Function | 1 | number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `CALIBRATEGAMMA AVAILABLE` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `CAMERA ANGLE X` | Function | 0-1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `CAMERA ANGLE Y` | Function | 0-1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `CAMERA ANGLE Z` | Function | 0-1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `CAMERA EXIST` | Function | 1 | number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `CAMERA POSITION X` | Function | 0-1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `CAMERA POSITION Y` | Function | 0-1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `CAMERA POSITION Z` | Function | 0-1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `CAN MAKE FILE` | Function | 1 | number -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `CCW VECTOR2` | Function | 2 | number, number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `CEIL` | Function | 1 | number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `CHECK DISPLAY MODE` | Function | 3, 6 | number, number, number -> integer; number, number, number, number, number, number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `CHECK LIMB LINK` | Function | 2 | number, number -> integer | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `CHECKLIST FVALUE A` | Function | 1 | number -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `CHECKLIST FVALUE B` | Function | 1 | number -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `CHECKLIST FVALUE C` | Function | 1 | number -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `CHECKLIST FVALUE D` | Function | 1 | number -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `CHECKLIST QUANTITY` | Function | 0 | none -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `CHECKLIST STRING$` | Function | 1 | number -> string | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `CHECKLIST VALUE A` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `CHECKLIST VALUE B` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `CHECKLIST VALUE C` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `CHECKLIST VALUE D` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `CHR$` | Function | 1 | number -> string | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `CL$` | Function | 0 | none -> string | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `CLIPANDSCALEPOINTS AVAILABLE` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `CLIPTLVERTS AVAILABLE` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `COLORPERSPECTIVE AVAILABLE` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `COLORWRITEENABLE AVAILABLE` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `COMPARE CASE$` | Function | 2 | string, string -> integer | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `CONTROL DEVICE INDEX` | Function | 0 | none -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `CONTROL DEVICE NAME$` | Function | 0 | none -> string | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `CONTROL DEVICE X` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `CONTROL DEVICE Y` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `CONTROL DEVICE Z` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `CONTROLKEY` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `COS` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `CREATE NET PLAYER` | Function | 1 | number -> real | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `CUBEMAP AVAILABLE` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `CULLCCW AVAILABLE` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `CULLCW AVAILABLE` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `CURRENT BITMAP` | Function | 0 | none -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `CURRENT GRAPHICS CARD$` | Function | 0 | none -> string | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `CURVEANGLE` | Function | 3 | number, number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `CURVEVALUE` | Function | 3 | number, number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `DEFAULT NET GAME` | Function | 4 | number, number, number, number -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `DELETE MATRIX4` | Function | 1 | number -> integer | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `DELETE VECTOR2` | Function | 1 | number -> integer | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `DELETE VECTOR3` | Function | 1 | number -> integer | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `DELETE VECTOR4` | Function | 1 | number -> integer | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `DESKTOP HEIGHT` | Function | 0 | none -> integer | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `DESKTOP WIDTH` | Function | 0 | none -> integer | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `DITHER AVAILABLE` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `DLL CALL EXIST` | Function | 2 | number, number -> real | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `DLL EXIST` | Function | 1 | number -> real | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `DOT PRODUCT VECTOR2` | Function | 2 | number, number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `DOT PRODUCT VECTOR3` | Function | 2 | number, number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `DOWNKEY` | Function | 0 | none -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `EFFECT EXIST` | Function | 1 | number -> integer | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `ENTRY$` | Function | 0-1 | number -> string | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `ESCAPEKEY` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `EXECUTABLE RUNNING` | Function | 1 | number -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `EXECUTE EXECUTABLE` | Function | 3-4 | number, number, number, number -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `EXP` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `FILE END` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `FILE EXIST` | Function | 1 | string -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `FILE OPEN` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `FILE SIZE` | Function | 1 | string -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `FILTERING AVAILABLE` | Function | 0 | none -> real | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `FIND FIRST CHAR$` | Function | 2 | string, string -> integer | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `FIND LAST CHAR$` | Function | 2 | string, string -> integer | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `FIND SUB STRING$` | Function | 2 | string, string -> integer | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `FIRST TOKEN$` | Function | 2 | string, string -> string | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `FLOOR` | Function | 1 | number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `FOG AVAILABLE` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `FOGRANGE AVAILABLE` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `FOGTABLE AVAILABLE` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `FOGVERTEX AVAILABLE` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `FREE STRING$` | Function | 0 | none -> string | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `FULLSCREENGAMMA AVAILABLE` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `GET ARRAY TYPE` | Function | 1 | number -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `GET BACKBUFFER DEPTH` | Function | 0 | none -> real | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `GET BACKBUFFER HEIGHT` | Function | 0 | none -> real | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `GET BACKBUFFER PITCH` | Function | 0 | none -> real | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `GET BACKBUFFER PTR` | Function | 0 | none -> real | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `GET BACKBUFFER WIDTH` | Function | 0 | none -> real | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `GET CLIPBOARD$` | Function | 0 | none -> string | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `GET DATE$` | Function | 0 | none -> string | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `GET DEVICE TYPE` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `GET DIR$` | Function | 0 | none -> string | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `GET DX VERSION$` | Function | 0 | none -> string | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `GET FILE CREATION$` | Function | 0 | none -> string | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `GET FILE DATE$` | Function | 0 | none -> string | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `GET FILE NAME$` | Function | 0 | none -> string | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `GET FILE TYPE` | Function | 0 | none -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `GET FTP DIR$` | Function | 0 | none -> string | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `GET FTP ERROR$` | Function | 0 | none -> string | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `GET FTP FAILURE` | Function | 0 | none -> real | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `GET FTP FILE NAME$` | Function | 0 | none -> string | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `GET FTP FILE SIZE` | Function | 0 | none -> real | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `GET FTP FILE TYPE` | Function | 0 | none -> real | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `GET FTP PROGRESS` | Function | 0 | none -> real | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `GET FTP STATUS` | Function | 0 | none -> real | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `GET GROUND HEIGHT` | Function | 3 | number, number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `GET INDEXDATA` | Function | 1 | number -> integer | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `GET KEY STATE` | Function | 1 | number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `GET MATRIX HEIGHT` | Function | 3 | number, number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `GET MATRIX4 ELEMENT` | Function | 2 | number, number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `GET MAXIMUM LIGHTS` | Function | 0 | none -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `GET MAXIMUM PIXEL SHADER VALUE` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `GET MAXIMUM PIXEL SHADER VERSION` | Function | 0 | none -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `GET MAXIMUM TEXTURE HEIGHT` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `GET MAXIMUM TEXTURE WIDTH` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `GET MAXIMUM VERTEX SHADER CONSTANTS` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `GET MAXIMUM VERTEX SHADER VERSION` | Function | 0 | none -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `GET MAXIMUM VOLUME EXTENT` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `GET MEMBLOCK FROM OBJECT` | Function | 2 | number, number -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `GET MEMBLOCK PTR` | Function | 1 | number -> real | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `GET MEMBLOCK SIZE` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `GET NUMBER OF CD TRACKS` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `GET OBJECT COLLISION X` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `GET OBJECT COLLISION Y` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `GET OBJECT COLLISION Z` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `GET PICK DISTANCE` | Function | 0 | none -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `GET PICK VECTOR X` | Function | 0 | none -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `GET PICK VECTOR Y` | Function | 0 | none -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `GET PICK VECTOR Z` | Function | 0 | none -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `GET PIXELS PITCH` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `GET PIXELS POINTER` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `GET REGISTRY` | Function | 2 | number, number -> real | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `GET REGISTRY$` | Function | 2 | number, number -> string | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `GET STATIC COLLISION COUNT` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `GET STATIC COLLISION FLOOR` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `GET STATIC COLLISION HIT` | Function | 12 | number, number, number, number, number, number, number, number, number, number, number, number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `GET STATIC COLLISION VALUE` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `GET STATIC COLLISION X` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `GET STATIC COLLISION Y` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `GET STATIC COLLISION Z` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `GET TERRAIN GROUND HEIGHT` | Function | 3 | number, number, number -> real | Unsupported (DBC2043) | DBPro snapshot, legacy unverified | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `GET TERRAIN HEIGHT` | Function | 3 | number, number, number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `GET TERRAIN SIZE` | Function | 1 | number -> real | Unsupported (DBC2043) | legacy unverified | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `GET TERRAIN X SIZE` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `GET TERRAIN Z SIZE` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `GET TIME$` | Function | 0 | none -> string | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `GET TOTAL TERRAIN HEIGHT` | Function | 1 | number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `GET TYPE PATTERN$` | Function | 1-2 | number, number -> string | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `GET VERTEXDATA DIFFUSE` | Function | 1 | number -> integer | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `GET VERTEXDATA INDEX COUNT` | Function | 0 | none -> integer | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `GET VERTEXDATA NORMALS X` | Function | 1 | number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `GET VERTEXDATA NORMALS Y` | Function | 1 | number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `GET VERTEXDATA NORMALS Z` | Function | 1 | number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `GET VERTEXDATA POSITION X` | Function | 1 | number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `GET VERTEXDATA POSITION Y` | Function | 1 | number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `GET VERTEXDATA POSITION Z` | Function | 1 | number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `GET VERTEXDATA PTR` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `GET VERTEXDATA U` | Function | 1-2 | number, number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `GET VERTEXDATA V` | Function | 1-2 | number, number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `GET VERTEXDATA VERTEX COUNT` | Function | 0 | none -> integer | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `GET VR CAMERA HEIGHT` | Function | 0 | none -> real | Implemented | OpenDBC extension | Shared OpenDBC VR addition; see docs/vr.md for its observable contract. |
| `HCOS` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `HEX$` | Function | 1 | number -> string | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `HSIN` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `HTAN` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `HTTP REQUEST DATA` | Function | 3 | number, number, number -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `IMAGE EXIST` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `IMAGE FILE EXIST` | Function | 1 | string -> integer | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `IMAGE HEIGHT` | Function | 1 | number -> integer | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `IMAGE WIDTH` | Function | 1 | number -> integer | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `INKEY$` | Function | 0 | none -> string | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `INSTR` | Function | 2 | string, string -> integer | Implemented | legacy unverified | DarkBASIC Professional function implemented with an observable contract. |
| `INT` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `INTERSECT OBJECT` | Function | 7 | number, number, number, number, number, number, number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `INVERSE MATRIX4` | Function | 2 | number, number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `IS EQUAL MATRIX4` | Function | 2 | number, number -> integer | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `IS EQUAL VECTOR2` | Function | 2 | number, number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `IS EQUAL VECTOR3` | Function | 2 | number, number -> integer | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `IS EQUAL VECTOR4` | Function | 2 | number, number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `IS IDENTITY MATRIX4` | Function | 1 | number -> integer | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `IS VR CONTROLLER TRACKED` | Function | 1 | integer -> integer | Implemented | OpenDBC extension | Shared OpenDBC VR addition; see docs/vr.md for its observable contract. |
| `IS VR ENABLED` | Function | 0 | none -> integer | Implemented | OpenDBC extension | Shared OpenDBC VR addition; see docs/vr.md for its observable contract. |
| `IS VR HEAD TRACKED` | Function | 0 | none -> integer | Implemented | OpenDBC extension | Shared OpenDBC VR addition; see docs/vr.md for its observable contract. |
| `IS VR SUPPORTED` | Function | 0 | none -> integer | Implemented | OpenDBC extension | Shared OpenDBC VR addition; see docs/vr.md for its observable contract. |
| `JOYSTICK DOWN` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `JOYSTICK FIRE A` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `JOYSTICK FIRE B` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `JOYSTICK FIRE C` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `JOYSTICK FIRE D` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `JOYSTICK FIRE X` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `JOYSTICK HAT ANGLE` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `JOYSTICK INFORMATION` | Function | 1 | number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `JOYSTICK LEFT` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `JOYSTICK RIGHT` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `JOYSTICK SLIDER A` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `JOYSTICK SLIDER B` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `JOYSTICK SLIDER C` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `JOYSTICK SLIDER D` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `JOYSTICK TWIST X` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `JOYSTICK TWIST Y` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `JOYSTICK TWIST Z` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `JOYSTICK UP` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `JOYSTICK X` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `JOYSTICK Y` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `JOYSTICK Z` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `KEYSTATE` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `LEFT$` | Function | 2 | string, number -> string | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `LEFTKEY` | Function | 0 | none -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `LEN` | Function | 1 | string -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `LENGTH VECTOR2` | Function | 1 | number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `LENGTH VECTOR3` | Function | 1 | number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `LENGTH VECTOR4` | Function | 1 | number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `LIGHT DIRECTION X` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `LIGHT DIRECTION Y` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `LIGHT DIRECTION Z` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `LIGHT EXIST` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `LIGHT POSITION X` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `LIGHT POSITION Y` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `LIGHT POSITION Z` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `LIGHT RANGE` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `LIGHT TYPE` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `LIGHT VISIBLE` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `LIMB ANGLE X` | Function | 2 | number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `LIMB ANGLE Y` | Function | 2 | number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `LIMB ANGLE Z` | Function | 2 | number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `LIMB COLLISION` | Function | 4 | number, number, number, number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `LIMB DIRECTION X` | Function | 2 | number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `LIMB DIRECTION Y` | Function | 2 | number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `LIMB DIRECTION Z` | Function | 2 | number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `LIMB EXIST` | Function | 2 | number, number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `LIMB HIT` | Function | 4 | number, number, number, number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `LIMB NAME$` | Function | 2 | number, number -> string | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `LIMB OFFSET X` | Function | 2 | number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `LIMB OFFSET Y` | Function | 2 | number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `LIMB OFFSET Z` | Function | 2 | number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `LIMB POSITION X` | Function | 2 | number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `LIMB POSITION Y` | Function | 2 | number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `LIMB POSITION Z` | Function | 2 | number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `LIMB SCALE X` | Function | 2 | number, number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `LIMB SCALE Y` | Function | 2 | number, number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `LIMB SCALE Z` | Function | 2 | number, number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `LIMB TEXTURE` | Function | 2-3 | number, number, number -> real | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `LIMB TEXTURE NAME` | Function | 2-3 | number, number, number -> string | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `LIMB VISIBLE` | Function | 2 | number, number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `LISTENER ANGLE X` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `LISTENER ANGLE Y` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `LISTENER ANGLE Z` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `LISTENER POSITION X` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `LISTENER POSITION Y` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `LISTENER POSITION Z` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `LOAD ICON` | Function | 1 | number -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `LOWER$` | Function | 1 | string -> string | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `MAKE MATRIX4` | Function | 1 | number -> integer | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `MAKE MEMORY` | Function | 1 | number -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `MAKE VECTOR2` | Function | 1 | number -> integer | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `MAKE VECTOR3` | Function | 1 | number -> integer | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `MAKE VECTOR4` | Function | 1 | number -> integer | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `MATRIX EXIST` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `MATRIX POSITION X` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `MATRIX POSITION Y` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `MATRIX POSITION Z` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `MATRIX TILE COUNT` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `MATRIX TILES EXIST` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `MATRIX WIREFRAME STATE` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `MEMBLOCK BYTE` | Function | 2 | number, number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `MEMBLOCK DWORD` | Function | 2 | number, number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `MEMBLOCK EXIST` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `MEMBLOCK FLOAT` | Function | 2 | number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `MEMBLOCK WORD` | Function | 2 | number, number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `MESH EXIST` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `MID$` | Function | 2 | string, number -> string | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `MIPCUBEMAP AVAILABLE` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `MIPMAP AVAILABLE` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `MIPMAPLODBIAS AVAILABLE` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `MIPMAPVOLUME AVAILABLE` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `MOUSECLICK` | Function | 0 | none -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `MOUSEMOVEX` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `MOUSEMOVEY` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `MOUSEMOVEZ` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `MOUSEX` | Function | 0 | none -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `MOUSEY` | Function | 0 | none -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `MOUSEZ` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `MUSIC EXIST` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `MUSIC LOOPING` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `MUSIC PAUSED` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `MUSIC PLAYING` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `MUSIC SPEED` | Function | 1 | number -> integer | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `MUSIC VOLUME` | Function | 1 | number -> integer | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `MYDOCDIR$` | Function | 0 | none -> string | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `NET BUFFER SIZE` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `NET GAME EXISTS` | Function | 0 | none -> real | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `NET GAME LOST` | Function | 0 | none -> real | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `NET GAME NOW HOSTING` | Function | 0 | none -> real | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `NET MESSAGE EXISTS` | Function | 0 | none -> real | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `NET MESSAGE FLOAT` | Function | 0 | none -> real | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `NET MESSAGE INTEGER` | Function | 0 | none -> real | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `NET MESSAGE PLAYER FROM` | Function | 0 | none -> real | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `NET MESSAGE PLAYER TO` | Function | 0 | none -> real | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `NET MESSAGE STRING$` | Function | 0 | none -> string | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `NET MESSAGE TYPE` | Function | 0 | none -> real | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `NET PLAYER CREATED` | Function | 0 | none -> real | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `NET PLAYER DESTROYED` | Function | 0 | none -> real | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `NEWXVALUE` | Function | 3 | number, number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `NEWYVALUE` | Function | 3 | number, number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `NEWZVALUE` | Function | 3 | number, number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `NEXT TOKEN$` | Function | 1 | string -> string | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `NONLOCALVIDEOMEMORY AVAILABLE` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `NONPOWTEXTURES AVAILABLE` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `OBJECT ANGLE X` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `OBJECT ANGLE Y` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `OBJECT ANGLE Z` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `OBJECT COLLISION` | Function | 2 | number, number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `OBJECT COLLISION CENTER X` | Function | 1 | number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `OBJECT COLLISION CENTER Y` | Function | 1 | number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `OBJECT COLLISION CENTER Z` | Function | 1 | number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `OBJECT COLLISION RADIUS` | Function | 1 | number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `OBJECT EXIST` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `OBJECT FRAME` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `OBJECT HIT` | Function | 2 | number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `OBJECT IN SCREEN` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `OBJECT INTERPOLATION` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `OBJECT LOOPING` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `OBJECT PLAYING` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `OBJECT POSITION X` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `OBJECT POSITION Y` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `OBJECT POSITION Z` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `OBJECT SCREEN X` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `OBJECT SCREEN Y` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `OBJECT SIZE` | Function | 1-2 | number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `OBJECT SIZE X` | Function | 1-2 | number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `OBJECT SIZE Y` | Function | 1-2 | number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `OBJECT SIZE Z` | Function | 1-2 | number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `OBJECT SPEED` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `OBJECT VISIBLE` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `ONLYSQUARETEXTURES AVAILABLE` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `PARTICLES EXIST` | Function | 1 | number -> integer | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `PARTICLES POSITION X` | Function | 1 | number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `PARTICLES POSITION Y` | Function | 1 | number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `PARTICLES POSITION Z` | Function | 1 | number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `PATH EXIST` | Function | 1 | string -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `PERFTIMER` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `PERSPECTIVETEXTURES AVAILABLE` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `PICK OBJECT` | Function | 4-5 | number, number, number, number, number -> integer | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `PIXEL SHADER EXIST` | Function | 1 | number -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `POINT` | Function | 2 | number, number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `PROJECTEDTEXTURES AVAILABLE` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `READ BYTE` | Function | 1 | integer output -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `READ BYTE FROM FILE` | Function | 2 | string, number -> integer | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `READ FILEMAP STRING` | Function | 1 | number -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `READ FILEMAP VALUE` | Function | 1 | number -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `READ FLOAT` | Function | 1 | real output -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `READ LONG` | Function | 1 | integer output -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `READ WORD` | Function | 1 | integer output -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `RENDERAFTERFLIP AVAILABLE` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `RENDERWINDOWED AVAILABLE` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `RETURNKEY` | Function | 0 | none -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `REVERSE$` | Function | 1 | string -> string | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `RGB` | Function | 3 | number, number, number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `RGBB` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `RGBG` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `RGBR` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `RIGHT$` | Function | 2 | string, number -> string | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `RIGHTKEY` | Function | 0 | none -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `RND` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `SCANCODE` | Function | 0 | none -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `SCREEN DEPTH` | Function | 0 | none -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `SCREEN FPS` | Function | 0 | none -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `SCREEN HEIGHT` | Function | 0 | none -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `SCREEN INVALID` | Function | 0 | none -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `SCREEN TYPE` | Function | 0 | none -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `SCREEN WIDTH` | Function | 0 | none -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `SEPARATETEXTUREMEMORIES AVAILABLE` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `SEPERATETEXTUREMEMORIES AVAILABLE` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `SHIFTKEY` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `SIN` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `SOUND EXIST` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `SOUND LOOPING` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `SOUND PAN` | Function | 1 | number -> integer | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `SOUND PAUSED` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `SOUND PLAYING` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `SOUND POSITION X` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `SOUND POSITION Y` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `SOUND POSITION Z` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `SOUND SPEED` | Function | 1 | number -> integer | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `SOUND TYPE` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `SOUND VOLUME` | Function | 1 | number -> integer | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `SPACE$` | Function | 1 | number -> string | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `SPACEKEY` | Function | 0 | none -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `SPRITE ALPHA` | Function | 1 | number -> integer | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `SPRITE ANGLE` | Function | 1 | number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `SPRITE BLUE` | Function | 1 | number -> integer | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `SPRITE COLLISION` | Function | 2 | number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `SPRITE EXIST` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `SPRITE FLIPPED` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `SPRITE FRAME` | Function | 1 | number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `SPRITE GREEN` | Function | 1 | number -> integer | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `SPRITE HEIGHT` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `SPRITE HIT` | Function | 2 | number, number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `SPRITE IMAGE` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `SPRITE MIRRORED` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `SPRITE OFFSET X` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `SPRITE OFFSET Y` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `SPRITE PRIORITY` | Function | 1 | number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `SPRITE RED` | Function | 1 | number -> integer | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `SPRITE SCALE X` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `SPRITE SCALE Y` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `SPRITE VISIBLE` | Function | 1 | number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `SPRITE WIDTH` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `SPRITE X` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `SPRITE Y` | Function | 1 | number -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `SQRT` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `SQUARED LENGTH VECTOR2` | Function | 1 | number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `SQUARED LENGTH VECTOR3` | Function | 1 | number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `SQUARED LENGTH VECTOR4` | Function | 1 | number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `STATIC LINE OF SIGHT` | Function | 8 | number, number, number, number, number, number, number, number -> real | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `STATIC LINE OF SIGHT X` | Function | 0 | none -> real | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `STATIC LINE OF SIGHT Y` | Function | 0 | none -> real | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `STATIC LINE OF SIGHT Z` | Function | 0 | none -> real | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `STATIC RAYCAST` | Function | 6 | number, number, number, number, number, number -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `STATIC VOLUME` | Function | 7 | number, number, number, number, number, number, number -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `STATISTIC` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `STR$` | Function | 1-2 | any, number -> string | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `SYSTEM DMEM AVAILABLE` | Function | 0 | none -> real | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `SYSTEM SMEM AVAILABLE` | Function | 0 | none -> real | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `SYSTEM TMEM AVAILABLE` | Function | 0 | none -> real | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `TAN` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `TERRAIN EXIST` | Function | 1 | number -> integer | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `TERRAIN POSITION X` | Function | 1 | number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `TERRAIN POSITION Y` | Function | 1 | number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `TERRAIN POSITION Z` | Function | 1 | number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `TEXT BACKGROUND TYPE` | Function | 0 | none -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `TEXT FONT$` | Function | 0 | none -> string | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `TEXT HEIGHT` | Function | 1 | string -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `TEXT SIZE` | Function | 0 | none -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `TEXT STYLE` | Function | 0 | none -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `TEXT WIDTH` | Function | 1 | string -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `TEXTURESYSTEMMEMORY AVAILABLE` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `TEXTUREVIDEOMEMORY AVAILABLE` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `TIMER` | Function | 0 | none -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `TLVERTEXSYSTEMMEMORY AVAILABLE` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `TLVERTEXVIDEOMEMORY AVAILABLE` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `TNL AVAILABLE` | Function | 0 | none -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `TOTAL DVD CHAPTERS` | Function | 2 | number, number -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `TOTAL OBJECT FRAMES` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `UPKEY` | Function | 0 | none -> integer | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `UPPER$` | Function | 1 | string -> string | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `VAL` | Function | 1 | string -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `VERTEX SHADER EXIST` | Function | 1 | number -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `VOLUMEMAP AVAILABLE` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `W VECTOR4` | Function | 1 | number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `WBUFFER AVAILABLE` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `WFOG AVAILABLE` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `WINDIR$` | Function | 0 | none -> string | Unsupported (DBC2043) | Classic help, DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `WINDOW EXIST` | Function | 1 | string -> integer | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `WRAPVALUE` | Function | 1 | number -> real | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `X VECTOR2` | Function | 1 | number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `X VECTOR3` | Function | 1 | number -> real | Implemented | DBPro snapshot, OpenDBC extension | DarkBASIC Professional function implemented with an observable contract. |
| `X VECTOR4` | Function | 1 | number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `Y VECTOR2` | Function | 1 | number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `Y VECTOR3` | Function | 1 | number -> real | Implemented | DBPro snapshot, OpenDBC extension | DarkBASIC Professional function implemented with an observable contract. |
| `Y VECTOR4` | Function | 1 | number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `Z VECTOR3` | Function | 1 | number -> real | Implemented | DBPro snapshot, OpenDBC extension | DarkBASIC Professional function implemented with an observable contract. |
| `Z VECTOR4` | Function | 1 | number -> real | Implemented | DBPro snapshot | DarkBASIC Professional function implemented with an observable contract. |
| `ZBUFFER AVAILABLE` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `ZFOG AVAILABLE` | Function | 0 | none -> real | Unsupported (DBC2043) | DBPro snapshot | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `&&` | Operator | 0 | grammar form | Implemented | DBPro snapshot | DarkBASIC Professional operator implemented with an observable contract. |
| `*` | Operator | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional operator implemented with an observable contract. |
| `+` | Operator | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional operator implemented with an observable contract. |
| `-` | Operator | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional operator implemented with an observable contract. |
| `.` | Operator | 0 | grammar form | Implemented | DBPro snapshot | DarkBASIC Professional operator implemented with an observable contract. |
| `/` | Operator | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional operator implemented with an observable contract. |
| `<` | Operator | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional operator implemented with an observable contract. |
| `<<` | Operator | 0 | grammar form | Implemented | DBPro snapshot | DarkBASIC Professional operator implemented with an observable contract. |
| `<=` | Operator | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional operator implemented with an observable contract. |
| `<>` | Operator | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional operator implemented with an observable contract. |
| `=` | Operator | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional operator implemented with an observable contract. |
| `>` | Operator | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional operator implemented with an observable contract. |
| `>=` | Operator | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional operator implemented with an observable contract. |
| `>>` | Operator | 0 | grammar form | Implemented | DBPro snapshot | DarkBASIC Professional operator implemented with an observable contract. |
| `AND` | Operator | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional operator implemented with an observable contract. |
| `MOD` | Operator | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional operator implemented with an observable contract. |
| `NOT` | Operator | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional operator implemented with an observable contract. |
| `OR` | Operator | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional operator implemented with an observable contract. |
| `XOR` | Operator | 1 | grammar form | Implemented | DBPro snapshot | DarkBASIC Professional operator implemented with an observable contract. |
| `^` | Operator | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional operator implemented with an observable contract. |
| `&#124;&#124;` | Operator | 0 | grammar form | Implemented | DBPro snapshot | DarkBASIC Professional operator implemented with an observable contract. |
| `~~` | Operator | 0 | grammar form | Implemented | DBPro snapshot | DarkBASIC Professional operator implemented with an observable contract. |
| `AS` | Statement | 0 | grammar form | Implemented | DBPro snapshot | DarkBASIC Professional statement implemented with an observable contract. |
| `BOOLEAN` | Statement | 1 | grammar form | Implemented | DBPro snapshot | DarkBASIC Professional statement implemented with an observable contract. |
| `BYTE` | Statement | 1 | grammar form | Implemented | DBPro snapshot | DarkBASIC Professional statement implemented with an observable contract. |
| `CASE` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional statement implemented with an observable contract. |
| `CASE DEFAULT` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional statement implemented with an observable contract. |
| `DATA` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional statement implemented with an observable contract. |
| `DEC` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional statement implemented with an observable contract. |
| `DIM` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional statement implemented with an observable contract. |
| `DO` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional statement implemented with an observable contract. |
| `DOUBLE FLOAT` | Statement | 1 | grammar form | Implemented | DBPro snapshot | DarkBASIC Professional statement implemented with an observable contract. |
| `DOUBLE INTEGER` | Statement | 13 | grammar form | Implemented | DBPro snapshot | DarkBASIC Professional statement implemented with an observable contract. |
| `DWORD` | Statement | 4 | grammar form | Implemented | DBPro snapshot | DarkBASIC Professional statement implemented with an observable contract. |
| `ELSE` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional statement implemented with an observable contract. |
| `ELSEIF` | Statement | 0 | grammar form | Implemented | DBPro snapshot | DarkBASIC Professional statement implemented with an observable contract. |
| `END` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional statement implemented with an observable contract. |
| `ENDCASE` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional statement implemented with an observable contract. |
| `ENDFUNCTION` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional statement implemented with an observable contract. |
| `ENDIF` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional statement implemented with an observable contract. |
| `ENDSELECT` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional statement implemented with an observable contract. |
| `ENDTYPE` | Statement | 0 | grammar form | Implemented | DBPro snapshot | DarkBASIC Professional statement implemented with an observable contract. |
| `ENDWHILE` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional statement implemented with an observable contract. |
| `EXIT` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional statement implemented with an observable contract. |
| `EXITFUNCTION` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional statement implemented with an observable contract. |
| `FLOAT` | Statement | 1 | grammar form | Implemented | DBPro snapshot | DarkBASIC Professional statement implemented with an observable contract. |
| `FOR` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional statement implemented with an observable contract. |
| `FUNCTION` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional statement implemented with an observable contract. |
| `GLOBAL` | Statement | 0 | grammar form | Implemented | DBPro snapshot | DarkBASIC Professional statement implemented with an observable contract. |
| `GOSUB` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional statement implemented with an observable contract. |
| `GOTO` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional statement implemented with an observable contract. |
| `IF` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional statement implemented with an observable contract. |
| `INC` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional statement implemented with an observable contract. |
| `INTEGER` | Statement | 7 | grammar form | Implemented | DBPro snapshot | DarkBASIC Professional statement implemented with an observable contract. |
| `LOCAL` | Statement | 0 | grammar form | Implemented | DBPro snapshot | DarkBASIC Professional statement implemented with an observable contract. |
| `LOOP` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional statement implemented with an observable contract. |
| `NEXT` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional statement implemented with an observable contract. |
| `READ` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional statement implemented with an observable contract. |
| `REM` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional statement implemented with an observable contract. |
| `REMEND` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional statement implemented with an observable contract. |
| `REMSTART` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional statement implemented with an observable contract. |
| `REPEAT` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional statement implemented with an observable contract. |
| `RESTORE` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional statement implemented with an observable contract. |
| `RETURN` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional statement implemented with an observable contract. |
| `SELECT` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional statement implemented with an observable contract. |
| `STEP` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional statement implemented with an observable contract. |
| `STRING` | Statement | 1 | grammar form | Implemented | DBPro snapshot | DarkBASIC Professional statement implemented with an observable contract. |
| `THEN` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional statement implemented with an observable contract. |
| `TYPE` | Statement | 0 | grammar form | Implemented | DBPro snapshot | DarkBASIC Professional statement implemented with an observable contract. |
| `UNDIM` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional statement implemented with an observable contract. |
| `UNTIL` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional statement implemented with an observable contract. |
| `WHILE` | Statement | 0 | grammar form | Implemented | Classic help, DBPro snapshot | DarkBASIC Professional statement implemented with an observable contract. |
| `WORD` | Statement | 1 | grammar form | Implemented | DBPro snapshot | DarkBASIC Professional statement implemented with an observable contract. |
