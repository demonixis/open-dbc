"""Build the Dusk Rally demo media: a coastal circuit cut into chunks, the island
ground and its painted map, two sky domes, the cars, the trackside props and
their textures.

Run from the repository root:
    /Applications/Blender.app/Contents/MacOS/Blender --background --factory-startup \
        --python assets-src/blender/dusk_rally.py

The circuit centreline is the Catmull-Rom spline through CONTROL, sampled
exactly as dusk_rally.dba samples it with CATMULLROM VECTOR3, so the road the
game drives on and the road it draws are the same curve. Game coordinates are
(x, height, z); Blender builds them as (x, z, height) and xexport.py swaps the
axes back.
"""

import math
import sys
from pathlib import Path

import bmesh
import bpy
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bake_textures as bt  # noqa: E402
import xexport  # noqa: E402
from flooded_vault import material  # noqa: E402
from frost_outpost import part  # noqa: E402

OUT = bt.ROOT / "examples/dbpro/demos/dusk_rally"

# Shared with dusk_rally.dba: keep these values identical in both files.
CONTROL = [(0.0, -225.0), (187.5, -240.0), (315.0, -165.0), (337.5, -15.0), (247.5, 90.0), (285.0, 210.0),
           (187.5, 300.0), (30.0, 285.0), (-90.0, 195.0), (-112.5, 60.0), (-240.0, 30.0), (-315.0, -90.0),
           (-247.5, -210.0), (-112.5, -225.0)]
SAMPLES_PER_SPAN = 24
ROAD_HALF, CURB_OUT, BARRIER = 7.0, 8.6, 14.0
LAMP_STEP, LAMP_SIDE, LAMP_REACH, LAMP_HEIGHT = 12, 16.5, 12.5, 7.4
CHUNKS = 16
COAST_CENTRE = (15.0, 30.0)
SUN_AZIMUTH = math.radians(-35.0)  # the sun sets over the sea, ahead of the main straight
GROUND_SIZE = 1100.0
GROUND_PIXELS = 2048
# Textures seen large on screen keep 1024 x 1024; the others are halved.
FULL_SIZE = {"asphalt", "barrier", "sea", "rock"}


# ---------------------------------------------------------------------------
# Circuit geometry

def catmull_rom(p0, p1, p2, p3, s):
    # The D3DX formula behind CATMULLROM VECTOR3.
    s2, s3 = s * s, s * s * s
    return tuple(0.5 * ((2 * b) + (-a + c) * s + (2 * a - 5 * b + 4 * c - d) * s2 + (-a + 3 * b - 3 * c + d) * s3)
                 for a, b, c, d in zip(p0, p1, p2, p3))


def centreline():
    count = len(CONTROL)
    points = []
    for span in range(count):
        p = [CONTROL[(span + offset) % count] for offset in (-1, 0, 1, 2)]
        for step in range(SAMPLES_PER_SPAN):
            points.append(catmull_rom(*p, step / SAMPLES_PER_SPAN))
    total = len(points)
    rights, lengths, curvature = [], [0.0], []
    for k in range(total):
        ax, az = points[k - 1]
        bx, bz = points[(k + 1) % total]
        tx, tz = bx - ax, bz - az
        length = math.hypot(tx, tz)
        # Left-handed, y up: the right of a heading (tx, tz) is (tz, -tx).
        rights.append((tz / length, -tx / length))
    for k in range(total):
        lengths.append(lengths[-1] + math.dist(points[k], points[(k + 1) % total]))
    for k in range(total):
        (px, pz), (qx, qz) = rights[k - 1], rights[(k + 1) % total]
        turn = math.atan2(px * qz - pz * qx, px * qx + pz * qz)
        curvature.append(abs(turn) / max(lengths[k + 1] - lengths[k - 1] if k else lengths[2], 1e-3))
    return points, rights, lengths, curvature


def coast_radius(angle):
    # The island: a wavy circle pulled in towards the sea south of the main straight, with a headland.
    south = max(0.0, -math.sin(angle)) ** 3
    headland = math.exp(-((angle - math.radians(-150.0) + math.pi) % (2 * math.pi) - math.pi) ** 2 / 0.02)
    return (445.0 - 110.0 * south + 60.0 * headland + 9.0 * math.sin(3 * angle + 0.5) +
            6.0 * math.sin(7 * angle + 2.1))


def at(point, right, lateral, height):
    return point[0] + right[0] * lateral, point[1] + right[1] * lateral, height


# ---------------------------------------------------------------------------
# Textures

def asphalt_surface(g):
    grit = g.noise(40.0, 6, 0.7, 201.0)
    patches = g.noise(3.0, 4, 0.5, 203.0)
    colour = g.ramp(g.math("ADD", g.math("MULTIPLY", grit, 0.6), g.math("MULTIPLY", patches, 0.4)),
                    [(0.25, (0.07, 0.07, 0.08)), (0.75, (0.20, 0.19, 0.20))])
    # Edge lines run along v at both sides; a dashed centre line, two dashes per tile.
    edge = g.math("LESS_THAN", g.math("ABSOLUTE", g.math("SUBTRACT", g.math("ABSOLUTE", g.math("SUBTRACT", g.u, 0.5)),
                                                         0.455)), 0.012)
    dash = g.math("MULTIPLY", g.math("LESS_THAN", g.math("ABSOLUTE", g.math("SUBTRACT", g.u, 0.5)), 0.008),
                  g.math("LESS_THAN", g.math("FRACT", g.math("MULTIPLY", g.v, 2.0)), 0.45))
    paint = g.math("MULTIPLY", g.math("MAXIMUM", edge, dash), g.math("ADD", 0.55, g.math("MULTIPLY", grit, 0.45)))
    skid = g.math("MULTIPLY", g.math("POWER", g.noise(1.5, 3, 0.5, 205.0, distortion=1.5), 6.0), 0.6)
    colour = g.mix(skid, colour, (0.03, 0.03, 0.03, 1.0))
    return g.mix(paint, colour, (0.92, 0.90, 0.82, 1.0)), grit


def asphalt(g):
    colour, height = asphalt_surface(g)
    g.finish(colour, height, 0.25)


def curb(g):
    stripe = g.math("LESS_THAN", g.math("FRACT", g.math("MULTIPLY", g.v, 2.0)), 0.5)
    paint = g.mix(stripe, (0.92, 0.92, 0.90, 1.0), (0.78, 0.07, 0.05, 1.0))
    wear = g.noise(15.0, 6, 0.6, 211.0)
    dirt = g.math("MULTIPLY", g.math("GREATER_THAN", wear, 0.66), 0.5)
    g.finish(g.mix(dirt, paint, (0.25, 0.22, 0.2, 1.0)), wear, 0.3)


