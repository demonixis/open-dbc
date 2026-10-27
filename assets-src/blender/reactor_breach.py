"""Build the Reactor Breach demo media: the lightmapped facility, its light
panels, a security drone with five clips on one timeline, the pulse rifle, a
coolant valve and their textures.

Run from the repository root:
    /Applications/Blender.app/Contents/MacOS/Blender --background --factory-startup \
        --python assets-src/blender/reactor_breach.py

The level layout is read from the DATA lines of reactor_breach.dba, so the game
and its geometry cannot drift apart. Every level face gets its own island in a
1024 x 1024 atlas at a uniform texel density; Cycles bakes the light panels and
the reactor core into that atlas (lightmap.jpg). At load time the game copies
those UVs to stage 1 for the lightmap and scales stage 0 by LEVEL_TILING so the
tiling materials repeat every TILE world units.
"""

import math
import re
import sys
from pathlib import Path

import bmesh
import bpy
import numpy as np
from mathutils import Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bake_textures as bt  # noqa: E402
import xexport  # noqa: E402
from flooded_vault import keyframe, material  # noqa: E402
from frost_outpost import part, polar  # noqa: E402

OUT = bt.ROOT / "examples/dbpro/demos/reactor_breach"
SOURCE = OUT / "reactor_breach.dba"
ATLAS = 1024
MARGIN = 6  # pixels between islands, filled by the bake margin
TILE = 32.0  # world units covered by one repeat of a level material


# ---------------------------------------------------------------------------
# Layout

def read_layout():
    text = SOURCE.read_text(encoding="ascii")
    block = text.split("rem layout:begin", 1)[1].split("rem layout:end", 1)[0]
    layout = {}
    for line in block.splitlines():
        match = re.match(r"\s*data\s+\"(\w+)\"(.*)", line)
        if not match or match.group(1) == "end":
            continue
        values = [float(value) for value in match.group(2).split(",") if value.strip()]
        layout.setdefault(match.group(1), []).append(values)
    return layout


# ---------------------------------------------------------------------------
# Textures

def floor_plate(g):
    edge, cell = g.cells(2, 2, 0.0)
    surface = g.noise(8.0, 6, 0.6, 201.0)
    height, face = bt.bevel(g, edge, 0.006, 0.012, surface, 0.15)
    # Diamond tread: two sets of short diagonal ridges.
    a = g.math("LESS_THAN", g.math("ABSOLUTE", g.math("SUBTRACT", g.math("FRACT", g.math(
        "ADD", g.math("MULTIPLY", g.u, 24.0), g.math("MULTIPLY", g.v, 24.0))), 0.5)), 0.12)
    b = g.math("LESS_THAN", g.math("ABSOLUTE", g.math("SUBTRACT", g.math("FRACT", g.math(
        "SUBTRACT", g.math("MULTIPLY", g.u, 24.0), g.math("MULTIPLY", g.v, 24.0))), 0.5)), 0.12)
    gate = g.math("GREATER_THAN", g.math("FRACT", g.math("MULTIPLY", g.v, 48.0)), 0.5)
    tread = g.math("MAXIMUM", g.math("MULTIPLY", a, gate), g.math("MULTIPLY", b, g.math("SUBTRACT", 1.0, gate)))
    grime = g.noise(3.0, 6, 0.6, 203.0)
    steel = g.ramp(g.math("ADD", g.math("MULTIPLY", grime, 0.6), g.math("MULTIPLY", cell, 0.3)),
                   [(0.2, (0.16, 0.17, 0.18)), (0.8, (0.40, 0.41, 0.42))])
    steel = g.mix(g.math("MULTIPLY", tread, 0.5), steel, (0.62, 0.63, 0.64, 1.0))
    colour = g.mix(face, (0.04, 0.04, 0.045, 1.0), steel)
    g.finish(colour, g.math("ADD", height, g.math("MULTIPLY", tread, 0.15)), 0.8)


def wall_panel(g):
    edge, cell = g.cells(2, 2, 0.0)
    surface = g.noise(10.0, 6, 0.55, 207.0)
    height, face = bt.bevel(g, edge, 0.008, 0.02, surface, 0.15)
    lower = g.math("LESS_THAN", g.math("FRACT", g.math("MULTIPLY", g.v, 2.0)), 0.18)
    # Vent slots across the middle of every panel.
    slot_y = g.math("LESS_THAN", g.math("ABSOLUTE", g.math("SUBTRACT", g.math("FRACT", g.math(
        "MULTIPLY", g.v, 2.0)), 0.62)), 0.09)
    slot_x = g.math("LESS_THAN", g.math("ABSOLUTE", g.math("SUBTRACT", g.math("FRACT", g.math(
        "MULTIPLY", g.v, 28.0)), 0.5)), 0.22)
    inset = g.math("LESS_THAN", g.math("ABSOLUTE", g.math("SUBTRACT", g.math("FRACT", g.math(
        "MULTIPLY", g.u, 2.0)), 0.5)), 0.3)
    vent = g.math("MULTIPLY", g.math("MULTIPLY", slot_y, slot_x), inset)
    paint = g.ramp(g.math("ADD", g.math("MULTIPLY", cell, 0.3), g.math("MULTIPLY", surface, 0.7)),
                   [(0.2, (0.42, 0.45, 0.47)), (0.8, (0.62, 0.65, 0.66))])
    paint = g.mix(lower, paint, (0.08, 0.20, 0.22, 1.0))
    paint = g.mix(vent, paint, (0.03, 0.03, 0.035, 1.0))
    colour = g.mix(face, (0.05, 0.05, 0.06, 1.0), paint)
    g.finish(colour, g.math("SUBTRACT", height, g.math("MULTIPLY", vent, 0.5)), 1.0)


def ceiling_grate(g):
    bars_x = g.math("LESS_THAN", g.math("ABSOLUTE", g.math("SUBTRACT", g.math("FRACT", g.math("MULTIPLY", g.u, 16.0)), 0.5)), 0.18)
    ribs = g.math("LESS_THAN", g.math("ABSOLUTE", g.math("SUBTRACT", g.math("FRACT", g.math("MULTIPLY", g.v, 2.0)), 0.5)), 0.06)
    bars = g.math("MAXIMUM", bars_x, ribs)
    metal = g.ramp(g.noise(8.0, 6, 0.6, 211.0), [(0.3, (0.16, 0.17, 0.19)), (0.8, (0.32, 0.33, 0.35))])
    g.finish(g.mix(bars, (0.02, 0.02, 0.025, 1.0), metal), bars, 0.8)


