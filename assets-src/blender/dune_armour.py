"""Build the Dune Armour demo media: a desert terrain atlas, the player's tank,
the enemy six-wheeler, a radar tower, a watchtower, two buildings, acacia
billboards and their textures.

Run from the repository root:
    /Applications/Blender.app/Contents/MacOS/Blender --background --factory-startup \
        --python assets-src/blender/dune_armour.py
"""

import math
import sys
from pathlib import Path

import bpy
import numpy as np
from mathutils import Matrix

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bake_textures as bt  # noqa: E402
import xexport  # noqa: E402
from classic_tutorials import sprite  # noqa: E402
from flooded_vault import material  # noqa: E402
from frost_outpost import part, pistol_metal  # noqa: E402

OUT = bt.ROOT / "examples/dbc/demos/dune_armour"


# ---------------------------------------------------------------------------
# Textures

def ripples(g, frequency, offset):
    # Wind ripples: a sine across v, bent by noise so the crests wander.
    bend = g.math("MULTIPLY", g.noise(3.0, 4, 0.5, offset), 3.0)
    phase = g.math("ADD", g.math("MULTIPLY", g.v, frequency), bend)
    return g.math("ADD", 0.5, g.math("MULTIPLY", g.math("SINE", g.math("MULTIPLY", phase, 2 * math.pi)), 0.5))


def sand(g):
    crest = ripples(g, 24.0, 201.0)
    grain = g.noise(40.0, 6, 0.6, 203.0)
    colour = g.ramp(g.math("ADD", g.math("MULTIPLY", crest, 0.3), g.math("MULTIPLY", grain, 0.7)),
                    [(0.3, (0.62, 0.47, 0.28)), (0.75, (0.86, 0.71, 0.48))])
    g.finish(colour, g.math("ADD", g.math("MULTIPLY", crest, 0.6), g.math("MULTIPLY", grain, 0.4)), 0.5)


def dune_shadow(g):
    crest = ripples(g, 14.0, 207.0)
    grain = g.noise(30.0, 6, 0.6, 209.0)
    colour = g.ramp(grain, [(0.3, (0.48, 0.34, 0.20)), (0.8, (0.70, 0.54, 0.33))])
    g.finish(colour, g.math("ADD", g.math("MULTIPLY", crest, 0.5), g.math("MULTIPLY", grain, 0.5)), 0.5)


def desert_rock(g):
    cracks = g.math("POWER", g.math("SUBTRACT", 1.0, g.voronoi(5.0, "DISTANCE_TO_EDGE", offset=211.0)), 10.0)
    stone = g.noise(10.0, 8, 0.65, 213.0)
    colour = g.ramp(stone, [(0.3, (0.40, 0.22, 0.14)), (0.7, (0.66, 0.42, 0.28))])
    colour = g.mix(g.math("MULTIPLY", cracks, 0.6), colour, (0.15, 0.08, 0.05, 1.0))
    g.finish(colour, g.math("SUBTRACT", stone, g.math("MULTIPLY", cracks, 0.5)), 0.8)


def track_road(g):
    grain = g.noise(36.0, 6, 0.6, 217.0)
    ruts = g.math("ADD", g.math("LESS_THAN", g.math("ABSOLUTE", g.math("SUBTRACT", g.u, 0.3)), 0.06),
                  g.math("LESS_THAN", g.math("ABSOLUTE", g.math("SUBTRACT", g.u, 0.7)), 0.06))
    colour = g.ramp(grain, [(0.3, (0.55, 0.44, 0.30)), (0.8, (0.72, 0.60, 0.43))])
    colour = g.mix(g.math("MULTIPLY", ruts, 0.35), colour, (0.25, 0.18, 0.10, 1.0))
    g.finish(colour, g.math("SUBTRACT", grain, g.math("MULTIPLY", ruts, 0.4)), 0.5)