def barrier_surface(g):
    # Concrete blocks with red and white chevron panels on the upper half.
    edge, cell = g.cells(4, 1, 0.0)
    surface = g.noise(12.0, 6, 0.6, 221.0)
    height, face = bt.bevel(g, edge, 0.004, 0.02, surface, 0.25)
    concrete = g.ramp(g.math("ADD", g.math("MULTIPLY", cell, 0.3), g.math("MULTIPLY", surface, 0.7)),
                      [(0.2, (0.42, 0.42, 0.44)), (0.8, (0.70, 0.70, 0.70))])
    chevron = g.math("LESS_THAN", g.math("FRACT", g.math("ADD", g.math("MULTIPLY", g.u, 8.0),
                                                           g.math("MULTIPLY", g.math("ABSOLUTE", g.math(
                                                               "SUBTRACT", g.v, 0.72)), 3.0))), 0.5)
    band = g.math("GREATER_THAN", g.v, 0.48)
    panel = g.mix(chevron, (0.92, 0.92, 0.9, 1.0), (0.80, 0.10, 0.06, 1.0))
    colour = g.mix(band, concrete, panel)
    colour = g.mix(face, (0.08, 0.08, 0.08, 1.0), colour)
    return colour, height


def barrier(g):
    colour, height = barrier_surface(g)
    g.finish(colour, height, 0.4)


def grass(g):
    blades = g.noise(30.0, 8, 0.7, 231.0)
    tufts = g.noise(5.0, 6, 0.6, 233.0)
    colour = g.ramp(g.math("ADD", g.math("MULTIPLY", blades, 0.5), g.math("MULTIPLY", tufts, 0.5)),
                    [(0.25, (0.09, 0.17, 0.05)), (0.55, (0.20, 0.30, 0.08)), (0.8, (0.40, 0.42, 0.16))])
    g.finish(colour, blades, 0.5)


def sand(g):
    ripples = g.math("SINE", g.math("ADD", g.math("MULTIPLY", g.v, 90.0),
                                    g.math("MULTIPLY", g.noise(2.0, 4, 0.5, 241.0), 14.0)))
    grain = g.noise(30.0, 6, 0.6, 243.0)
    colour = g.ramp(g.math("ADD", g.math("MULTIPLY", grain, 0.7), g.math("MULTIPLY", ripples, 0.08)),
                    [(0.3, (0.55, 0.45, 0.30)), (0.8, (0.82, 0.72, 0.52))])
    g.finish(colour, g.math("ADD", grain, g.math("MULTIPLY", ripples, 0.2)), 0.5)


def gravel(g):
    stones = g.voronoi(30.0, "F1", "Distance", 251.0)
    height = g.math("SUBTRACT", 1.0, g.math("MULTIPLY", stones, 1.5), clamp=True)
    colour = g.ramp(g.math("ADD", g.math("MULTIPLY", height, 0.6), g.math("MULTIPLY", g.noise(20.0, 4, 0.5, 253.0), 0.4)),
                    [(0.2, (0.30, 0.26, 0.22)), (0.8, (0.62, 0.56, 0.48))])
    g.finish(colour, height, 0.8)


def detail(g):
    # Centred on mid grey: stage 1 multiplies it in at twice its value, so the average stays neutral.
    fine = g.noise(60.0, 8, 0.7, 261.0)
    coarse = g.noise(12.0, 4, 0.5, 263.0)
    value = g.math("ADD", 0.25, g.math("MULTIPLY", g.math("ADD", g.math("MULTIPLY", fine, 0.6),
                                                          g.math("MULTIPLY", coarse, 0.4)), 0.5))
    g.finish(g.ramp(value, [(0.0, (0.0, 0.0, 0.0)), (1.0, (1.0, 1.0, 1.0))]), fine, 0.0)


def sea(g):
    swell = g.noise(3.0, 6, 0.55, 271.0, distortion=0.5)
    glints = g.math("POWER", g.math("SUBTRACT", 1.0, g.voronoi(10.0, "DISTANCE_TO_EDGE", offset=273.0)), 14.0)
    water = g.ramp(swell, [(0.2, (0.03, 0.12, 0.20)), (0.8, (0.08, 0.30, 0.40))])
    g.finish(g.mix(g.math("MULTIPLY", glints, 0.5), water, (0.80, 0.85, 0.80, 1.0)), swell, 0.4)


def rock_surface(g):
    strata = g.math("SINE", g.math("ADD", g.math("MULTIPLY", g.v, 40.0), g.math("MULTIPLY", g.noise(2.0, 5, 0.6, 281.0), 8.0)))
    cracks = g.voronoi(4.0, "DISTANCE_TO_EDGE", offset=283.0)
    lumps = g.noise(6.0, 8, 0.6, 285.0)
    height = g.math("ADD", g.math("MULTIPLY", lumps, 0.6), g.math("ADD", g.math("MULTIPLY", cracks, 0.5),
                                                                  g.math("MULTIPLY", strata, 0.08)))
    colour = g.ramp(g.math("ADD", g.math("MULTIPLY", lumps, 0.7), g.math("MULTIPLY", strata, 0.1)),
                    [(0.3, (0.30, 0.22, 0.17)), (0.8, (0.62, 0.50, 0.40))])
    return colour, height


def rock(g):
    colour, height = rock_surface(g)
    g.finish(colour, height, 0.4)


def normal_map(surface, strength, name):
    """A tangent-space normal map of a surface's height, for SET BUMP MAPPING ON."""
    def builder(g):
        _, height = surface(g)
        bump = g.node("ShaderNodeBump")
        bump.inputs["Strength"].default_value = 1.0
        bump.inputs["Distance"].default_value = strength
        g.link(height, bump.inputs["Height"])
        # The bake plane faces +Z with its U along +X and V along +Y, so the world normal is the tangent-space one.
        encode = g.node("ShaderNodeVectorMath", operation="MULTIPLY_ADD")
        g.link(bump.outputs["Normal"], encode.inputs[0])
        encode.inputs[1].default_value = (0.5, 0.5, 0.5)
        encode.inputs[2].default_value = (0.5, 0.5, 0.5)
        emission = g.node("ShaderNodeEmission")
        g.link(encode.outputs[0], emission.inputs["Color"])
        output = g.node("ShaderNodeOutputMaterial")
        g.link(emission.outputs[0], output.inputs["Surface"])
    builder.__name__ = name
    return builder