def hazard(g):
    stripe = g.math("LESS_THAN", g.math("FRACT", g.math("ADD", g.math("MULTIPLY", g.u, 8.0), g.math("MULTIPLY", g.v, 8.0))), 0.5)
    wear = g.math("GREATER_THAN", g.noise(6.0, 6, 0.65, 213.0), 0.66)
    paint = g.mix(stripe, (0.05, 0.05, 0.05, 1.0), (0.95, 0.70, 0.08, 1.0))
    g.finish(g.mix(g.math("MULTIPLY", wear, 0.8), paint, (0.30, 0.30, 0.32, 1.0)), wear, 0.5)


def machine_metal(g):
    edge, cell = g.cells(4, 4, 0.0)
    surface = g.noise(12.0, 6, 0.6, 217.0)
    height, face = bt.bevel(g, edge, 0.004, 0.01, surface, 0.2)
    rivet = g.math("LESS_THAN", g.voronoi(16.0, "F1", "Distance", 219.0), 0.06)
    metal = g.ramp(g.math("ADD", g.math("MULTIPLY", cell, 0.3), g.math("MULTIPLY", surface, 0.7)),
                   [(0.2, (0.20, 0.24, 0.27)), (0.8, (0.42, 0.47, 0.50))])
    metal = g.mix(rivet, metal, (0.70, 0.72, 0.74, 1.0))
    g.finish(g.mix(face, (0.03, 0.03, 0.035, 1.0), metal), g.math("ADD", height, g.math("MULTIPLY", rivet, 0.3)), 1.0)


def crate(g):
    edge, _ = g.cells(1, 1, 0.0)
    rim = g.math("LESS_THAN", edge, 0.09)
    brace_a = g.math("LESS_THAN", g.math("ABSOLUTE", g.math("SUBTRACT", g.u, g.v)), 0.07)
    brace_b = g.math("LESS_THAN", g.math("ABSOLUTE", g.math("SUBTRACT", g.math("ADD", g.u, g.v), 1.0)), 0.07)
    frame = g.math("MAXIMUM", rim, g.math("MAXIMUM", brace_a, brace_b))
    dents = g.noise(6.0, 6, 0.6, 223.0)
    shell = g.ramp(dents, [(0.2, (0.30, 0.34, 0.20)), (0.8, (0.44, 0.48, 0.30))])
    steel = g.ramp(dents, [(0.2, (0.30, 0.31, 0.32)), (0.8, (0.55, 0.56, 0.56))])
    stencil = g.math("MULTIPLY", g.math("LESS_THAN", g.math("ABSOLUTE", g.math("SUBTRACT", g.v, 0.5)), 0.05),
                     g.math("LESS_THAN", g.math("ABSOLUTE", g.math("SUBTRACT", g.u, 0.5)), 0.22))
    colour = g.mix(frame, g.mix(stencil, shell, (0.85, 0.75, 0.2, 1.0)), steel)
    g.finish(colour, g.math("ADD", g.math("MULTIPLY", frame, 0.8), g.math("MULTIPLY", dents, 0.04)), 0.6)


def drone_armour(g):
    edge, cell = g.cells(3, 3, 0.5)
    surface = g.noise(10.0, 6, 0.6, 227.0)
    height, face = bt.bevel(g, edge, 0.005, 0.015, surface, 0.2)
    stripe = g.math("LESS_THAN", g.math("ABSOLUTE", g.math("SUBTRACT", g.math("FRACT", g.math("MULTIPLY", g.v, 2.0)), 0.5)), 0.07)
    paint = g.ramp(g.math("ADD", g.math("MULTIPLY", cell, 0.3), g.math("MULTIPLY", surface, 0.7)),
                   [(0.2, (0.55, 0.57, 0.60)), (0.8, (0.82, 0.84, 0.86))])
    paint = g.mix(stripe, paint, (0.95, 0.45, 0.05, 1.0))
    g.finish(g.mix(face, (0.02, 0.02, 0.02, 1.0), paint), height, 1.0)


def rifle_metal(g):
    edge, cell = g.cells(2, 4, 0.0)
    surface = g.noise(16.0, 6, 0.6, 229.0)
    height, face = bt.bevel(g, edge, 0.004, 0.02, surface, 0.15)
    metal = g.ramp(g.math("ADD", g.math("MULTIPLY", cell, 0.2), g.math("MULTIPLY", surface, 0.8)),
                   [(0.2, (0.10, 0.11, 0.12)), (0.8, (0.30, 0.32, 0.34))])
    g.finish(g.mix(face, (0.02, 0.02, 0.02, 1.0), metal), height, 1.0)


def paint_red(g):
    wear = g.noise(5.0, 7, 0.65, 233.0)
    chips = g.math("GREATER_THAN", wear, 0.68)
    paint = g.ramp(g.noise(20.0, 4, 0.5, 234.0), [(0.3, (0.55, 0.06, 0.04)), (0.8, (0.80, 0.12, 0.07))])
    g.finish(g.mix(chips, paint, (0.45, 0.45, 0.47, 1.0)), g.math("SUBTRACT", 1.0, chips), 0.6)


def glow(g):
    _, _, radius, _ = polar(g)
    core = g.math("POWER", g.math("SUBTRACT", 1.0, g.math("MINIMUM", g.math("MULTIPLY", radius, 2.0), 1.0)), 2.2)
    g.finish(g.ramp(core, [(0.0, (0.0, 0.0, 0.0)), (0.55, (0.5, 0.5, 0.5)), (1.0, (1.0, 1.0, 1.0))]), core, 0.0)


def flash(g):
    # A four-pointed muzzle star on black, drawn additively.
    dx, dy, radius, angle = polar(g)
    rays = g.math("POWER", g.math("ABSOLUTE", g.math("COSINE", g.math("MULTIPLY", angle, 2.0))), 8.0)
    fall = g.math("SUBTRACT", 1.0, g.math("MINIMUM", g.math("MULTIPLY", radius, 2.0), 1.0))
    star = g.math("MULTIPLY", g.math("ADD", g.math("MULTIPLY", rays, 0.7), g.math("POWER", fall, 3.0)), fall)
    g.finish(g.ramp(star, [(0.0, (0.0, 0.0, 0.0)), (0.4, (0.9, 0.5, 0.1)), (1.0, (1.0, 0.95, 0.8))]), star, 0.0)


def eye(g):
    _, _, radius, _ = polar(g)
    core = g.math("POWER", g.math("SUBTRACT", 1.0, g.math("MINIMUM", g.math("MULTIPLY", radius, 2.0), 1.0)), 1.5)
    # Pure black at the rim: the eye halo and the plasma bolts are drawn additively.
    g.finish(g.ramp(core, [(0.0, (0.0, 0.0, 0.0)), (0.5, (0.9, 0.12, 0.04)), (1.0, (1.0, 0.85, 0.6))]), core, 0.0)