def camo(g):
    blobs = g.noise(3.0, 3, 0.4, 221.0)
    colour = g.ramp(blobs, [(0.42, (0.76, 0.64, 0.42)), (0.48, (0.45, 0.42, 0.24)), (0.58, (0.45, 0.42, 0.24)),
                            (0.62, (0.36, 0.26, 0.16))])
    wear = g.noise(30.0, 6, 0.6, 223.0)
    colour = g.mix(g.math("MULTIPLY", g.math("GREATER_THAN", wear, 0.7), 0.3), colour, (0.85, 0.80, 0.70, 1.0))
    g.finish(colour, wear, 0.3)


def treads(g):
    # Track links in both directions, so the belt reads whichever way the UVs run.
    edge, _ = g.cells(6.0, 12.0, 0.0)
    pad, _ = bt.bevel(g, edge, 0.04, 0.12, g.noise(20.0, 4, 0.5, 227.0))
    colour = g.ramp(pad, [(0.0, (0.05, 0.05, 0.05)), (1.0, (0.42, 0.40, 0.36))])
    g.finish(colour, pad, 1.0)


def tyre(g):
    edge, _ = g.cells(12.0, 1.0, 0.0)
    block, _ = bt.bevel(g, edge, 0.08, 0.1, g.noise(20.0, 4, 0.5, 229.0))
    colour = g.ramp(block, [(0.0, (0.04, 0.04, 0.04)), (1.0, (0.16, 0.16, 0.16))])
    g.finish(colour, block, 1.0)


def adobe(g):
    plaster = g.noise(8.0, 6, 0.6, 231.0)
    cracks = g.math("POWER", g.math("SUBTRACT", 1.0, g.voronoi(3.0, "DISTANCE_TO_EDGE", offset=233.0)), 24.0)
    colour = g.ramp(plaster, [(0.3, (0.70, 0.52, 0.36)), (0.75, (0.88, 0.72, 0.52))])
    colour = g.mix(g.math("MULTIPLY", cracks, 0.7), colour, (0.35, 0.22, 0.14, 1.0))
    g.finish(colour, g.math("SUBTRACT", plaster, g.math("MULTIPLY", cracks, 0.4)), 0.4)


def steel_plates(g):
    edge, cell = g.cells(3.0, 3.0, 0.0)
    rust = g.noise(12.0, 6, 0.6, 237.0)
    plate, seam = bt.bevel(g, edge, 0.01, 0.03, rust)
    paint = g.ramp(cell, [(0.0, (0.34, 0.37, 0.36)), (1.0, (0.46, 0.48, 0.45))])
    colour = g.mix(g.math("MULTIPLY", g.math("GREATER_THAN", rust, 0.56), 0.85), paint, (0.42, 0.22, 0.10, 1.0))
    colour = g.mix(g.math("SUBTRACT", 1.0, seam), colour, (0.15, 0.15, 0.15, 1.0))
    g.finish(colour, plate, 0.6)


def fire(g):
    flame = g.noise(4.0, 8, 0.6, 241.0, distortion=1.5)
    g.finish(g.ramp(flame, [(0.25, (0.20, 0.03, 0.0)), (0.55, (0.95, 0.40, 0.05)), (0.8, (1.0, 0.85, 0.40))]),
             flame, 0.0)


def build_textures() -> None:
    bt.setup_bake_scene()
    tiles = [bt.bake(sand), bt.bake(dune_shadow), bt.bake(desert_rock), bt.bake(track_road)]
    size = bt.SIZE
    atlas = np.zeros((size * 2, size * 2, 4), dtype=np.float32)
    # PREPARE MATRIX TEXTURE numbers tiles from the top left, row by row.
    atlas[size:, :size], atlas[size:, size:] = tiles[0], tiles[1]
    atlas[:size, :size], atlas[:size, size:] = tiles[2], tiles[3]
    bt.save_jpeg(atlas, OUT / "textures/desert.jpg")
    for builder, name in ((camo, "camo"), (treads, "treads"), (tyre, "tyre"), (adobe, "adobe"),
                          (steel_plates, "steel"), (pistol_metal, "gunmetal"), (fire, "fire")):
        bt.save_jpeg(bt.bake(builder), OUT / f"textures/{name}.jpg")