def livery(base, accent, seed):
    def builder(g):
        # cube_project maps the sides of the hull: twin stripes run along the car.
        stripe = g.math("LESS_THAN", g.math("ABSOLUTE", g.math("SUBTRACT", g.math("ABSOLUTE", g.math("SUBTRACT", g.u, 0.5)),
                                                               0.07)), 0.035)
        flake = g.noise(60.0, 4, 0.5, seed)
        paint = g.mix(g.math("MULTIPLY", flake, 0.12), (*base, 1.0), (1.0, 1.0, 1.0, 1.0))
        g.finish(g.mix(stripe, paint, (*accent, 1.0)), flake, 0.1)
    builder.__name__ = f"livery_{seed}"
    return builder


def wheel(g):
    # Blender's cylinder UVs: the tread fills the top half, the two hubs are discs centred at v = 0.25.
    hub_u = g.math("SUBTRACT", g.math("FRACT", g.math("MULTIPLY", g.u, 2.0)), 0.5)
    hub_v = g.math("MULTIPLY", g.math("SUBTRACT", g.v, 0.25), 2.0)
    radius = g.math("SQRT", g.math("ADD", g.math("MULTIPLY", hub_u, hub_u), g.math("MULTIPLY", hub_v, hub_v)))
    angle = g.math("ARCTAN2", hub_v, hub_u)
    spokes = g.math("GREATER_THAN", g.math("SINE", g.math("MULTIPLY", angle, 5.0)), 0.3)
    rim = g.math("MULTIPLY", g.math("LESS_THAN", radius, 0.36), g.math("GREATER_THAN", radius, 0.08))
    alloy = g.mix(g.math("MULTIPLY", rim, spokes), (0.10, 0.10, 0.11, 1.0), (0.75, 0.76, 0.80, 1.0))
    tread = g.math("GREATER_THAN", g.math("FRACT", g.math("MULTIPLY", g.u, 40.0)), 0.5)
    rubber = g.mix(g.math("MULTIPLY", tread, 0.4), (0.05, 0.05, 0.05, 1.0), (0.10, 0.10, 0.11, 1.0))
    g.finish(g.mix(g.math("GREATER_THAN", g.v, 0.5), alloy, rubber), tread, 0.2)


def bark(g):
    rings = g.math("SINE", g.math("ADD", g.math("MULTIPLY", g.v, 70.0), g.math("MULTIPLY", g.noise(4.0, 3, 0.5, 291.0), 3.0)))
    fibre = g.noise(25.0, 6, 0.6, 293.0)
    colour = g.ramp(g.math("ADD", g.math("MULTIPLY", rings, 0.25), g.math("MULTIPLY", fibre, 0.6)),
                    [(0.2, (0.20, 0.13, 0.08)), (0.9, (0.55, 0.42, 0.28))])
    g.finish(colour, g.math("ADD", g.math("MULTIPLY", rings, 0.4), fibre), 0.7)


def frond(g):
    # Leaflets fan out from the midrib along v; the gaps stay dark, the frond is drawn solid.
    midrib = g.math("ABSOLUTE", g.math("SUBTRACT", g.u, 0.5))
    leaflets = g.math("FRACT", g.math("ADD", g.math("MULTIPLY", g.v, 24.0), g.math("MULTIPLY", midrib, 6.0)))
    leaf = g.math("LESS_THAN", leaflets, 0.75)
    colour = g.ramp(g.math("ADD", g.math("MULTIPLY", leaflets, 0.5), g.math("MULTIPLY", g.v, 0.4)),
                    [(0.1, (0.05, 0.16, 0.04)), (0.9, (0.28, 0.46, 0.12))])
    g.finish(g.mix(leaf, (0.03, 0.07, 0.02, 1.0), colour), leaf, 0.3)


def concrete(g):
    edge, cell = g.cells(2, 2, 0.0)
    surface = g.noise(10.0, 6, 0.6, 301.0)
    height, face = bt.bevel(g, edge, 0.003, 0.01, surface, 0.3)
    colour = g.ramp(g.math("ADD", g.math("MULTIPLY", cell, 0.2), g.math("MULTIPLY", surface, 0.8)),
                    [(0.2, (0.45, 0.44, 0.42)), (0.8, (0.75, 0.73, 0.70))])
    g.finish(g.mix(face, (0.2, 0.2, 0.2, 1.0), colour), height, 0.5)


def seats(g):
    rows = g.math("LESS_THAN", g.math("FRACT", g.math("MULTIPLY", g.v, 6.0)), 0.55)
    seat = g.math("LESS_THAN", g.math("FRACT", g.math("MULTIPLY", g.u, 24.0)), 0.8)
    paint = g.mix(g.math("GREATER_THAN", g.noise(3.0, 2, 0.5, 303.0), 0.5), (0.10, 0.30, 0.65, 1.0), (0.90, 0.75, 0.15, 1.0))
    colour = g.mix(g.math("MULTIPLY", rows, seat), (0.35, 0.35, 0.36, 1.0), paint)
    g.finish(colour, g.math("MULTIPLY", rows, seat), 0.4)


def tower(g):
    band = g.math("LESS_THAN", g.math("FRACT", g.math("MULTIPLY", g.v, 3.0)), 0.5)
    wear = g.noise(10.0, 6, 0.6, 311.0)
    paint = g.mix(band, (0.95, 0.94, 0.90, 1.0), (0.12, 0.16, 0.30, 1.0))
    g.finish(g.mix(g.math("MULTIPLY", g.math("GREATER_THAN", wear, 0.7), 0.3), paint, (0.3, 0.3, 0.3, 1.0)), wear, 0.4)


def flare(g):
    # Drawn additively on camera-facing quads around lamps and headlights.
    dx, dy = g.math("SUBTRACT", g.u, 0.5), g.math("SUBTRACT", g.v, 0.5)
    radius = g.math("SQRT", g.math("ADD", g.math("MULTIPLY", dx, dx), g.math("MULTIPLY", dy, dy)))
    glow = g.math("POWER", g.math("SUBTRACT", 1.0, g.math("MINIMUM", g.math("MULTIPLY", radius, 2.0), 1.0)), 3.0)
    g.finish(g.ramp(glow, [(0.0, (0.0, 0.0, 0.0)), (0.5, (0.9, 0.55, 0.2)), (1.0, (1.0, 0.95, 0.8))]), glow, 0.0)


