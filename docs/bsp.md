# DBPro BSP worlds

`dbpro` only. Behaviour follows the MIT DBPro sources: the `LOAD BSP` dispatch in
`Shared/Q3BSP/CBSPFile.cpp` and DBPro's own format in `Shared/CustomBSP/`.

## Loading

`LOAD BSP pk3$, map$` tries the map name as given, then the same name in the
pk3's folder, matching file names without case. OpenDBC reads DBPro's own BSP
format (the world tutorial's `ikzdm1.BSP`): nodes, planes, leaves with polygon
ranges, run-length PVS bytes and 21-byte texture names, every index validated.
Quake 1, Half-Life, Quake 2 and Quake 3 files, and maps inside pk3 archives, fail
with a runtime error naming the format. A new `LOAD BSP` replaces the world;
`DELETE BSP` removes it and does nothing without one.

Texture names are relative to the map and may not leave its folder. A missing
file falls back to the same stem as `.png`, `.jpg`, `.dds`, `.tga` or `.bmp`; a
texture that still cannot load leaves its polygons untextured, as in DBPro.

## Drawing

The world is lit like objects, with a white material and wrapped linear
sampling. Each frame the leaf holding the BSP camera (`SET BSP CAMERA`, camera 0
by default; a missing camera counts as the origin) selects its PVS leaves, which
are then culled by the view frustum. Polygons face their stored normal. A BSP
camera in solid space draws nothing.

## Collision

| Form | Contract |
| --- | --- |
| `SET BSP CAMERA COLLISION i, camera, r, response` | Ellipsoid (r, 2r, r) |
| `SET BSP OBJECT COLLISION i, object, r, response` | Same for an object |
| `SET BSP CAMERA/OBJECT COLLISION RADIUS i, id, x, y, z` | Ellipsoid (x, y, z); keeps the response |
| `SET BSP COLLISION HEIGHT ADJUSTMENT i, h` | Collision centre `h` below the entity |
| `SET BSP COLLISION THRESHHOLD i, s` | Moves shorter than `s` (scaled units) are not tested; default 0.003 |
| `SET BSP COLLISION OFF i` | Stops index `i` |
| `PROCESS BSP COLLISION i` | Resolves index `i` now |
| `BSP COLLISION HIT(i)` | 1 after a contact, then 0 until the next one |
| `BSP COLLISION X/Y/Z(i)` | Resolved minus requested position |

Indices run from 1 to 24; others are runtime errors. Setup commands do nothing
before a world is loaded, as in DBPro. At each `SYNC` every active index sweeps
its ellipsoid from the position recorded after the previous frame to the current
one, before drawing. Response 0 keeps the slide, 1 returns to the recorded
position, other values only record the hit.

The solver keeps DBPro's contract, not its numerical quirks: positions scaled by
the radii, one-sided triangles, a 0.001 skin and at most five slide steps. It
does not push out an ellipsoid that already overlaps the world. DBPro ignored the
threshold for its own format; OpenDBC applies the Quake 3 meaning to every map.

`SET BSP MULTITEXTURING ON/OFF` only drove Quake 1/2 lightmaps and remain
unsupported (DBC2043).