def lamp(g):
    edge, _ = g.cells(1, 1, 0.0)
    frame = g.math("LESS_THAN", edge, 0.06)
    louvre = g.math("LESS_THAN", g.math("ABSOLUTE", g.math("SUBTRACT", g.math("FRACT", g.math("MULTIPLY", g.u, 8.0)), 0.5)), 0.06)
    light = g.ramp(g.math("ABSOLUTE", g.math("SUBTRACT", g.v, 0.5)), [(0.0, (1.0, 1.0, 1.0)), (0.5, (0.75, 0.78, 0.8))])
    g.finish(g.mix(g.math("MAXIMUM", frame, louvre), light, (0.25, 0.26, 0.28, 1.0)), frame, 0.0)


def screen(g):
    # Console read-outs: bar graphs on the left, scrolling text rows on the right.
    left = g.math("LESS_THAN", g.u, 0.45)
    bar_col = g.math("FRACT", g.math("MULTIPLY", g.u, 10.0))
    bar_id = g.math("FLOOR", g.math("MULTIPLY", g.u, 10.0))
    bar_h = g.math("ADD", 0.25, g.math("MULTIPLY", 0.6, g.math("FRACT", g.math("MULTIPLY", g.math("SINE", g.math(
        "MULTIPLY", bar_id, 12.9898)), 43758.5))))
    bars = g.math("MULTIPLY", g.math("LESS_THAN", g.v, bar_h), g.math("LESS_THAN", g.math("ABSOLUTE", g.math(
        "SUBTRACT", bar_col, 0.5)), 0.3))
    row = g.math("LESS_THAN", g.math("ABSOLUTE", g.math("SUBTRACT", g.math("FRACT", g.math("MULTIPLY", g.v, 14.0)), 0.5)), 0.18)
    words = g.math("GREATER_THAN", g.noise(9.0, 2, 0.3, 237.0), 0.45)
    text = g.math("MULTIPLY", g.math("MULTIPLY", row, words), g.math("SUBTRACT", 1.0, left))
    lit = g.math("MAXIMUM", g.math("MULTIPLY", bars, left), text)
    scan = g.math("ADD", 0.85, g.math("MULTIPLY", g.math("SINE", g.math("MULTIPLY", g.v, 400.0)), 0.15))
    base = g.mix(lit, (0.01, 0.06, 0.07, 1.0), (0.30, 1.0, 0.85, 1.0))
    g.finish(g.mix(1.0, base, g.combine_grey(scan), "MULTIPLY"), lit, 0.0)


def plasma(g):
    swirl = g.noise(2.0, 8, 0.6, 241.0, distortion=1.5)
    bright = g.math("POWER", swirl, 2.0)
    g.finish(g.ramp(bright, [(0.05, (0.0, 0.12, 0.25)), (0.3, (0.1, 0.65, 0.95)), (0.7, (0.85, 1.0, 1.0))]), swirl, 0.0)


def forcefield(g):
    cells = g.voronoi(4.0, "DISTANCE_TO_EDGE", offset=243.0)
    edges = g.math("POWER", g.math("SUBTRACT", 1.0, g.math("MINIMUM", g.math("MULTIPLY", cells, 8.0), 1.0)), 2.0)
    shimmer = g.math("MULTIPLY", g.noise(2.0, 3, 0.4, 244.0), 0.15)
    level = g.math("ADD", edges, shimmer)
    g.finish(g.ramp(level, [(0.0, (0.0, 0.0, 0.0)), (0.5, (0.05, 0.35, 0.6)), (1.0, (0.6, 0.95, 1.0))]), level, 0.0)


def environment(g):
    # A blurred room for SPHERE MAPPING: bright strip lights over a dark floor.
    height = g.math("SUBTRACT", g.v, 0.5)
    sky = g.ramp(g.v, [(0.0, (0.03, 0.04, 0.05)), (0.5, (0.18, 0.22, 0.26)), (1.0, (0.45, 0.55, 0.62))])
    strips = g.math("MULTIPLY", g.math("GREATER_THAN", height, 0.18), g.math("LESS_THAN", g.math("ABSOLUTE", g.math(
        "SUBTRACT", g.math("FRACT", g.math("MULTIPLY", g.u, 4.0)), 0.5)), 0.12))
    g.finish(g.mix(strips, sky, (1.0, 1.0, 1.0, 1.0)), strips, 0.0)


def medkit(g):
    edge, _ = g.cells(1, 1, 0.0)
    dx = g.math("ABSOLUTE", g.math("SUBTRACT", g.u, 0.5))
    dy = g.math("ABSOLUTE", g.math("SUBTRACT", g.v, 0.5))
    plus = g.math("MAXIMUM", g.math("MULTIPLY", g.math("LESS_THAN", dx, 0.08), g.math("LESS_THAN", dy, 0.28)),
                  g.math("MULTIPLY", g.math("LESS_THAN", dy, 0.08), g.math("LESS_THAN", dx, 0.28)))
    shell = g.ramp(g.noise(8.0, 4, 0.5, 247.0), [(0.3, (0.80, 0.82, 0.84)), (0.8, (0.95, 0.96, 0.97))])
    colour = g.mix(plus, shell, (0.10, 0.75, 0.25, 1.0))
    g.finish(g.mix(g.math("LESS_THAN", edge, 0.05), colour, (0.3, 0.3, 0.32, 1.0)), plus, 0.4)


def cell(g):
    band = g.math("LESS_THAN", g.math("ABSOLUTE", g.math("SUBTRACT", g.math("FRACT", g.math("MULTIPLY", g.v, 3.0)), 0.5)), 0.2)
    metal = g.ramp(g.noise(12.0, 4, 0.5, 251.0), [(0.3, (0.18, 0.19, 0.2)), (0.8, (0.35, 0.36, 0.38))])
    g.finish(g.mix(band, metal, (0.3, 0.95, 1.0, 1.0)), band, 0.4)


class NormalGraph(bt.Graph):
    """Same materials, but finish emits the bump normal as a tangent-space normal map."""

    def finish(self, colour, height, strength=0.8):
        bump = self.node("ShaderNodeBump")
        bump.inputs["Strength"].default_value = min(1.0, strength)
        bump.inputs["Distance"].default_value = 12.0 * strength
        self.link(height, bump.inputs["Height"])
        # The bake plane faces +Z with U along +X, so world space is tangent space.
        encode = self.node("ShaderNodeVectorMath", operation="MULTIPLY_ADD")
        self.link(bump.outputs["Normal"], encode.inputs[0])
        encode.inputs[1].default_value = (0.5, 0.5, 0.5)
        encode.inputs[2].default_value = (0.5, 0.5, 0.5)
        emission = self.node("ShaderNodeEmission")
        self.link(encode.outputs[0], emission.inputs["Color"])
        output = self.node("ShaderNodeOutputMaterial")
        self.link(emission.outputs[0], output.inputs["Surface"])


def bake_normal(builder) -> np.ndarray:
    original = bt.Graph
    bt.Graph = NormalGraph
    try:
        return bt.bake(builder)
    finally:
        bt.Graph = original