def env_gloss(g):
    # Sphere map for the paint: the view-space normal picks a texel, so the warm band sits on side panels.
    dx, dy = g.math("SUBTRACT", g.u, 0.5), g.math("SUBTRACT", g.v, 0.5)
    radius = g.math("SQRT", g.math("ADD", g.math("MULTIPLY", dx, dx), g.math("MULTIPLY", dy, dy)))
    horizon = g.math("POWER", g.math("SUBTRACT", 1.0, g.math("MINIMUM", g.math("MULTIPLY", g.math(
        "ABSOLUTE", g.math("SUBTRACT", dy, 0.06)), 9.0), 1.0)), 2.0)
    rim = g.math("POWER", g.math("MINIMUM", g.math("MULTIPLY", radius, 2.0), 1.0), 4.0)
    sky = g.math("MULTIPLY", g.math("GREATER_THAN", dy, 0.06), 0.25)
    warm = g.mix(horizon, (0.10, 0.12, 0.25, 1.0), (0.95, 0.55, 0.25, 1.0))
    colour = g.mix(g.math("ADD", g.math("MULTIPLY", rim, 0.7), g.math("ADD", g.math("MULTIPLY", horizon, 0.6), sky)),
                   (0.0, 0.0, 0.0, 1.0), warm)
    g.finish(colour, horizon, 0.0)


