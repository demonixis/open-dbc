# DarkBASIC Professional extension compatibility reference

This file is generated from `src/language/language_catalogue.inc` by
`scripts/generate_language_references.py`. It describes OpenDBC support; edit the catalogue, not this file.
The ODE commands are always available in the `DbPro` profile and run on the pinned Jolt backend; remaining forms retain explicit diagnostics.
DarkPHYSICS, Dark Dynamix and 3D Cloth & Particles forms are not in the catalogue: they fail to compile with `DBC2049`.

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
| `ODE ADD FORCE` | Command | 7 | integer, real, real, real, real, real, real -> void | Implemented | ODE extension | DarkBASIC Professional command implemented with an observable contract. |
| `ODE ADD STATIC OBJECT BOX` | Command | 2 | integer, integer -> void | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ODE COLLISION GET MESSAGE` | Command | 0 | none -> void | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ODE CONTROL CHARACTER CONTROLLER` | Command | 9 | integer, real, real, real, real, real, real, real, real -> void | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ODE CREATE BALL JOINT` | Command | 6 | integer, integer, integer, real, real, real -> void | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ODE CREATE CHARACTER CONTROLLER` | Command | 4 | integer, real, real, real -> void | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ODE CREATE DYNAMIC BOX` | Command | 1, 3, 4, 6 | integer -> void; integer, integer, integer -> void; integer, real, real, real -> void; integer, integer, integer, real, real, real -> void | Implemented | ODE extension | DarkBASIC Professional command implemented with an observable contract. |
| `ODE CREATE DYNAMIC BOX MANUAL` | Command | 10 | integer, real, real, real, real, real, real, real, real, real -> void | Implemented | ODE extension | DarkBASIC Professional command implemented with an observable contract. |
| `ODE CREATE DYNAMIC CAPSULE` | Command | 3 | integer, real, real -> void | Implemented | ODE extension | DarkBASIC Professional command implemented with an observable contract. |
| `ODE CREATE DYNAMIC CYLINDER` | Command | 1 | integer -> void | Implemented | ODE extension | DarkBASIC Professional command implemented with an observable contract. |
| `ODE CREATE DYNAMIC SPHERE` | Command | 1 | integer -> void | Implemented | ODE extension | DarkBASIC Professional command implemented with an observable contract. |
| `ODE CREATE DYNAMIC TRIANGLE MESH` | Command | 1 | integer -> void | Implemented | ODE extension | DarkBASIC Professional command implemented with an observable contract. |
| `ODE CREATE HINGE JOINT` | Command | 8-9 | integer, integer, real, real, real, real, real, real -> void; integer, integer, integer, real, real, real, real, real, real -> void | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ODE CREATE RAGDOLL FROM BONE MODEL` | Command | 2-3 | integer, real -> void; integer, real, integer -> void | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ODE CREATE STATIC BOX` | Command | 1 | integer -> void | Implemented | ODE extension | DarkBASIC Professional command implemented with an observable contract. |
| `ODE CREATE STATIC BOX LIMB` | Command | 2 | integer, integer -> void | Implemented | ODE extension | DarkBASIC Professional command implemented with an observable contract. |
| `ODE CREATE STATIC CAPSULE` | Command | 1 | integer -> void | Implemented | ODE extension | DarkBASIC Professional command implemented with an observable contract. |
| `ODE CREATE STATIC CYLINDER` | Command | 10 | integer, real, real, real, real, real, real, real, real, real -> void | Implemented | ODE extension | DarkBASIC Professional command implemented with an observable contract. |
| `ODE CREATE STATIC SPHERE` | Command | 1 | integer -> void | Implemented | ODE extension | DarkBASIC Professional command implemented with an observable contract. |
| `ODE CREATE STATIC TERRAIN` | Command | 4 | integer, integer, integer, integer -> void | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ODE CREATE STATIC TERRAIN MESH` | Command | 1 | integer -> void | Implemented | ODE extension | DarkBASIC Professional command implemented with an observable contract. |
| `ODE CREATE STATIC TRIANGLE MESH` | Command | 1, 3, 4 | integer -> void; integer, integer, integer -> void; integer, integer, integer, integer -> void | Implemented | ODE extension | DarkBASIC Professional command implemented with an observable contract. |
| `ODE CREATE STATIC TRIANGLE MESH LIMB` | Command | 2 | integer, integer -> void | Implemented | ODE extension | DarkBASIC Professional command implemented with an observable contract. |
| `ODE CREATE STATIC UNIVERSE` | Command | 0 | none -> void | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ODE CREATE TREE CYLINDER` | Command | 1 | integer -> void | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ODE DESTROY OBJECT` | Command | 1 | integer -> void | Implemented | ODE extension | DarkBASIC Professional command implemented with an observable contract. |
| `ODE DESTROY RAGDOLL` | Command | 1 | integer -> void | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ODE END` | Command | 0 | none -> void | Implemented | ODE extension | DarkBASIC Professional command implemented with an observable contract. |
| `ODE END STATIC OBJECT` | Command | 2 | integer, integer -> void | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ODE SET ACTIVE` | Command | 2 | integer, integer -> void | Implemented | ODE extension | DarkBASIC Professional command implemented with an observable contract. |
| `ODE SET ANGULAR VELOCITY` | Command | 4 | integer, real, real, real -> void | Implemented | ODE extension | DarkBASIC Professional command implemented with an observable contract. |
| `ODE SET BODY FRICTION` | Command | 2 | integer, real -> void | Implemented | ODE extension | DarkBASIC Professional command implemented with an observable contract. |
| `ODE SET BODY MASS` | Command | 2 | integer, real -> void | Implemented | ODE extension | DarkBASIC Professional command implemented with an observable contract. |
| `ODE SET BODY POSITION` | Command | 4 | integer, real, real, real -> void | Implemented | ODE extension | DarkBASIC Professional command implemented with an observable contract. |
| `ODE SET BODY ROTATION` | Command | 4 | integer, real, real, real -> void | Implemented | ODE extension | DarkBASIC Professional command implemented with an observable contract. |
| `ODE SET CHARACTER CONTROLLER` | Command | 8 | integer, real, real, real, real, real, real, real -> void | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ODE SET CONTACT` | Command | 3 | integer, integer, integer -> void | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ODE SET CONTACT BOUNCE` | Command | 2 | integer, real -> void | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ODE SET CONTACT BOUNCE VELOCITY` | Command | 2 | integer, real -> void | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ODE SET CONTACT FDIR1` | Command | 2 | integer, real -> void | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ODE SET CONTACT MOTION1` | Command | 2 | integer, real -> void | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ODE SET CONTACT MOTION2` | Command | 2 | integer, real -> void | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ODE SET CONTACT MU2` | Command | 2 | integer, real -> void | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ODE SET CONTACT SLIP1` | Command | 2 | integer, real -> void | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ODE SET CONTACT SLIP2` | Command | 2 | integer, real -> void | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ODE SET CONTACT SOFT CFM` | Command | 2 | integer, real -> void | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ODE SET CONTACT SOFT ERP` | Command | 2 | integer, real -> void | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ODE SET CONTACT VELOCITY` | Command | 2 | integer, real -> void | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ODE SET ENABLED` | Command | 2 | integer, integer -> void | Implemented | ODE extension | DarkBASIC Professional command implemented with an observable contract. |
| `ODE SET GRAVITY` | Command | 2 | integer, integer -> void | Implemented | ODE extension | DarkBASIC Professional command implemented with an observable contract. |
| `ODE SET HINGE JOINT LIMIT` | Command | 3 | integer, real, real -> void | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ODE SET LINEAR VELOCITY` | Command | 4 | integer, real, real, real -> void | Implemented | ODE extension | DarkBASIC Professional command implemented with an observable contract. |
| `ODE SET RAGDOLL MODE` | Command | 1 | integer -> void | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ODE SET RAY CAST` | Command | 2 | integer, integer -> void | Implemented | ODE extension | DarkBASIC Professional command implemented with an observable contract. |
| `ODE SET RESPONSE` | Command | 2 | integer, integer -> void | Implemented | ODE extension | DarkBASIC Professional command implemented with an observable contract. |
| `ODE SET STEP MODE` | Command | 1 | integer -> void | Implemented | ODE extension | DarkBASIC Professional command implemented with an observable contract. |
| `ODE SET WATER LINE` | Command | 1 | real -> void | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ODE SET WORLD CFM` | Command | 1 | real -> void | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ODE SET WORLD ERP` | Command | 1 | real -> void | Implemented | ODE extension | DarkBASIC Professional command implemented with an observable contract. |
| `ODE SET WORLD GRAVITY` | Command | 3 | real, real, real -> void | Implemented | ODE extension | DarkBASIC Professional command implemented with an observable contract. |
| `ODE SET WORLD QUICK STEP NUM ITERATIONS` | Command | 1 | integer -> void | Implemented | ODE extension | DarkBASIC Professional command implemented with an observable contract. |
| `ODE SET WORLD STEP` | Command | 1 | real -> void | Implemented | ODE extension | DarkBASIC Professional command implemented with an observable contract. |
| `ODE SETSURFACE MODE` | Command | 3 | integer, integer, integer -> void | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ODE SETSURFACE MODE CONTACT APPROX0` | Command | 2 | integer, integer -> void | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ODE SETSURFACE MODE CONTACT APPROX1` | Command | 2 | integer, integer -> void | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ODE SETSURFACE MODE CONTACT APPROX11` | Command | 2 | integer, integer -> void | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ODE SETSURFACE MODE CONTACT APPROX12` | Command | 2 | integer, integer -> void | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ODE SETSURFACE MODE CONTACT BOUNCE` | Command | 2 | integer, integer -> void | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ODE SETSURFACE MODE CONTACT MOTION1` | Command | 2 | integer, integer -> void | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ODE SETSURFACE MODE CONTACT MOTION2` | Command | 2 | integer, integer -> void | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ODE SETSURFACE MODE CONTACT MU2` | Command | 2 | integer, integer -> void | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ODE SETSURFACE MODE CONTACT SLIP1` | Command | 2 | integer, integer -> void | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ODE SETSURFACE MODE CONTACT SLIP2` | Command | 2 | integer, integer -> void | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ODE SETSURFACE MODE CONTACT SOFT CFM` | Command | 2 | integer, integer -> void | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ODE SETSURFACE MODE CONTACT SOFT ERP` | Command | 2 | integer, integer -> void | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ODE START` | Command | 0 | none -> void | Implemented | ODE extension | DarkBASIC Professional command implemented with an observable contract. |
| `ODE START STATIC OBJECT` | Command | 1 | integer -> void | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ODE UPDATE` | Command | 0-1 | none -> void; real -> void | Implemented | ODE extension | DarkBASIC Professional command implemented with an observable contract. |
| `ODE UPDATE STATIC TERRAIN` | Command | 8 | integer, integer, integer, integer, integer, integer, integer, integer -> void | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional command recognized for a stable unsupported diagnostic. |
| `ODE COLLISION MESSAGE EXISTS` | Function | 0 | none -> integer | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `ODE CREATE SWINGDOOR HINGE` | Function | 5 | integer, real, real, real, real -> integer | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `ODE GET BODY ADJUSTMENT X` | Function | 1 | integer -> real | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `ODE GET BODY ADJUSTMENT Y` | Function | 1 | integer -> real | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `ODE GET BODY ADJUSTMENT Z` | Function | 1 | integer -> real | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `ODE GET BODY HEIGHT` | Function | 1 | integer -> real | Implemented | ODE extension | DarkBASIC Professional function implemented with an observable contract. |
| `ODE GET BODY LINEAR VELOCITY X` | Function | 1 | integer -> real | Implemented | ODE extension | DarkBASIC Professional function implemented with an observable contract. |
| `ODE GET BODY LINEAR VELOCITY Y` | Function | 1 | integer -> real | Implemented | ODE extension | DarkBASIC Professional function implemented with an observable contract. |
| `ODE GET BODY LINEAR VELOCITY Z` | Function | 1 | integer -> real | Implemented | ODE extension | DarkBASIC Professional function implemented with an observable contract. |
| `ODE GET BODY TRAVEL DISTANCE X` | Function | 1 | integer -> real | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `ODE GET BODY TRAVEL DISTANCE Y` | Function | 1 | integer -> real | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `ODE GET BODY TRAVEL DISTANCE Z` | Function | 1 | integer -> real | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `ODE GET CHARACTER CONTROLLER DUCKING` | Function | 1 | integer -> real | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `ODE GET CHARACTER FALL DISTANCE` | Function | 0 | none -> real | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `ODE GET CHARACTER HIT FLOOR` | Function | 0 | none -> integer | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `ODE GET OBJECT A` | Function | 0 | none -> integer | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `ODE GET OBJECT A ANGULAR VELOCITY X` | Function | 0 | none -> real | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `ODE GET OBJECT A ANGULAR VELOCITY Y` | Function | 0 | none -> real | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `ODE GET OBJECT A ANGULAR VELOCITY Z` | Function | 0 | none -> real | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `ODE GET OBJECT A CONTACT` | Function | 0 | none -> real | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `ODE GET OBJECT A VELOCITY X` | Function | 0 | none -> real | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `ODE GET OBJECT A VELOCITY Y` | Function | 0 | none -> real | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `ODE GET OBJECT A VELOCITY Z` | Function | 0 | none -> real | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `ODE GET OBJECT B` | Function | 0 | none -> integer | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `ODE GET OBJECT B ANGULAR VELOCITY X` | Function | 0 | none -> real | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `ODE GET OBJECT B ANGULAR VELOCITY Y` | Function | 0 | none -> real | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `ODE GET OBJECT B ANGULAR VELOCITY Z` | Function | 0 | none -> real | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `ODE GET OBJECT B CONTACT` | Function | 0 | none -> real | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `ODE GET OBJECT B VELOCITY X` | Function | 0 | none -> real | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `ODE GET OBJECT B VELOCITY Y` | Function | 0 | none -> real | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `ODE GET OBJECT B VELOCITY Z` | Function | 0 | none -> real | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `ODE GET RAY COLLISION X` | Function | 0 | none -> real | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `ODE GET RAY COLLISION Y` | Function | 0 | none -> real | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `ODE GET RAY COLLISION Z` | Function | 0 | none -> real | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `ODE GET RAY NORMAL X` | Function | 0 | none -> real | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `ODE GET RAY NORMAL Y` | Function | 0 | none -> real | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `ODE GET RAY NORMAL Z` | Function | 0 | none -> real | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `ODE GET RAY OBJECT HIT` | Function | 0 | none -> integer | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `ODE RAY` | Function | 7 | real, real, real, real, real, real, integer -> integer | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `ODE RAY CAST` | Function | 7 | integer, real, real, real, real, real, real -> integer | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `ODE RAY FORCE` | Function | 7 | real, real, real, real, real, real, real -> integer | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
| `ODE RAY TERRAIN` | Function | 6 | real, real, real, real, real, real -> integer | Unsupported (DBC2043) | ODE extension | Historical DarkBASIC Professional function recognized for a stable unsupported diagnostic. |