def save_raw_jpeg(pixels: np.ndarray, path: Path) -> None:
    # Normal maps store vectors, not colours: write them without the sRGB encode of save_jpeg.
    height, width, _ = pixels.shape
    image = bpy.data.images.new(path.stem, width, height)
    image.pixels.foreach_set(np.clip(pixels, 0.0, 1.0).ravel())
    scene = bpy.context.scene
    scene.render.image_settings.file_format = "JPEG"
    scene.render.image_settings.quality = bt.JPEG_QUALITY
    scene.render.image_settings.color_management = "OVERRIDE"
    scene.view_settings.view_transform = "Standard"
    image.save_render(str(path), scene=scene)
    print(f"wrote {path.relative_to(bt.ROOT)}")


def downsample(pixels: np.ndarray, factor: int) -> np.ndarray:
    size = pixels.shape[0] // factor
    return pixels.reshape(size, factor, size, factor, 4).mean(axis=(1, 3))


def save_blob(path: Path, size: int = 128) -> None:
    """A soft black disc whose alpha fades out: the drones' blob shadow."""
    ramp = np.linspace(-1.0, 1.0, size)
    radius = np.sqrt(ramp[None, :] ** 2 + ramp[:, None] ** 2)
    pixels = np.zeros((size, size, 4), dtype=np.float32)
    pixels[..., 3] = np.clip(1.0 - radius, 0.0, 1.0) ** 1.5 * 0.85
    image = bpy.data.images.new(path.stem, size, size, alpha=True)
    image.pixels.foreach_set(pixels.ravel())
    scene = bpy.context.scene
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    image.save_render(str(path), scene=scene)
    print(f"wrote {path.relative_to(bt.ROOT)}")


def build_textures() -> None:
    bt.setup_bake_scene()
    for builder, name in ((floor_plate, "floor"), (wall_panel, "wall"), (ceiling_grate, "ceiling"),
                          (hazard, "hazard"), (machine_metal, "machine"), (crate, "crate"),
                          (drone_armour, "drone")):
        bt.save_jpeg(bt.bake(builder), OUT / f"{name}.jpg")
    save_raw_jpeg(downsample(bake_normal(crate), 2), OUT / "crate_normal.jpg")
    # Small textures: effects, props and screens.
    for builder, name in ((rifle_metal, "rifle"), (paint_red, "valve"), (glow, "glow"), (eye, "eye"), (flash, "flash"),
                          (lamp, "lamp"), (screen, "screen"), (plasma, "plasma"), (forcefield, "field"),
                          (environment, "chrome"), (medkit, "medkit"), (cell, "cell")):
        bt.save_jpeg(downsample(bt.bake(builder), 2), OUT / f"{name}.jpg")
    save_blob(OUT / "blob.png")


# ---------------------------------------------------------------------------
# Level geometry. World (x, z) from the layout is Blender (x, y); height is Blender z.

class Builder:
    """Collect faces per material limb; each face remembers the island it belongs to."""

    def __init__(self):
        self.limbs = {}

    def quad(self, limb, corners, island=None):
        self.limbs.setdefault(limb, []).append((corners, island))

    def box(self, limb, x1, y1, z1, x2, y2, z2, top=True, bottom=False):
        p = lambda x, y, z: Vector((x, y, z))  # noqa: E731
        sides = [
            (p(x1, y1, z1), p(x2, y1, z1), p(x2, y1, z2), p(x1, y1, z2)),  # -y
            (p(x2, y2, z1), p(x1, y2, z1), p(x1, y2, z2), p(x2, y2, z2)),  # +y
            (p(x1, y2, z1), p(x1, y1, z1), p(x1, y1, z2), p(x1, y2, z2)),  # -x
            (p(x2, y1, z1), p(x2, y2, z1), p(x2, y2, z2), p(x2, y1, z2)),  # +x
        ]
        if top:
            sides.append((p(x1, y1, z2), p(x2, y1, z2), p(x2, y2, z2), p(x1, y2, z2)))
        if bottom:
            sides.append((p(x1, y2, z1), p(x2, y2, z1), p(x2, y1, z1), p(x1, y1, z1)))
        for corners in sides:
            self.quad(limb, corners)

    def cylinder(self, limb, x, y, radius, z1, z2, segments=12, top=True):
        ring = [(x + radius * math.cos(2 * math.pi * i / segments), y + radius * math.sin(2 * math.pi * i / segments))
                for i in range(segments)]
        for i in range(segments):
            (ax, ay), (bx, by) = ring[i], ring[(i + 1) % segments]
            self.quad(limb, (Vector((ax, ay, z1)), Vector((bx, by, z1)), Vector((bx, by, z2)), Vector((ax, ay, z2))))
        if top:
            # A fan cap kept as one island so the lightmap stays continuous.
            centre = Vector((x, y, z2))
            for i in range(segments):
                (ax, ay), (bx, by) = ring[i], ring[(i + 1) % segments]
                self.quad(limb, (centre, Vector((ax, ay, z2)), Vector((bx, by, z2))), island=("cap", limb, x, y, z2))


def wall_runs(rooms, index):
    """Yield the wall pieces of a room: (corners along the edge, z bottom, z top)."""
    x1, y1, x2, y2, height = rooms[index]
    edges = (("s", y1, x1, x2), ("n", y2, x1, x2), ("w", x1, y1, y2), ("e", x2, y1, y2))
    for side, line, a, b in edges:
        openings = []
        for other_index, (ox1, oy1, ox2, oy2, oh) in enumerate(rooms):
            if other_index == index:
                continue
            if side in "sn":
                touching = (side == "s" and oy2 == line) or (side == "n" and oy1 == line)
                lo, hi = max(a, ox1), min(b, ox2)
            else:
                touching = (side == "w" and ox2 == line) or (side == "e" and ox1 == line)
                lo, hi = max(a, oy1), min(b, oy2)
            if touching and hi > lo:
                openings.append((lo, hi, min(oh, height)))
        openings.sort()
        cursor = a
        for lo, hi, top in openings:
            if lo > cursor:
                yield side, line, cursor, lo, 0.0, height
            if top < height:
                yield side, line, lo, hi, top, height
            cursor = hi
        if cursor < b:
            yield side, line, cursor, b, 0.0, height


def edge_quad(side, line, a, b, z1, z2, inset=0.0):
    """A wall quad facing into the room it bounds."""
    if side == "s":
        y = line + inset
        return (Vector((b, y, z1)), Vector((a, y, z1)), Vector((a, y, z2)), Vector((b, y, z2)))
    if side == "n":
        y = line - inset
        return (Vector((a, y, z1)), Vector((b, y, z1)), Vector((b, y, z2)), Vector((a, y, z2)))
    if side == "w":
        x = line + inset
        return (Vector((x, a, z1)), Vector((x, b, z1)), Vector((x, b, z2)), Vector((x, a, z2)))
    x = line - inset
    return (Vector((x, b, z1)), Vector((x, a, z1)), Vector((x, a, z2)), Vector((x, b, z2)))


