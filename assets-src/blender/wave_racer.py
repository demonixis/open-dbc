"""Build the Wave Racer demo media: a jet scooter, its rider, course buoys, a
jump ramp, a lighthouse and their textures.

Run from the repository root:
    /Applications/Blender.app/Contents/MacOS/Blender --background --factory-startup \
        --python assets-src/blender/wave_racer.py
"""

import math
import sys
from pathlib import Path

import bpy

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bake_textures as bt  # noqa: E402
import xexport  # noqa: E402
from flooded_vault import material  # noqa: E402
from frost_outpost import part, pistol_metal, polar  # noqa: E402

OUT = bt.ROOT / "examples/dbc/demos/wave_racer"


# ---------------------------------------------------------------------------
# Textures

def sea(g):
    swell = g.noise(2.0, 6, 0.55, 111.0, distortion=0.4)
    foam = g.math("POWER", g.math("SUBTRACT", 1.0, g.voronoi(4.0, "DISTANCE_TO_EDGE", offset=113.0)), 16.0)
    water = g.ramp(swell, [(0.2, (0.02, 0.10, 0.22)), (0.8, (0.06, 0.32, 0.48))])
    g.finish(g.mix(g.math("MULTIPLY", foam, 0.7), water, (0.85, 0.93, 0.97, 1.0)), swell, 0.5)


def sky(g):
    # The horizon is the middle of the texture: a sphere looks right whichever pole is up.
    height = g.math("ABSOLUTE", g.math("SUBTRACT", g.v, 0.5))
    gradient = g.ramp(height, [(0.0, (0.55, 0.70, 0.88)), (0.06, (0.25, 0.48, 0.82)), (0.5, (0.06, 0.20, 0.55))])
    cloud = g.math("MULTIPLY", g.math("POWER", g.noise(3.0, 8, 0.6, 117.0), 3.5),
                   g.math("LESS_THAN", height, 0.35))
    g.finish(g.mix(g.math("MULTIPLY", cloud, 1.8, clamp=True), gradient, (1.0, 1.0, 1.0, 1.0)), cloud, 0.0)


def livery(g):
    stripe = g.math("LESS_THAN", g.math("ABSOLUTE", g.math("SUBTRACT", g.math("FRACT", g.math(
        "ADD", g.math("MULTIPLY", g.u, 2.0), g.math("MULTIPLY", g.v, 1.0))), 0.5)), 0.12)
    paint = g.mix(stripe, (0.92, 0.93, 0.95, 1.0), (1.0, 0.40, 0.05, 1.0))
    flecks = g.noise(20.0, 4, 0.5, 121.0)
    g.finish(g.mix(g.math("MULTIPLY", flecks, 0.15), paint, (0.3, 0.3, 0.3, 1.0)), flecks, 0.2)


def suit(g):
    band = g.math("LESS_THAN", g.math("ABSOLUTE", g.math("SUBTRACT", g.math("FRACT", g.math("MULTIPLY", g.v, 3.0)), 0.5)), 0.06)
    fabric = g.ramp(g.noise(30.0, 4, 0.5, 123.0), [(0.3, (0.04, 0.06, 0.14)), (0.8, (0.10, 0.14, 0.28))])
    g.finish(g.mix(band, fabric, (0.2, 1.0, 0.6, 1.0)), g.noise(30.0, 4, 0.5, 123.0), 0.4)


def bands(g):
    # Red and white rings for the buoys and the lighthouse.
    ring = g.math("LESS_THAN", g.math("FRACT", g.math("MULTIPLY", g.v, 4.0)), 0.5)
    paint = g.mix(ring, (0.95, 0.95, 0.92, 1.0), (0.80, 0.08, 0.06, 1.0))
    wear = g.noise(12.0, 6, 0.6, 127.0)
    g.finish(g.mix(g.math("MULTIPLY", g.math("GREATER_THAN", wear, 0.72), 0.4), paint, (0.35, 0.3, 0.28, 1.0)),
             wear, 0.4)


def spray(g):
    _, _, radius, _ = polar(g)
    puff = g.math("MULTIPLY", g.math("POWER", g.math("SUBTRACT", 1.0, g.math("MINIMUM", g.math("MULTIPLY", radius, 2.0), 1.0)), 1.5),
                  g.math("ADD", 0.5, g.math("MULTIPLY", g.noise(6.0, 4, 0.5, 131.0), 0.8)))
    g.finish(g.ramp(puff, [(0.0, (0.0, 0.0, 0.0)), (1.0, (0.9, 0.95, 1.0))]), puff, 0.0)


def build_textures() -> None:
    bt.setup_bake_scene()
    for builder, name in ((sea, "sea"), (sky, "sky"), (livery, "livery"), (suit, "suit"), (bands, "bands"),
                          (spray, "spray"), (pistol_metal, "ramp"),
                          (bt.dark_rock, "rock")):
        bt.save_jpeg(bt.bake(builder), OUT / f"{name}.jpg")


# ---------------------------------------------------------------------------
# Models (Blender: +Y is forward, +Z is up)

def taper_front(obj, factor):
    # Narrow and lower the bow: vertices towards +Y shrink in X and Z.
    for vertex in obj.data.vertices:
        if vertex.co.y > 0:
            vertex.co.x *= factor
            vertex.co.z = vertex.co.z * factor + 0.1