def sky(pixels):
    """The sunset dome: u is the azimuth, v the elevation from 10 degrees below the horizon to the zenith.

    The game darkens it towards night through the dome's vertex colours."""
    height, width = 512, 1024
    v = (np.arange(height) + 0.5)[:, None] / height
    u = (np.arange(width) + 0.5)[None, :] / width
    elevation = v * 100.0 - 10.0  # degrees
    azimuth = u * 2 * math.pi
    sun_gap = np.abs((azimuth - SUN_AZIMUTH + math.pi) % (2 * math.pi) - math.pi)
    sun_distance = np.sqrt(sun_gap ** 2 + np.radians(elevation - 5.0) ** 2)
    up = np.clip(elevation / 90.0, 0.0, 1.0)

    def lerp(a, b, t):
        return np.asarray(a)[None, None, :] * (1 - t[..., None]) + np.asarray(b)[None, None, :] * t[..., None]

    horizon, middle, zenith = (1.0, 0.52, 0.22), (0.80, 0.45, 0.48), (0.14, 0.22, 0.50)
    colour = np.where((up < 0.2)[..., None], lerp(horizon, middle, up / 0.2), lerp(middle, zenith, np.clip(
        (up - 0.2) / 0.8, 0, 1) ** 0.7))
    colour = np.broadcast_to(colour, (height, width, 3)).copy()
    colour *= np.where(elevation < 0, 0.75, 1.0)[..., None]
    glow = (np.exp(-sun_distance * 2.2) + np.exp(-sun_distance * 12.0)) * 1.6
    colour += glow[..., None] * np.array((1.0, 0.62, 0.30)) * np.clip(1.0 - up * 1.4, 0.15, 1.0)[..., None]
    disc = np.clip(1.0 - sun_distance / 0.035, 0.0, 1.0)
    colour = colour * (1 - disc[..., None]) + disc[..., None] * np.array((1.0, 0.93, 0.75))
    # Clouds: a baked noise tile along the horizon, lit orange towards the sun.
    noise = pixels[: height, :, 0]
    noise = np.tile(noise, (1, width // noise.shape[1] + 1))[:, :width]
    cover = np.clip((noise - 0.42) * 2.6, 0.0, 1.0) * np.clip(1.0 - np.abs(elevation - 12.0) / 22.0, 0.0, 1.0)
    lit = np.clip(np.exp(-sun_gap * 0.8), 0.0, 1.0)[..., None]
    cloud = lit * np.array((1.0, 0.55, 0.35)) + (1 - lit) * np.array((0.45, 0.32, 0.42))
    colour = colour * (1 - cover[..., None] * 0.85) + cloud * cover[..., None] * 0.85
    out = np.ones((height, width, 4), dtype=np.float32)
    # The colours above are display values; save_jpeg expects linear ones.
    out[..., :3] = np.clip(colour, 0, 1) ** 2.2
    return out


def ground_map(points, rights, grass_tile, sand_tile, gravel_tile):
    """Paint the island: grass, beaches along the coast, gravel run-off and barrier shade along the circuit."""
    size = GROUND_PIXELS
    x0, z0 = COAST_CENTRE[0] - GROUND_SIZE / 2, COAST_CENTRE[1] - GROUND_SIZE / 2
    # Distances are computed on a coarse grid, then upsampled: they vary slowly.
    coarse = 512
    cv = (np.arange(coarse) + 0.5) / coarse
    cx = x0 + cv[None, :] * GROUND_SIZE
    cz = z0 + cv[:, None] * GROUND_SIZE
    track = np.full((coarse, coarse), 1e9)
    total = len(points)
    for k in range(total):
        ax, az = points[k]
        bx, bz = points[(k + 1) % total]
        dx, dz = bx - ax, bz - az
        t = np.clip(((cx - ax) * dx + (cz - az) * dz) / (dx * dx + dz * dz), 0.0, 1.0)
        track = np.minimum(track, np.hypot(cx - (ax + t * dx), cz - (az + t * dz)))
    angle = np.arctan2(cz - COAST_CENTRE[1], cx - COAST_CENTRE[0])
    radius = np.vectorize(coast_radius)(angle)
    coast = radius - np.hypot(cx - COAST_CENTRE[0], cz - COAST_CENTRE[1])

    def upsample(field):
        scale = size // coarse
        field = np.repeat(np.repeat(field, scale, axis=0), scale, axis=1)
        # A small box blur hides the coarse cells.
        for axis in (0, 1):
            field = (np.roll(field, -2, axis) + np.roll(field, -1, axis) + field + np.roll(field, 1, axis) +
                     np.roll(field, 2, axis)) / 5.0
        return field

    track, coast = upsample(track), upsample(coast)

    def tiled(tile, repeats):
        t = tile[..., :3]
        step = max(1, t.shape[0] * repeats // size)
        t = t[::step, ::step]
        reps = size // t.shape[0] + 1
        return np.tile(t, (reps, reps, 1))[:size, :size]

    grass_c, sand_c, gravel_c = tiled(grass_tile, 8), tiled(sand_tile, 10), tiled(gravel_tile, 16)
    large = tiled(grass_tile, 1)[..., 1:2] / max(grass_tile[..., 1].mean(), 1e-3)
    colour = grass_c * (0.8 + 0.3 * np.clip(large, 0.5, 1.5) - 0.15)
    smooth = lambda e0, e1, x: np.clip((x - e0) / (e1 - e0), 0.0, 1.0)[..., None]  # noqa: E731
    beach = 1.0 - smooth(14.0, 30.0, coast)
    colour = colour * (1 - beach) + sand_c * beach
    wet = 1.0 - smooth(-2.0, 5.0, coast)
    colour = colour * (1 - wet * 0.45)
    under = 1.0 - smooth(-30.0, -1.0, coast)
    colour = colour * (1 - under) + np.array((0.10, 0.28, 0.30)) * under
    runoff = (1.0 - smooth(12.5, 14.5, track)) * smooth(7.5, 9.0, track)
    colour = colour * (1 - runoff) + gravel_c * runoff
    shade = 0.62 + 0.38 * smooth(13.8, 19.0, track)
    colour = colour * shade
    out = np.ones((size, size, 4), dtype=np.float32)
    out[..., :3] = np.clip(colour, 0, 1)
    return out


def build_textures(points, rights) -> None:
    bt.setup_bake_scene()
    for builder, name in ((asphalt, "asphalt"), (curb, "curb"), (barrier, "barrier"), (detail, "detail"),
                          (sea, "sea"), (rock, "rock"), (wheel, "wheel"), (bark, "bark"), (frond, "frond"),
                          (concrete, "concrete"), (seats, "seats"), (tower, "tower"), (flare, "flare"),
                          (env_gloss, "gloss"),
                          (normal_map(barrier_surface, 3.0, "barrier_bump"), "barrier_bump"),
                          (normal_map(rock_surface, 6.0, "rock_bump"), "rock_bump")):
        pixels = bt.bake(builder)
        if name not in FULL_SIZE:
            pixels = pixels[::2, ::2].copy()
        bt.save_jpeg(pixels, OUT / f"textures/{name}.jpg")
        if name == "flare":
            # The tail-light glow is the same flare in red.
            pixels[..., :3] *= np.array((1.0, 0.12, 0.06), dtype=np.float32)
            bt.save_jpeg(pixels, OUT / "textures/flare_red.jpg")
    for index, (base, accent) in enumerate((((0.95, 0.35, 0.05), (0.08, 0.08, 0.1)),
                                            ((0.05, 0.55, 0.60), (0.95, 0.95, 0.9)),
                                            ((0.42, 0.10, 0.62), (1.0, 0.75, 0.1)),
                                            ((0.90, 0.90, 0.88), (0.10, 0.25, 0.75)))):
        pixels = bt.bake(livery(base, accent, 321 + index))
        bt.save_jpeg(pixels[::2, ::2].copy(), OUT / f"textures/car_{index + 1}.jpg")
    clouds = bt.bake(lambda g: g.finish(g.ramp(g.noise(2.5, 8, 0.6, 331.0), [(0.0, (0, 0, 0)), (1.0, (1, 1, 1))]),
                                         g.u, 0.0))
    bt.save_jpeg(sky(clouds), OUT / "textures/sky.jpg")
    grass_tile, sand_tile, gravel_tile = bt.bake(grass), bt.bake(sand), bt.bake(gravel)
    bt.save_jpeg(ground_map(points, rights, grass_tile, sand_tile, gravel_tile), OUT / "textures/ground.jpg")


# ---------------------------------------------------------------------------
# Meshes built face by face

class Builder:
    """Collects quads per material into one Blender mesh object."""

    def __init__(self, name, materials, parent=None, order=0):
        self.bm = bmesh.new()
        self.uv = self.bm.loops.layers.uv.new("UVMap")
        self.name, self.materials, self.parent, self.order = name, materials, parent, order

    def face(self, corners, uvs, mat=0):
        face = self.bm.faces.new([self.bm.verts.new(c) for c in corners])
        face.material_index = mat
        for loop, coordinate in zip(face.loops, uvs):
            loop[self.uv].uv = coordinate
        return face

    def box(self, centre, size, mat=0, uv_scale=1.0, caps=True):
        cx, cy, cz = centre
        hx, hy, hz = (s / 2 for s in size)
        x0, x1, y0, y1, z0, z1 = cx - hx, cx + hx, cy - hy, cy + hy, cz - hz, cz + hz
        s = uv_scale
        if caps:
            self.face([(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)], [(0, 0), (size[0] * s, 0), (size[0] * s, size[1] * s), (0, size[1] * s)], mat)
            self.face([(x0, y1, z0), (x1, y1, z0), (x1, y0, z0), (x0, y0, z0)], [(0, 0), (size[0] * s, 0), (size[0] * s, size[1] * s), (0, size[1] * s)], mat)
        self.face([(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)], [(0, 0), (size[0] * s, 0), (size[0] * s, size[2] * s), (0, size[2] * s)], mat)
        self.face([(x1, y1, z0), (x0, y1, z0), (x0, y1, z1), (x1, y1, z1)], [(0, 0), (size[0] * s, 0), (size[0] * s, size[2] * s), (0, size[2] * s)], mat)
        self.face([(x0, y1, z0), (x0, y0, z0), (x0, y0, z1), (x0, y1, z1)], [(0, 0), (size[1] * s, 0), (size[1] * s, size[2] * s), (0, size[2] * s)], mat)
        self.face([(x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (x1, y0, z1)], [(0, 0), (size[1] * s, 0), (size[1] * s, size[2] * s), (0, size[2] * s)], mat)

    def finish(self):
        mesh = bpy.data.meshes.new(self.name)
        self.bm.to_mesh(mesh)
        self.bm.free()
        obj = bpy.data.objects.new(self.name, mesh)
        bpy.context.scene.collection.objects.link(obj)
        for mat in self.materials:
            obj.data.materials.append(mat)
        obj["limb_order"] = self.order
        obj.parent = self.parent
        return obj


def build_track(points, rights, lengths, curvature):
    road, curb_mat, wall, metal = material("asphalt"), material("curb"), material("barrier"), \
        material("lamp_metal", (0.22, 0.23, 0.25, 1.0))
    root = bpy.data.objects.new("circuit", None)
    bpy.context.scene.collection.objects.link(root)
    total = len(points)
    per_chunk = total // CHUNKS
    # Limbs 1-16 are the road chunks, 17-32 the walls: the game splits them into objects so off-screen
    # chunks are culled, and only the walls get the bump map.
    for chunk in range(CHUNKS):
        b = Builder(f"road_{chunk:02d}", [road, curb_mat, metal], root, chunk + 1)
        w = Builder(f"wall_{chunk:02d}", [wall], root, CHUNKS + chunk + 1)
        for k in range(chunk * per_chunk, (chunk + 1) * per_chunk):
            n = (k + 1) % total
            p, q, rp, rq = points[k], points[n], rights[k], rights[n]
            v0, v1 = lengths[k] / 16.0, lengths[k + 1] / 16.0
            b.face([at(p, rp, -ROAD_HALF, 0.04), at(p, rp, ROAD_HALF, 0.04), at(q, rq, ROAD_HALF, 0.04),
                    at(q, rq, -ROAD_HALF, 0.04)], [(0, v0), (1, v0), (1, v1), (0, v1)], 0)
            if max(curvature[k], curvature[n]) > 0.006:
                c0, c1 = lengths[k] / 4.0, lengths[k + 1] / 4.0
                for sign in (-1, 1):
                    inner, outer = sign * ROAD_HALF, sign * CURB_OUT
                    corners = [at(p, rp, inner, 0.07), at(p, rp, outer, 0.03), at(q, rq, outer, 0.03),
                               at(q, rq, inner, 0.07)]
                    if sign < 0:
                        corners = [corners[1], corners[0], corners[3], corners[2]]
                    b.face(corners, [(0, c0), (1, c0), (1, c1), (0, c1)], 1)
            if k % 2 == 0:
                m = (k + 2) % total
                r2, q2 = rights[m], points[m]
                u0, u1 = lengths[k] / 6.0, lengths[k + 2] / 6.0
                for sign in (-1, 1):
                    face = sign * BARRIER
                    # Both walls face the road: the right-hand one needs the reversed winding.
                    front = [at(p, rp, face, 0.0), at(q2, r2, face, 0.0), at(q2, r2, face, 1.1), at(p, rp, face, 1.1)]
                    uv_front = [(u0, 0), (u1, 0), (u1, 1), (u0, 1)]
                    if sign > 0:
                        front, uv_front = front[::-1], uv_front[::-1]
                    w.face(front, uv_front, 0)
            if k % LAMP_STEP == LAMP_STEP // 2:
                side = 1 if (k // LAMP_STEP) % 2 == 0 else -1
                post = at(p, rp, side * LAMP_SIDE, 0.0)
                tip = at(p, rp, side * LAMP_REACH, 0.0)
                b.box((post[0], post[1], LAMP_HEIGHT / 2), (0.3, 0.3, LAMP_HEIGHT), 2, 0.5, caps=False)
                mid = ((post[0] + tip[0]) / 2, (post[1] + tip[1]) / 2)
                length = math.dist(post[:2], tip[:2]) + 0.6
                heading = math.atan2(tip[1] - post[1], tip[0] - post[0])
                # The arm and the lamp head are boxes turned to point at the road.
                for centre, size, caps in (((mid[0], mid[1], LAMP_HEIGHT), (length, 0.18, 0.18), False),
                                           ((tip[0], tip[1], LAMP_HEIGHT - 0.1), (1.1, 0.5, 0.25), True)):
                    start = len(b.bm.verts)
                    b.box((0, 0, centre[2]), size, 2, 0.5, caps)
                    b.bm.verts.ensure_lookup_table()
                    for vertex in b.bm.verts[start:]:
                        x, y = vertex.co.x, vertex.co.y
                        vertex.co.x = centre[0] + x * math.cos(heading) - y * math.sin(heading)
                        vertex.co.y = centre[1] + x * math.sin(heading) + y * math.cos(heading)
        b.finish()
        w.finish()
    return root


def build_bulbs(points, rights):
    glow = material("bulb", (1.0, 0.85, 0.55, 1.0))
    b = Builder("bulbs", [glow])
    for k in range(LAMP_STEP // 2, len(points), LAMP_STEP):
        side = 1 if (k // LAMP_STEP) % 2 == 0 else -1
        x, z, _ = at(points[k], rights[k], side * LAMP_REACH, 0.0)
        y = LAMP_HEIGHT - 0.24
        # A downward-facing lens: seen from below as the lamp is approached.
        b.face([(x - 0.4, z + 0.2, y), (x + 0.4, z + 0.2, y), (x + 0.4, z - 0.2, y), (x - 0.4, z - 0.2, y)],
               [(0, 0), (1, 0), (1, 1), (0, 1)])
    return b.finish()


def build_ground():
    land = material("ground")
    b = Builder("ground", [land])
    x0, z0 = COAST_CENTRE[0] - GROUND_SIZE / 2, COAST_CENTRE[1] - GROUND_SIZE / 2
    segments = 48
    rings = [(0.0, 0.0), (0.6, 0.0), (1.0, 0.0), (1.0, -3.0)]  # fraction of the coast radius, height

    def vertex(angle, ring):
        fraction, height = rings[ring]
        radius = coast_radius(angle) * fraction + (35.0 if ring == 3 else 0.0)
        x = COAST_CENTRE[0] + math.cos(angle) * radius
        z = COAST_CENTRE[1] + math.sin(angle) * radius
        return (x, z, height), ((x - x0) / GROUND_SIZE, (z - z0) / GROUND_SIZE)

    for s in range(segments):
        a0, a1 = 2 * math.pi * s / segments, 2 * math.pi * (s + 1) / segments
        for ring in range(len(rings) - 1):
            p00, t00 = vertex(a0, ring)
            p01, t01 = vertex(a1, ring)
            p10, t10 = vertex(a0, ring + 1)
            p11, t11 = vertex(a1, ring + 1)
            if ring == 0:
                b.face([p00, p10, p11], [t00, t10, t11])
            else:
                b.face([p00, p10, p11, p01], [t00, t10, t11, t01])
    return b.finish()


def build_dome(name):
    # Seen from inside: faces wind towards the centre. u follows the azimuth, v the elevation.
    sky_mat = material(name)
    b = Builder(name, [sky_mat])
    segments, rings = 12, 5
    elevations = [-10.0, 2.0, 12.0, 30.0, 55.0, 90.0]

    def point(s, r):
        angle = 2 * math.pi * s / segments
        e = math.radians(elevations[r])
        return (math.cos(angle) * math.cos(e), math.sin(angle) * math.cos(e), math.sin(e)), \
            (s / segments, (elevations[r] + 10.0) / 100.0)

    for s in range(segments):
        for r in range(rings):
            (p00, t00), (p01, t01) = point(s, r), point(s + 1, r)
            (p10, t10), (p11, t11) = point(s, r + 1), point(s + 1, r + 1)
            if r == rings - 1:
                b.face([p00, p10, p01], [t00, ((t00[0] + t01[0]) / 2, t10[1]), t01])
            else:
                b.face([p00, p10, p11, p01], [t00, t10, t11, t01])
    obj = b.finish()
    for polygon in obj.data.polygons:
        polygon.use_smooth = True
    return obj


# ---------------------------------------------------------------------------
# Cars and props (Blender: +Y is forward, +Z is up)

def shape(obj, rule):
    for vertex in obj.data.vertices:
        rule(vertex.co)


def reproject(obj, size):
    # One texture tile over the whole hull instead of one per unit.
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.cube_project(cube_size=size)
    bpy.ops.object.mode_set(mode="OBJECT")


def build_car():
    paint, glass = material("paint"), material("glass", (0.05, 0.07, 0.10, 1.0))
    trim = material("trim", (0.04, 0.04, 0.05, 1.0))
    body = part("cube", "body", (1.9, 4.3, 0.5), (0, 0, 0.62), None, 0, paint, bevel=0.06)
    body.modifiers["bevel"].segments = 1

    def wedge(co):
        if co.y > 0.9 and co.z > 0:
            co.z -= 0.16 * (co.y - 0.9) / 1.25
        if co.y > 1.6:
            co.x *= 0.9
        if co.z < 0:
            co.x *= 0.96

    shape(body, wedge)
    reproject(body, 2.4)
    cabin = part("cube", "cabin", (1.5, 2.0, 0.48), (0, -0.35, 0.47), body, 1, glass)

    def canopy(co):
        if co.z > 0:
            co.x *= 0.78
            co.y = co.y - 0.42 if co.y > 0 else co.y + 0.3

    shape(cabin, canopy)
    wing = part("cube", "wing", (1.8, 0.35, 0.06), (0, -2.0, 0.45), body, 0, trim)
    del wing
    # Join the rear wing into the hull so the hull stays limb 0.
    bpy.ops.object.select_all(action="DESELECT")
    bpy.data.objects["wing"].select_set(True)
    body.select_set(True)
    bpy.context.view_layer.objects.active = body
    bpy.ops.object.join()
    tyre = material("tyre")
    order = 2
    for name, x, y in (("wheel_fl", -0.86, 1.32), ("wheel_fr", 0.86, 1.32), ("wheel_rl", -0.86, -1.32),
                       ("wheel_rr", 0.86, -1.32)):
        bpy.ops.mesh.primitive_cylinder_add(vertices=8, radius=0.35, depth=0.3)
        wheel_obj = bpy.context.active_object
        wheel_obj.name = name
        # Axle along X; the limb itself stays unrotated so ROTATE LIMB spins and steers it directly.
        wheel_obj.data.transform(__import__("mathutils").Matrix.Rotation(math.pi / 2, 4, "Y"))
        wheel_obj.data.materials.append(tyre)
        wheel_obj.parent = body
        wheel_obj.location = (x, y, 0.35 - 0.62)
        wheel_obj["limb_order"] = order
        order += 1
    return body


def build_car_lamps():
    head, tail = material("headlamp", (1.0, 0.95, 0.80, 1.0)), material("taillamp", (1.0, 0.12, 0.05, 1.0))
    b = Builder("lamps", [head, tail])
    # Lenses just proud of the hull; drawn unlit, so they glow whatever the light.
    for sign in (-1, 1):
        x = sign * 0.62
        b.face([(x + 0.24, 2.17, 0.58), (x - 0.24, 2.17, 0.58), (x - 0.24, 2.17, 0.68), (x + 0.24, 2.17, 0.68)],
               [(0, 0), (1, 0), (1, 1), (0, 1)], 0)
        b.face([(x - 0.3, -2.17, 0.70), (x + 0.3, -2.17, 0.70), (x + 0.3, -2.17, 0.80), (x - 0.3, -2.17, 0.80)],
               [(0, 0), (1, 0), (1, 1), (0, 1)], 1)
    return b.finish()


def build_shadow():
    # A closed low-poly silhouette for SET SHADOW SHADING ON: the hull and the cabin.
    b = Builder("shadow", [material("shadow")])
    b.box((0, 0, 0.62), (1.8, 4.1, 0.45))
    b.box((0, -0.35, 1.05), (1.3, 1.6, 0.42))
    return b.finish()


def build_palm():
    trunk_mat, leaf = material("bark"), material("frond")
    b = Builder("palm", [trunk_mat, leaf])
    sides, segments, height = 5, 4, 9.0
    ring = []
    for s in range(segments + 1):
        t = s / segments
        lean = 1.6 * t * t
        radius = 0.32 - 0.12 * t
        ring.append([(lean + math.cos(2 * math.pi * i / sides) * radius, math.sin(2 * math.pi * i / sides) * radius,
                      t * height) for i in range(sides + 1)])
    for s in range(segments):
        for i in range(sides):
            b.face([ring[s][i], ring[s][i + 1], ring[s + 1][i + 1], ring[s + 1][i]],
                   [(i / sides, s / segments * 3), ((i + 1) / sides, s / segments * 3),
                    ((i + 1) / sides, (s + 1) / segments * 3), (i / sides, (s + 1) / segments * 3)], 0)
    top = (1.6, 0.0, height)
    for f in range(6):
        a = 2 * math.pi * f / 6 + 0.3
        dx, dy = math.cos(a), math.sin(a)
        px, py = -dy * 0.45, dx * 0.45
        mid = (top[0] + dx * 2.2, top[1] + dy * 2.2, top[2] + 0.5)
        end = (top[0] + dx * 4.2, top[1] + dy * 4.2, top[2] - 1.4)
        # Two segments per frond; the game draws the palm without culling so they read from below too.
        for a0, a1, v0, v1, w0, w1 in ((top, mid, 0.0, 0.5, 0.15, 1.0), (mid, end, 0.5, 1.0, 1.0, 0.2)):
            quad = [(a0[0] - px * w0, a0[1] - py * w0, a0[2]), (a0[0] + px * w0, a0[1] + py * w0, a0[2]),
                    (a1[0] + px * w1, a1[1] + py * w1, a1[2]), (a1[0] - px * w1, a1[1] - py * w1, a1[2])]
            uvs = [(0, v0), (1, v0), (1, v1), (0, v1)]
            b.face(quad, uvs, 1)
    return b.finish()


def build_rock():
    stone = material("rock")
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=1, radius=1.0)
    obj = bpy.context.active_object
    obj.name = "rock"
    for vertex in obj.data.vertices:
        co = vertex.co
        noise = 0.75 + 0.35 * math.sin(co.x * 3.1 + co.y * 1.7) * math.cos(co.z * 2.3 + co.x)
        co.x, co.y, co.z = co.x * noise * 1.3, co.y * noise, max(co.z * noise * 0.8, -0.3)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.cube_project(cube_size=1.0)
    bpy.ops.object.mode_set(mode="OBJECT")
    obj.data.materials.append(stone)
    obj["limb_order"] = 0
    return obj


def build_grandstand():
    stone, seat = material("concrete"), material("seats")
    roof = material("roof", (0.85, 0.86, 0.88, 1.0))
    b = Builder("grandstand", [stone, seat, roof])
    width, steps = 60.0, 8
    for s in range(steps):
        depth = 1.4
        y0 = -s * depth
        z1 = 0.8 + s * 0.75
        # The track lies towards +Y: each step is a riser facing it and a tread, rising away from it.
        b.face([(width / 2, y0, z1 - 0.75), (-width / 2, y0, z1 - 0.75), (-width / 2, y0, z1), (width / 2, y0, z1)],
               [(0, 0), (width / 10, 0), (width / 10, 0.12), (0, 0.12)], 0)
        b.face([(-width / 2, y0 - depth, z1), (width / 2, y0 - depth, z1), (width / 2, y0, z1), (-width / 2, y0, z1)],
               [(0, s / steps), (1, s / steps), (1, (s + 1) / steps), (0, (s + 1) / steps)], 1)
    back = -steps * 1.4
    top = 0.8 + steps * 0.75
    b.box((0, back - 0.3, top / 2), (width, 0.6, top), 0, 0.1)
    for x in (-width / 2, -width / 6, width / 6, width / 2):
        b.box((x, back - 0.3, top + 2.2), (0.4, 0.4, 4.4), 0, 0.3)
    b.box((0, back / 2 - 0.5, top + 4.5), (width + 2, -back + 3, 0.3), 2, 0.05)
    return b.finish()


def build_gantry():
    steel = material("gantry", (0.15, 0.16, 0.18, 1.0))
    banner = material("banner", (0.85, 0.30, 0.08, 1.0))
    b = Builder("gantry", [steel, banner])
    for x in (-10.5, 10.5):
        b.box((x, 0, 3.5), (0.8, 0.8, 7.0), 0)
    b.box((0, 0, 7.4), (22.0, 1.0, 1.4), 1)
    return b.finish()


def build_lighthouse():
    paint, stone = material("tower"), material("rock")
    lamp = material("beacon", (1.0, 0.9, 0.6, 1.0))
    base = part("cylinder", "base", (9.0, 9.0, 3.0), (0, 0, 1.5), None, 0, stone)
    body = part("cylinder", "tower", (6.0, 6.0, 22.0), (0, 0, 1.5), base, 1, paint, (0, 0, 11.0))
    for vertex in body.data.vertices:
        if vertex.co.z > 11:
            vertex.co.x, vertex.co.y = vertex.co.x * 0.6, vertex.co.y * 0.6
    gallery = part("cylinder", "gallery", (5.0, 5.0, 0.6), (0, 0, 22.0), body, 2, stone)
    part("cylinder", "lantern", (2.6, 2.6, 2.6), (0, 0, 1.6), gallery, 3, lamp)
    return base


def build_hill():
    stone = material("rock")
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=2, radius=1.0)
    obj = bpy.context.active_object
    obj.name = "hill"
    for vertex in obj.data.vertices:
        co = vertex.co
        noise = 0.8 + 0.25 * math.sin(co.x * 4.0 + co.y * 2.5) + 0.12 * math.cos(co.y * 7.0 + co.z * 3.0)
        co.x, co.y, co.z = co.x * noise, co.y * noise * 0.8, max(co.z * noise, -0.2) * 0.55
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.cube_project(cube_size=0.25)
    bpy.ops.object.mode_set(mode="OBJECT")
    obj.data.materials.append(stone)
    obj["limb_order"] = 0
    return obj


def main() -> None:
    points, rights, lengths, curvature = centreline()
    margin = min(coast_radius(math.atan2(z - COAST_CENTRE[1], x - COAST_CENTRE[0])) -
                 math.hypot(x - COAST_CENTRE[0], z - COAST_CENTRE[1]) for x, z in points)
    print(f"circuit length {lengths[-1]:.0f}, {len(points)} samples, closest coast {margin:.1f}")
    if "--models-only" not in sys.argv:
        build_textures(points, rights)
    for builder, name, textures in (
            (lambda: build_track(points, rights, lengths, curvature), "circuit",
             {"asphalt": "asphalt.jpg", "curb": "curb.jpg", "barrier": "barrier.jpg"}),
            (lambda: build_bulbs(points, rights), "bulbs", {}),
            (build_ground, "ground", {"ground": "ground.jpg"}),
            (lambda: build_dome("sky"), "sky", {"sky": "sky.jpg"}),
            (build_car, "car", {"paint": "car_1.jpg", "tyre": "wheel.jpg"}),
            (build_car_lamps, "car_lamps", {}),
            (build_shadow, "car_shadow", {}),
            (build_palm, "palm", {"bark": "bark.jpg", "frond": "frond.jpg"}),
            (build_rock, "rock", {"rock": "rock.jpg"}),
            (build_grandstand, "grandstand", {"concrete": "concrete.jpg", "seats": "seats.jpg"}),
            (build_gantry, "gantry", {}),
            (build_lighthouse, "lighthouse", {"tower": "tower.jpg", "rock": "rock.jpg"}),
            (build_hill, "hill", {"rock": "rock.jpg"})):
        bpy.ops.wm.read_factory_settings(use_empty=True)
        xexport.export(OUT / f"models/{name}.x", [builder()], textures)


if __name__ == "__main__":
    main()
    sys.exit(0)