def build_level_faces(layout):
    rooms = [tuple(values) for values in layout["room"]]
    level = Builder()
    for index, (x1, y1, x2, y2, height) in enumerate(rooms):
        level.quad("floor", (Vector((x1, y1, 0)), Vector((x2, y1, 0)), Vector((x2, y2, 0)), Vector((x1, y2, 0))))
        level.quad("ceiling", (Vector((x1, y2, height)), Vector((x2, y2, height)), Vector((x2, y1, height)),
                               Vector((x1, y1, height))))
        for side, line, a, b, z1, z2 in wall_runs(rooms, index):
            level.quad("wall", edge_quad(side, line, a, b, z1, z2))
            if z1 == 0.0:
                # A hazard-striped skirting board along every solid wall: front and top.
                front = edge_quad(side, line, a, b, 0.0, 3.0, 1.0)
                normal = (front[1] - front[0]).cross(front[2] - front[0]).normalized()
                level.quad("trim", front)
                level.quad("trim", (front[3], front[2], front[2] - normal, front[3] - normal))
    for x, y, width in layout.get("pillar", []):
        h = width / 2
        level.box("machine", x - h, y - h, 0.0, x + h, y + h, 90.0, top=False)
        level.box("trim", x - h - 1, y - h - 1, 0.0, x + h + 1, y + h + 1, 4.0)
    for x, y, radius in layout.get("pump", []):
        level.cylinder("machine", x, y, radius, 0.0, 34.0, 12)
        level.cylinder("trim", x, y, radius + 1.5, 0.0, 4.0, 12)
        level.box("machine", x - 2, y - 2, 34.0, x + 2, y + 2, 56.0, top=False)
    for x, y, radius in layout.get("core", []):
        level.cylinder("trim", x, y, radius, 0.0, 6.0, 16)
        level.cylinder("machine", x, y, radius - 4, 6.0, 10.0, 16)
        level.cylinder("machine", x, y, radius - 6, 60.0, 68.0, 16, top=False)
        level.cylinder("machine", x, y, radius - 9, 68.0, 90.0, 12, top=False)
        for i in range(4):
            angle = math.pi / 4 + i * math.pi / 2
            sx, sy = x + (radius - 7) * math.cos(angle), y + (radius - 7) * math.sin(angle)
            level.box("machine", sx - 1.5, sy - 1.5, 10.0, sx + 1.5, sy + 1.5, 60.0, top=False)
    for x, y, facing in layout.get("console", []):
        level.box("machine", x - 8, y - 5, 0.0, x + 8, y + 5, 10.0)
    # A catwalk ledge around the reactor hall, high on its walls.
    hx1, hy1, hx2, hy2, _ = rooms[2]
    for x1, y1, x2, y2 in ((hx1, hy1, hx1 + 8, hy2), (hx2 - 8, hy1, hx2, hy2)):
        level.box("trim", x1, y1, 52.0, x2, y2, 54.0, top=True, bottom=True)
    return level


def island_basis(normal):
    if normal.z > 0.5:
        return Vector((1, 0, 0)), Vector((0, 1, 0))
    if normal.z < -0.5:
        return Vector((1, 0, 0)), Vector((0, -1, 0))
    up = Vector((0, 0, 1))
    return (-normal).cross(up).normalized(), up


def pack_islands(islands):
    """Shelf-pack the islands at one texel density; returns (pixels per unit, offsets)."""
    def attempt(scale):
        x = y = shelf = 0
        offsets = []
        for width, height in sizes:
            w, h = math.ceil(width * scale) + MARGIN, math.ceil(height * scale) + MARGIN
            if x + w > ATLAS:
                x, y, shelf = 0, y + shelf, 0
            offsets.append((x + MARGIN / 2, y + MARGIN / 2))
            x, shelf = x + w, max(shelf, h)
        return y + shelf <= ATLAS, offsets

    order = sorted(range(len(islands)), key=lambda i: -islands[i]["size"][1])
    sizes = [islands[i]["size"] for i in order]
    low, high = 0.1, 8.0
    for _ in range(40):
        middle = (low + high) / 2
        if attempt(middle)[0]:
            low = middle
        else:
            high = middle
    _, offsets = attempt(low)
    placed = [None] * len(islands)
    for rank, index in enumerate(order):
        placed[index] = offsets[rank]
    return low, placed


def level_objects(level):
    """Turn the collected faces into Blender meshes with the shared atlas UVs."""
    islands, face_island = [], []
    keyed = {}
    for limb, faces in level.limbs.items():
        for corners, key in faces:
            normal = (corners[1] - corners[0]).cross(corners[2] - corners[0]).normalized()
            u_axis, v_axis = island_basis(normal)
            if key is None or key not in keyed:
                islands.append({"u": u_axis, "v": v_axis, "points": []})
                if key is not None:
                    keyed[key] = len(islands) - 1
            index = keyed[key] if key is not None else len(islands) - 1
            islands[index]["points"].extend((corner.dot(u_axis), corner.dot(v_axis)) for corner in corners)
            face_island.append((limb, corners, index))
    for island in islands:
        us, vs = zip(*island["points"])
        island["min"] = (min(us), min(vs))
        island["size"] = (max(us) - min(us), max(vs) - min(vs))
    scale, offsets = pack_islands(islands)

    objects = {}
    for limb in level.limbs:
        mesh = bpy.data.meshes.new(limb)
        bm = bmesh.new()
        uv_layer = bm.loops.layers.uv.new("atlas")
        for face_limb, corners, index in face_island:
            if face_limb != limb:
                continue
            island = islands[index]
            face = bm.faces.new([bm.verts.new(corner) for corner in corners])
            for loop, corner in zip(face.loops, corners):
                u = (corner.dot(island["u"]) - island["min"][0]) * scale + offsets[index][0]
                v = (corner.dot(island["v"]) - island["min"][1]) * scale + offsets[index][1]
                loop[uv_layer].uv = (u / ATLAS, v / ATLAS)
        bm.to_mesh(mesh)
        bm.free()
        obj = bpy.data.objects.new(limb, mesh)
        bpy.context.scene.collection.objects.link(obj)
        objects[limb] = obj
    tiling = ATLAS / (scale * TILE)
    print(f"LEVEL_TILING {tiling:.4f} ({scale:.3f} px per unit)")
    return objects, tiling


# ---------------------------------------------------------------------------
# Light panels and screens: drawn unlit by the game, emitters in the bake.