def build_scooter():
    paint, trim = material("livery"), material("scooter_trim", (0.08, 0.08, 0.09, 1.0))
    flag_cloth = material("flag", (1.0, 0.85, 0.1, 1.0))
    hull = part("cube", "hull", (1.8, 4.2, 1.1), (0, 0, 0), None, 0, paint, bevel=0.18)
    taper_front(hull, 0.55)
    part("cube", "seat", (0.7, 1.4, 0.35), (0, -0.6, 0.55), hull, 1, trim, bevel=0.1, smooth=True)
    part("cube", "handlebar", (1.3, 0.15, 0.15), (0, 0.6, 1.1), hull, 2, trim, bevel=0.04)
    part("cylinder", "jet", (0.45, 0.45, 0.5), (0, -2.1, -0.1), hull, 3, trim).rotation_euler = (math.pi / 2, 0, 0)
    flag = part("cube", "flag", (0.05, 0.9, 0.6), (0, -1.6, 1.5), hull, 4, flag_cloth, (0, -0.45, 0.3))
    flag.rotation_euler = (0, 0, 0)
    return hull


def build_rider():
    suit_mat, helmet = material("suit"), material("helmet", (0.95, 0.85, 0.1, 1.0))
    pelvis = part("cube", "pelvis", (0.7, 0.45, 0.4), (0, -0.6, 1.0), None, 0, suit_mat, bevel=0.1, smooth=True)
    torso = part("cube", "torso", (0.9, 0.55, 1.0), (0, 0, 0.15), pelvis, 1, suit_mat, (0, 0.1, 0.5), 0.15, smooth=True)
    torso.rotation_euler = (0.35, 0, 0)  # leaning forward over the bars
    part("sphere", "helmet", (0.6, 0.65, 0.6), (0, 0.2, 1.05), torso, 2, helmet, (0, 0.05, 0.3), smooth=True)
    order = 3
    for side, sign in (("l", -1), ("r", 1)):
        arm = part("cube", f"arm_{side}", (0.22, 0.22, 0.6), (sign * 0.55, 0.15, 0.85), torso, order, suit_mat,
                   (0, 0, -0.3), 0.05, smooth=True)
        arm.rotation_euler = (-1.0, 0, 0)
        fore = part("cube", f"forearm_{side}", (0.2, 0.2, 0.55), (0, 0, -0.6), arm, order + 1, suit_mat,
                    (0, 0, -0.27), 0.05, smooth=True)
        fore.rotation_euler = (-0.5, 0, 0)
        order += 2
    for side, sign in (("l", -1), ("r", 1)):
        thigh = part("cube", f"thigh_{side}", (0.26, 0.26, 0.7), (sign * 0.25, 0.1, -0.1), pelvis, order, suit_mat,
                     (0, 0, -0.35), 0.06, smooth=True)
        thigh.rotation_euler = (1.2, 0, sign * 0.15)
        shin = part("cube", f"shin_{side}", (0.22, 0.22, 0.65), (0, 0, -0.7), thigh, order + 1, suit_mat,
                    (0, 0, -0.32), 0.05, smooth=True)
        shin.rotation_euler = (-1.4, 0, 0)
        order += 2
    return pelvis


def build_buoy():
    paint, lamp = material("bands"), material("buoy_lamp", (1.0, 0.95, 0.4, 1.0))
    float_ = part("sphere", "float", (2.2, 2.2, 1.6), (0, 0, 0), None, 0, paint, smooth=True)
    mast = part("cylinder", "mast", (0.25, 0.25, 3.0), (0, 0, 0.6), float_, 1, paint, (0, 0, 1.5))
    part("sphere", "lamp", (0.6, 0.6, 0.6), (0, 0, 3.0), mast, 2, lamp, smooth=True)
    return float_


def build_ramp():
    metal = material("ramp")
    # A 12-unit-long slope rising 4 units along +Y, with rails and floats.
    deck = part("cube", "deck", (6.0, 12.0, 0.3), (0, 0, 0), None, 0, metal, bevel=0.05)
    deck.rotation_euler = (math.atan2(4.0, 12.0), 0, 0)
    deck.location = (0, 0, 2.0)
    part("cube", "rail_l", (0.3, 12.0, 0.6), (-3.0, 0, 0.4), deck, 1, metal, bevel=0.05)
    part("cube", "rail_r", (0.3, 12.0, 0.6), (3.0, 0, 0.4), deck, 2, metal, bevel=0.05)
    part("cylinder", "float", (1.6, 1.6, 6.0), (0, 4.5, -1.2), deck, 3, metal).rotation_euler = (0, math.pi / 2, 0)
    return deck


def build_lighthouse():
    stone, paint = material("rock"), material("bands")
    lamp = material("lighthouse_lamp", (1.0, 0.9, 0.5, 1.0))
    rock = part("sphere", "island", (60.0, 50.0, 18.0), (0, 0, -4.0), None, 0, stone, smooth=True)
    tower = part("cone", "tower", (9.0, 9.0, 34.0), (0, 0, 8.0), rock, 1, paint, (0, 0, 17.0))
    gallery = part("cylinder", "gallery", (8.0, 8.0, 1.2), (0, 0, 31.0), tower, 2, stone)
    part("cylinder", "lantern", (4.0, 4.0, 4.0), (0, 0, 2.6), gallery, 3, lamp, smooth=True)
    return rock


def main() -> None:
    build_textures()
    for builder, name, textures in (
            (build_scooter, "scooter", {"livery": "livery.jpg"}),
            (build_rider, "rider", {"suit": "suit.jpg"}),
            (build_buoy, "buoy", {"bands": "bands.jpg"}),
            (build_ramp, "ramp", {"ramp": "ramp.jpg"}),
            (build_lighthouse, "lighthouse", {"rock": "rock.jpg", "bands": "bands.jpg"})):
        bpy.ops.wm.read_factory_settings(use_empty=True)
        xexport.export(OUT / f"{name}.x", [builder()], textures)


if __name__ == "__main__":
    main()
    sys.exit(0)