# ---------------------------------------------------------------------------
# Models (Blender: +Y is forward, +Z is up). Spinning parts get their turn
# baked into the mesh, so ROTATE LIMB starts from zero.

def turned(kind, name, size, location, parent, order, mat, rotation, offset=(0, 0, 0), bevel=0.0, smooth=False):
    obj = part(kind, name, size, location, parent, order, mat, offset, bevel, smooth)
    obj.data.transform(rotation)
    return obj


AXLE = Matrix.Rotation(math.pi / 2, 4, "Y")    # cylinder axis along X: a wheel
FORWARD = Matrix.Rotation(-math.pi / 2, 4, "X")  # cylinder axis along +Y: a barrel


def build_tank():
    paint, belt, steel = material("camo"), material("treads"), material("gunmetal")
    hull = part("cube", "hull", (4.0, 6.4, 1.3), (0, 0, 1.2), None, 0, paint, bevel=0.25)
    part("cube", "track_l", (1.1, 7.0, 1.4), (-2.5, 0, -0.35), hull, 1, belt, bevel=0.35)
    part("cube", "track_r", (1.1, 7.0, 1.4), (2.5, 0, -0.35), hull, 2, belt, bevel=0.35)
    turret = part("cube", "turret", (2.6, 3.0, 1.0), (0, -0.4, 1.1), hull, 3, paint, (0, 0, 0.5), 0.3)
    turned("cylinder", "barrel", (0.36, 0.36, 4.6), (0, 1.3, 0.55), turret, 4, steel, FORWARD, (0, 0, 2.3))
    mast = turned("cylinder", "mast", (0.15, 0.15, 1.2), (0.9, -1.1, 1.0), turret, 5, steel, Matrix(), (0, 0, 0.6))
    part("cone", "dish", (1.2, 1.2, 0.35), (0, 0, 1.25), mast, 6, steel)
    part("cylinder", "hatch", (0.9, 0.9, 0.25), (-0.5, -0.6, 1.05), turret, 7, paint)
    for side, sign in (("l", -1), ("r", 1)):
        turned("cylinder", f"exhaust_{side}", (0.3, 0.3, 0.8), (sign * 1.4, -3.3, 0.4), hull, 8 + (sign > 0), steel,
               FORWARD)
    return hull


def build_rattler():
    paint, rubber, steel = material("camo"), material("tyre"), material("gunmetal")
    body = part("cube", "body", (2.8, 6.0, 1.5), (0, 0, 1.6), None, 0, paint, bevel=0.2)
    order = 1
    for y in (-2.0, 0.0, 2.0):
        for side, sign in (("l", -1), ("r", 1)):
            turned("cylinder", f"wheel_{side}{order}", (1.4, 1.4, 0.6), (sign * 1.7, y, -0.7), body, order, rubber,
                   AXLE, smooth=True)
            order += 1
    part("cube", "cabin", (2.4, 1.8, 1.0), (0, 1.8, 0.9), body, 7, paint, bevel=0.15)
    gunner = part("cylinder", "gun_ring", (1.2, 1.2, 0.5), (0, -1.0, 0.95), body, 8, steel)
    turned("cylinder", "gun", (0.2, 0.2, 2.2), (0, 0.2, 0.3), gunner, 9, steel, FORWARD, (0, 0, 1.1))
    return body


def build_radar_tower():
    steel = material("steel")
    base = part("cube", "base", (4.0, 4.0, 0.6), (0, 0, 0.3), None, 0, steel)
    order = 1
    for x in (-1.6, 1.6):
        for y in (-1.6, 1.6):
            leg = turned("cylinder", f"leg{order}", (0.3, 0.3, 9.0), (x, y, 0.3), base, order, steel, Matrix(),
                         (0, 0, 4.5))
            leg.rotation_euler = (math.copysign(0.08, -y), math.copysign(0.08, x), 0)
            order += 1
    deck = part("cube", "deck", (3.6, 3.6, 0.4), (0, 0, 9.2), base, 5, steel)
    part("cylinder", "dish", (5.0, 0.8, 2.6), (0, 0, 1.9), deck, 6, steel, smooth=True)
    return base