ROOM_LIGHTS = (  # panel tint per room, in layout order
    (0.85, 0.92, 1.0), (1.0, 1.0, 0.95), (0.80, 0.90, 1.0), (1.0, 1.0, 0.95), (0.55, 1.0, 0.80),
    (1.0, 1.0, 0.95), (1.0, 0.72, 0.40), (1.0, 1.0, 0.95), (0.75, 0.55, 1.0))


def build_lamps(layout):
    rooms = [tuple(values) for values in layout["room"]]
    lamps = Builder()
    panels = []
    for index, (x1, y1, x2, y2, height) in enumerate(rooms):
        width, depth = x2 - x1, y2 - y1
        z = height - 0.3
        if width < 40 or depth < 40:
            # Corridors: a strip down the middle.
            long_x = width > depth
            count = max(1, int((width if long_x else depth) // 30))
            for i in range(count):
                t = (i + 0.5) / count
                cx = x1 + width * t if long_x else (x1 + x2) / 2
                cy = (y1 + y2) / 2 if long_x else y1 + depth * t
                hw, hd = (6, 2) if long_x else (2, 6)
                panels.append((index, cx - hw, cy - hd, cx + hw, cy + hd, z))
        else:
            # Few, spaced panels keep pools of light and dark between them.
            spacing = 40 if width <= 100 else 90
            nx, ny = max(1, int(width // spacing)), max(1, int(depth // spacing))
            for i in range(nx):
                for j in range(ny):
                    cx = x1 + width * (i + 0.5) / nx
                    cy = y1 + depth * (j + 0.5) / ny
                    panels.append((index, cx - 3, cy - 9, cx + 3, cy + 9, z))
    for index, ax, ay, bx, by, z in panels:
        lamps.quad("lamp", (Vector((ax, by, z)), Vector((bx, by, z)), Vector((bx, ay, z)), Vector((ax, ay, z))))
    screens = []
    for x, y, facing in layout.get("console", []):
        # Screens lean back on top of the console, facing -y (into the control room).
        quad = (Vector((x - 7, y - 4, 10.5)), Vector((x + 7, y - 4, 10.5)), Vector((x + 7, y + 2, 20.0)),
                Vector((x - 7, y + 2, 20.0)))
        lamps.quad("screen", quad)
        screens.append(quad)
    return lamps, panels, screens


def simple_mesh(name, builder_faces, mat):
    mesh = bpy.data.meshes.new(name)
    bm = bmesh.new()
    uv_layer = bm.loops.layers.uv.new("uv")
    for corners, _ in builder_faces:
        face = bm.faces.new([bm.verts.new(corner) for corner in corners])
        uvs = ((0, 0), (1, 0), (1, 1), (0, 1))
        for loop, uv in zip(face.loops, uvs):
            loop[uv_layer].uv = uv
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new(name, mesh)
    obj.data.materials.append(mat)
    bpy.context.scene.collection.objects.link(obj)
    return obj


# ---------------------------------------------------------------------------
# Lightmap bake

def emission_material(name, colour, strength):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nodes = mat.node_tree.nodes
    nodes.clear()
    emission = nodes.new("ShaderNodeEmission")
    emission.inputs["Color"].default_value = (*colour, 1.0)
    emission.inputs["Strength"].default_value = strength
    output = nodes.new("ShaderNodeOutputMaterial")
    mat.node_tree.links.new(emission.outputs[0], output.inputs["Surface"])
    return mat


def diffuse_material(name, grey):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    shader = mat.node_tree.nodes["Principled BSDF"]
    shader.inputs["Base Color"].default_value = (grey, grey, grey, 1.0)
    shader.inputs["Roughness"].default_value = 1.0
    return mat


def use_gpu() -> None:
    try:
        preferences = bpy.context.preferences.addons["cycles"].preferences
        preferences.compute_device_type = "METAL"
        preferences.get_devices()
        devices = [device for device in preferences.devices if device.type == "METAL"]
        for device in devices:
            device.use = True
        if devices:
            bpy.context.scene.cycles.device = "GPU"
    except (KeyError, TypeError, AttributeError):
        pass


def bake_lightmap(layout, objects, panels, screens) -> None:
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    use_gpu()
    scene.cycles.samples = 256
    scene.cycles.max_bounces = 2
    world = bpy.data.worlds.new("dark")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs["Color"].default_value = (0.004, 0.005, 0.007, 1.0)
    scene.world = world

    # One bake mesh: every level limb joined, all sharing the atlas.
    bpy.ops.object.select_all(action="DESELECT")
    copies = []
    for obj in objects.values():
        copy = obj.copy()
        copy.data = obj.data.copy()
        scene.collection.objects.link(copy)
        copies.append(copy)
    for copy in copies:
        copy.select_set(True)
    bpy.context.view_layer.objects.active = copies[0]
    bpy.ops.object.join()
    target = bpy.context.active_object
    target.data.materials.clear()
    surface = diffuse_material("bake_surface", 0.5)
    target.data.materials.append(surface)
    image = bpy.data.images.new("lightmap", ATLAS, ATLAS, float_buffer=True)
    node = surface.node_tree.nodes.new("ShaderNodeTexImage")
    node.image = image
    surface.node_tree.nodes.active = node

    # Emitters and occluders that the game draws as separate objects.
    rooms = layout["room"]
    for index, ax, ay, bx, by, z in panels:
        mat = emission_material(f"panel_{index}_{ax}_{ay}", ROOM_LIGHTS[index], 9.0)
        simple_mesh("panel", [((Vector((ax, by, z)), Vector((bx, by, z)), Vector((bx, ay, z)), Vector((ax, ay, z))),
                               None)], mat)
    screen_glow = emission_material("screen_glow", (0.3, 1.0, 0.85), 3.0)
    for quad in screens:
        simple_mesh("screen", [(quad, None)], screen_glow)
    core_glow = emission_material("core_glow", (0.25, 0.85, 1.0), 14.0)
    occluder = diffuse_material("occluder", 0.45)
    for x, y, radius in layout.get("core", []):
        bpy.ops.mesh.primitive_cylinder_add(vertices=24, radius=radius - 10, depth=50, location=(x, y, 35))
        bpy.context.active_object.data.materials.append(core_glow)
    for x, y, size, yaw in layout.get("crate", []):
        bpy.ops.mesh.primitive_cube_add(size=size, location=(x, y, size / 2), rotation=(0, 0, math.radians(-yaw)))
        bpy.context.active_object.data.materials.append(occluder)
    for x, y, facing in layout.get("valve", []):
        bpy.ops.mesh.primitive_cube_add(size=6, location=(x, y, 12))
        bpy.context.active_object.data.materials.append(occluder)
    # Red emergency lamps above every doorway.
    red = emission_material("emergency", (1.0, 0.12, 0.05), 25.0)
    for (x1, y1, x2, y2, h), (ox1, oy1, ox2, oy2, oh) in ((a, b) for a in rooms for b in rooms if a is not b):
        if y2 == oy1 and max(x1, ox1) < min(x2, ox2) and h >= oh:
            cx = (max(x1, ox1) + min(x2, ox2)) / 2
            for y in (y2 - 0.6, y2 + 0.6):
                bpy.ops.mesh.primitive_cube_add(size=1, location=(cx, y, min(h, oh) + 3), scale=(4, 0.5, 1.2))
                bpy.context.active_object.data.materials.append(red)
        if x2 == ox1 and max(y1, oy1) < min(y2, oy2) and h >= oh:
            cy = (max(y1, oy1) + min(y2, oy2)) / 2
            for x in (x2 - 0.6, x2 + 0.6):
                bpy.ops.mesh.primitive_cube_add(size=1, location=(x, cy, min(h, oh) + 3), scale=(0.5, 4, 1.2))
                bpy.context.active_object.data.materials.append(red)

    bpy.ops.object.select_all(action="DESELECT")
    target.select_set(True)
    bpy.context.view_layer.objects.active = target
    bpy.ops.object.bake(type="DIFFUSE", pass_filter={"DIRECT", "INDIRECT"}, margin=MARGIN, margin_type="EXTEND")
    pixels = np.empty(ATLAS * ATLAS * 4, dtype=np.float32)
    image.pixels.foreach_get(pixels)
    pixels = pixels.reshape(ATLAS, ATLAS, 4)
    # The game blends the lightmap with MODULATE2X: 0.5 leaves the material as authored,
    # brighter values overexpose it near the lamps. A soft shoulder keeps hot spots in range.
    light = pixels[..., :3]
    pixels[..., :3] = 0.6 * light / (1.0 + 0.5 * light) + 0.03
    pixels[..., 3] = 1.0
    bt.save_jpeg(pixels, OUT / "lightmap.jpg")


def reproject(obj, size):
    """Cube-project the UVs again so one texture repeat spans `size` units."""
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.cube_project(cube_size=size)
    bpy.ops.object.mode_set(mode="OBJECT")
    return obj


def join_into(target, others):
    # Bake every modifier first: a join keeps only the target's modifiers.
    for obj in (target, *others):
        bpy.context.view_layer.objects.active = obj
        for modifier in list(obj.modifiers):
            bpy.ops.object.modifier_apply(modifier=modifier.name)
    bpy.ops.object.select_all(action="DESELECT")
    for obj in others:
        obj.select_set(True)
    target.select_set(True)
    bpy.context.view_layer.objects.active = target
    bpy.ops.object.join()


# ---------------------------------------------------------------------------
# Drone: five clips on one timeline, played with LOOP OBJECT / PLAY OBJECT ranges.

CLIPS = {"idle": (0, 40), "move": (41, 71), "attack": (72, 92), "hit": (93, 103), "die": (104, 150)}
HOVER = 15.0


def build_drone():
    armour, dark = material("drone"), material("drone_dark", (0.08, 0.08, 0.09, 1.0))
    eye_mat, jet = material("drone_eye", (1.0, 0.15, 0.05, 1.0)), material("drone_jet", (0.3, 0.8, 1.0, 1.0))
    # Spheres keep their 16 x 8 facets with smooth normals: light geometry for the stencil shadows.
    hull = reproject(part("sphere", "hull", (10.0, 10.0, 8.5), (0, 0, HOVER), None, 0, armour), 10.0)
    for polygon in hull.data.polygons:
        polygon.use_smooth = True
    # Parts that never move on their own are joined into the hull: fewer limbs, smaller clips.
    visor = part("cylinder", "visor", (5.0, 5.0, 2.0), (0, 4.0, 0.4), hull, 0, dark)
    visor.rotation_euler = (math.pi / 2, 0, 0)
    fin = part("cube", "fin", (0.8, 6.0, 3.0), (0, -1.0, 4.0), hull, 0, dark, (0, 0, 1.2), 0.2)
    jet_ring = part("cylinder", "jet", (5.0, 5.0, 1.2), (0, 0, -4.2), hull, 0, jet)
    antenna = part("cylinder", "antenna", (0.3, 0.3, 4.0), (1.5, -2.0, 4.0), hull, 0, dark, (0, 0, 2.0))
    bpy.context.view_layer.update()
    for obj in (visor, fin, jet_ring, antenna):
        reproject(obj, 4.0)
    join_into(hull, (visor, fin, jet_ring, antenna))
    parts = {"hull": hull}
    parts["eye"] = part("sphere", "eye", (2.6, 2.6, 2.6), (0, 5.0, 0.4), hull, 1, eye_mat)
    for polygon in parts["eye"].data.polygons:
        polygon.use_smooth = True
    order = 2
    for side, sign in (("l", -1), ("r", 1)):
        arm = part("cube", f"arm_{side}", (3.0, 1.6, 1.4), (sign * 4.6, 0, -0.5), hull, order, dark, (sign * 1.5, 0, 0))
        pod = part("cylinder", f"pod_{side}", (2.2, 2.2, 6.5), (sign * 3.2, 0.5, 0), arm, order + 1, armour)
        pod.rotation_euler = (math.pi / 2, 0, 0)
        reproject(pod, 6.0)
        parts[arm.name], parts[pod.name] = arm, pod
        order += 2
    rest = {name: (obj.location.copy(), obj.rotation_euler.copy()) for name, obj in parts.items()}
    return hull, parts, rest


def drone_pose(parts, rest, frame, lift=0.0, pitch=0.0, roll=0.0, recoil=0.0, spread=0.0, droop=0.0):
    location, rotation = rest["hull"]
    keyframe(parts["hull"], frame, rotation=(rotation.x + pitch, rotation.y + roll, rotation.z),
             location=location + Vector((0, 0, lift)))
    for side, sign in (("l", -1), ("r", 1)):
        arm_location, arm_rotation = rest[f"arm_{side}"]
        keyframe(parts[f"arm_{side}"], frame, rotation=(arm_rotation.x, sign * droop, arm_rotation.z),
                 location=arm_location + Vector((sign * spread, 0, 0)))
        pod_location, pod_rotation = rest[f"pod_{side}"]
        keyframe(parts[f"pod_{side}"], frame, rotation=pod_rotation, location=pod_location + Vector((0, -recoil, 0)))


def animate_drone(parts, rest):
    for obj in parts.values():
        obj.animation_data_clear()
    keyframe(parts["eye"], 0, rotation=rest["eye"][1], location=rest["eye"][0])
    first, last = CLIPS["idle"]
    for frame in range(first, last + 1):
        s = math.sin(2 * math.pi * (frame - first) / (last - first))
        drone_pose(parts, rest, frame, lift=0.8 * s, roll=0.04 * s)
    first, last = CLIPS["move"]
    for frame in range(first, last + 1):
        s = math.sin(2 * math.pi * (frame - first) / (last - first))
        drone_pose(parts, rest, frame, lift=0.5 * s, pitch=-0.25, roll=0.06 * s, spread=-0.4)
    first, last = CLIPS["attack"]
    for frame in range(first, last + 1):
        t = (frame - first) / (last - first)
        kick = math.exp(-12 * t) if t > 0 else 0.0
        drone_pose(parts, rest, frame, pitch=0.12 * kick, recoil=2.5 * kick + 1.0 * math.exp(-12 * abs(t - 0.5)),
                   spread=0.6)
    first, last = CLIPS["hit"]
    for frame in range(first, last + 1):
        t = (frame - first) / (last - first)
        shake = math.sin(t * math.pi * 3) * (1 - t)
        drone_pose(parts, rest, frame, lift=-1.0 * (1 - t), pitch=0.3 * shake, roll=0.4 * shake)
    first, last = CLIPS["die"]
    for frame in range(first, last + 1):
        t = (frame - first) / (last - first)
        fall = min(1.0, t * 1.6) ** 2
        bounce = 0.0 if t < 0.625 else 1.5 * math.sin((t - 0.625) / 0.375 * math.pi) * (1 - t)
        drone_pose(parts, rest, frame, lift=-(HOVER - 4.2) * fall + bounce, pitch=0.5 * fall, roll=1.1 * fall,
                   spread=1.5 * fall, droop=0.9 * fall)


# ---------------------------------------------------------------------------
# Props

def build_rifle():
    metal, cell_mat = material("rifle"), material("rifle_cell", (0.3, 0.9, 1.0, 1.0))
    dark = material("rifle_dark", (0.05, 0.05, 0.06, 1.0))
    body = part("cube", "receiver", (1.6, 6.0, 2.2), (0, 0, 0), None, 0, metal, bevel=0.15)
    part("cylinder", "barrel", (0.7, 0.7, 5.0), (0, 5.2, 0.3), body, 1, dark).rotation_euler = (math.pi / 2, 0, 0)
    part("cube", "shroud", (1.9, 3.6, 1.6), (0, 3.6, 0.5), body, 2, metal, bevel=0.2)
    part("cylinder", "cell", (0.7, 0.7, 2.6), (-0.95, 0.6, 0.3), body, 3, cell_mat).rotation_euler = (math.pi / 2, 0, 0)
    part("cube", "sight", (0.6, 2.0, 0.8), (0, -0.5, 1.5), body, 4, dark, bevel=0.1)
    grip = part("cube", "grip", (1.2, 1.4, 3.0), (0, -1.8, -1.0), body, 5, dark, (0, 0, -1.4), 0.15)
    grip.rotation_euler = (-0.35, 0, 0)
    part("cube", "magazine", (1.1, 1.6, 2.2), (0, 1.2, -1.6), body, 6, metal, bevel=0.1)
    for obj in list(bpy.data.objects):
        reproject(obj, 4.0)
    return body


def build_valve():
    pipe, wheel_mat = material("valve_pipe", (0.6, 0.6, 0.62, 1.0)), material("valve")
    base = reproject(part("cube", "plate", (1.0, 10.0, 10.0), (0, 0, 12), None, 0, pipe, bevel=0.2), 10.0)
    part("cylinder", "stem", (2.0, 2.0, 4.0), (2.2, 0, 0), base, 1, pipe).rotation_euler = (0, math.pi / 2, 0)
    bpy.ops.mesh.primitive_torus_add(major_radius=4.0, minor_radius=0.6, major_segments=20, minor_segments=8)
    wheel = bpy.context.active_object
    wheel.name = "wheel"
    for index in range(3):
        bpy.ops.mesh.primitive_cube_add(size=1.0, scale=(8.0, 0.6, 0.5), rotation=(0, 0, index * math.pi / 3))
        spoke = bpy.context.active_object
        bpy.ops.object.select_all(action="DESELECT")
        spoke.select_set(True)
        wheel.select_set(True)
        bpy.context.view_layer.objects.active = wheel
        bpy.ops.object.join()
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.cube_project(cube_size=4.0)
    bpy.ops.object.mode_set(mode="OBJECT")
    wheel.data.materials.append(wheel_mat)
    # The wheel spins about its own +X axis (the game's limb X rotation).
    wheel.rotation_euler = (0, math.pi / 2, 0)
    bpy.ops.object.transform_apply(rotation=True)
    wheel.parent = base
    wheel.location = (4.5, 0, 0)
    wheel["limb_order"] = 2
    return base


# ---------------------------------------------------------------------------

def main() -> None:
    layout = read_layout()
    build_textures()

    bpy.ops.wm.read_factory_settings(use_empty=True)
    level = build_level_faces(layout)
    objects, tiling = level_objects(level)
    names = {"floor": "floor.jpg", "wall": "wall.jpg", "ceiling": "ceiling.jpg", "trim": "hazard.jpg",
             "machine": "machine.jpg"}
    order = ("floor", "wall", "ceiling", "trim", "machine")
    root = objects[order[0]]
    for index, name in enumerate(order):
        objects[name].data.materials.append(material(f"level_{name}"))
        objects[name]["limb_order"] = index
        if objects[name] is not root:
            objects[name].parent = root
    xexport.export(OUT / "level.x", [root], {f"level_{name}": file for name, file in names.items()})

    lamps, panels, screens = build_lamps(layout)
    lamp_obj = simple_mesh("lamps", lamps.limbs["lamp"], material("lamp"))
    screen_obj = simple_mesh("screens", lamps.limbs["screen"], material("screen"))
    screen_obj.parent = lamp_obj
    screen_obj["limb_order"] = 1
    xexport.export(OUT / "lamps.x", [lamp_obj], {"lamp": "lamp.jpg", "screen": "screen.jpg"})
    bpy.data.objects.remove(lamp_obj)
    bpy.data.objects.remove(screen_obj)
    bake_lightmap(layout, objects, panels, screens)

    bpy.ops.wm.read_factory_settings(use_empty=True)
    hull, parts, rest = build_drone()
    animate_drone(parts, rest)
    xexport.export(OUT / "drone.x", [hull], {"drone": "drone.jpg", "drone_eye": "eye.jpg", "drone_jet": "plasma.jpg"},
                   (0, CLIPS["die"][1]))

    bpy.ops.wm.read_factory_settings(use_empty=True)
    xexport.export(OUT / "rifle.x", [build_rifle()], {"rifle": "rifle.jpg", "rifle_cell": "plasma.jpg"})
    bpy.ops.wm.read_factory_settings(use_empty=True)
    xexport.export(OUT / "valve.x", [build_valve()], {"valve": "valve.jpg", "valve_pipe": "machine.jpg"})


if __name__ == "__main__":
    main()
    sys.exit(0)