def build_watchtower():
    wall, steel = material("adobe"), material("steel")
    base = part("cylinder", "base", (3.0, 3.0, 8.0), (0, 0, 0), None, 0, wall, (0, 0, 4.0))
    cabin = part("cube", "cabin", (4.2, 4.2, 2.2), (0, 0, 8.0), base, 1, wall, (0, 0, 1.1), 0.1)
    part("cone", "roof", (5.6, 5.6, 1.6), (0, 0, 2.2), cabin, 2, steel, (0, 0, 0.8))
    return base


def build_house(domed: bool):
    wall, steel = material("adobe"), material("steel")
    house = part("cube", "house", (9.0, 7.0, 4.0), (0, 0, 0), None, 0, wall, (0, 0, 2.0), 0.15)
    part("cube", "door", (1.6, 0.3, 2.6), (0, -3.5, 0), house, 1, steel, (0, 0, 1.3))
    if domed:
        part("sphere", "dome", (6.0, 6.0, 5.0), (0, 0, 4.0), house, 2, wall, smooth=True)
    else:
        part("cube", "parapet", (9.4, 7.4, 0.8), (0, 0, 4.0), house, 2, wall, (0, 0, 0.4), 0.1)
    return house


def acacia():
    trunk, leaves = material("acacia_trunk", (0.55, 0.40, 0.26, 1.0)), material("acacia_leaves", (0.22, 0.36, 0.10, 1.0))
    # The tree stands 2.6 units low so the orthographic camera at the origin frames it.
    root = part("cylinder", "trunk", (0.6, 0.6, 3.0), (0, 0, -2.6), None, 0, trunk, (0, 0, 1.5))
    for index, angle in enumerate((-0.5, 0.45)):
        branch = part("cylinder", f"branch{index}", (0.35, 0.35, 2.2), (0, 0, 2.6), root, 1 + index, trunk, (0, 0, 1.1))
        branch.rotation_euler = (0, angle, 0)
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=3, radius=1.0)
    crown = bpy.context.active_object
    crown.scale = (3.4, 3.0, 0.75)
    crown.location = (0, 0, 1.7)
    lumps = bpy.data.textures.new("crown", "CLOUDS")
    lumps.noise_scale = 0.5
    displace = crown.modifiers.new("displace", "DISPLACE")
    displace.texture = lumps
    displace.strength = 0.9
    crown.data.materials.append(leaves)
    return root


def main() -> None:
    build_textures()
    for builder, name, textures in (
            (build_tank, "hunter", {"camo": "camo.jpg", "treads": "treads.jpg", "gunmetal": "gunmetal.jpg"}),
            (build_rattler, "rattler", {"camo": "camo.jpg", "tyre": "tyre.jpg", "gunmetal": "gunmetal.jpg"}),
            (build_radar_tower, "radar_tower", {"steel": "steel.jpg"}),
            (build_watchtower, "watchtower", {"adobe": "adobe.jpg", "steel": "steel.jpg"}),
            (lambda: build_house(False), "house", {"adobe": "adobe.jpg", "steel": "steel.jpg"}),
            (lambda: build_house(True), "dome_house", {"adobe": "adobe.jpg", "steel": "steel.jpg"})):
        bpy.ops.wm.read_factory_settings(use_empty=True)
        xexport.export(OUT / f"models/{name}.x", [builder()], textures)
    # Seen from the side, lit from the camera's side and a little above.
    sprite((acacia,), {}, OUT / "textures/acacia.bmp", 128, 9.0, math.radians(88.0),
           (math.radians(65.0), 0.0, math.radians(-25.0)))


if __name__ == "__main__":
    main()
    sys.exit(0)
