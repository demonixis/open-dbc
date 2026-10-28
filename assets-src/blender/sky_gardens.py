"""Build the Sky Gardens demo media: the explorer and its animation timeline,
the floating island, ruin arch, tree, lantern, checkpoint banner, bounce
mushroom, shrine, seed gem and their textures.

Run from the repository root:
    /Applications/Blender.app/Contents/MacOS/Blender --background --factory-startup \
        --python assets-src/blender/sky_gardens.py

The level layout lives in the DATA statements of sky_gardens.dba; this script
only builds the reusable pieces it places. Every model is sized in game units
(the explorer is three units tall).
"""

import math
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

OUT = bt.ROOT / "examples/dbpro/demos/sky_gardens"


# ---------------------------------------------------------------------------
# Textures

def sky_gradient(size=512) -> np.ndarray:
    """Golden-hour sky as linear RGBA rows from the bottom of the image.

    MAKE OBJECT SPHERE maps the top of the image to its upper pole; the horizon is
    the middle row. Painted directly: the lit bake of finish() would grey it."""
    stops = [(-0.5, (0.95, 0.62, 0.42)), (-0.02, (1.0, 0.72, 0.46)), (0.0, (1.0, 0.78, 0.48)),
             (0.04, (1.0, 0.70, 0.50)), (0.12, (0.62, 0.70, 0.95)), (0.28, (0.24, 0.42, 0.90)),
             (0.5, (0.08, 0.18, 0.62))]
    heights = (np.arange(size) + 0.5) / size - 0.5
    positions = [stop for stop, _ in stops]
    column = np.stack([np.interp(heights, positions, [colour[channel] for _, colour in stops])
                       for channel in range(3)], axis=-1)
    rgba = np.ones((size, size, 4), dtype=np.float32)
    rgba[..., :3] = column[:, None, :]
    # Faint streaks of cloud just above the horizon, periodic around the dome.
    u = np.arange(size) / size
    streak = 0.5 + 0.5 * np.sin(2 * np.pi * (3 * u[None, :] + 0.15 * np.sin(2 * np.pi * 7 * u[None, :])))
    band = np.exp(-((heights[:, None] - 0.06) / 0.025) ** 2)
    rgba[..., :3] += (band * streak * 0.35)[..., None] * np.array([1.0, 0.75, 0.6])
    return rgba


def cloud_sea(g):
    puffs = g.noise(1.2, 2, 0.5, 203.0, distortion=0.4)
    colour = g.ramp(puffs, [(0.30, (0.42, 0.40, 0.60)), (0.50, (0.70, 0.58, 0.62)), (0.75, (0.86, 0.76, 0.70))])
    g.finish(colour, puffs, 0.3)


def cloud_wisps(g):
    # Drawn additively above the cloud sea: sparse wisps on black.
    density = g.math("POWER", g.noise(2.5, 8, 0.6, 207.0, distortion=0.4), 3.0)
    g.finish(g.ramp(density, [(0.15, (0.0, 0.0, 0.0)), (0.55, (0.36, 0.28, 0.26))]), density, 0.0)


def grass(g):
    blades = g.noise(30.0, 4, 0.7, 211.0)
    patches = g.noise(3.0, 6, 0.55, 213.0)
    colour = g.ramp(g.math("ADD", g.math("MULTIPLY", patches, 0.6), g.math("MULTIPLY", blades, 0.4)),
                    [(0.25, (0.10, 0.24, 0.05)), (0.55, (0.24, 0.44, 0.10)), (0.8, (0.50, 0.62, 0.18))])
    petals = g.math("LESS_THAN", g.voronoi(14.0, "F1", "Distance", 215.0), 0.05)
    flower = g.mix(g.math("GREATER_THAN", g.noise(9.0, 2, 0.5, 217.0), 0.5), (1.0, 0.95, 0.85, 1.0),
                   (1.0, 0.70, 0.25, 1.0))
    g.finish(g.mix(petals, colour, flower), blades, 0.6)


def island_rock(g):
    strata = g.math("SINE", g.math("ADD", g.math("MULTIPLY", g.v, 40.0), g.math("MULTIPLY", g.noise(2.0, 4, 0.5, 221.0), 9.0)))
    rock = g.noise(6.0, 8, 0.6, 223.0)
    colour = g.ramp(g.math("ADD", g.math("MULTIPLY", rock, 0.6), g.math("MULTIPLY", strata, 0.15)),
                    [(0.2, (0.22, 0.14, 0.10)), (0.55, (0.46, 0.32, 0.22)), (0.85, (0.66, 0.52, 0.38))])
    roots = g.math("POWER", g.math("SUBTRACT", 1.0, g.voronoi(3.0, "DISTANCE_TO_EDGE", offset=225.0)), 18.0)
    g.finish(g.mix(g.math("MULTIPLY", roots, 0.7), colour, (0.20, 0.34, 0.10, 1.0)),
             g.math("ADD", rock, g.math("MULTIPLY", strata, 0.2)), 1.1)


def ruin(g):
    edge, cell = g.cells(3, 6, 0.5)
    surface = g.noise(8.0, 8, 0.6, 231.0)
    height, face = bt.bevel(g, edge, 0.010, 0.04, surface, 0.35)
    stone = g.ramp(g.math("ADD", g.math("MULTIPLY", cell, 0.4), g.math("MULTIPLY", surface, 0.6)),
                   [(0.2, (0.52, 0.42, 0.30)), (0.8, (0.86, 0.76, 0.58))])
    moss = g.math("MULTIPLY", g.math("POWER", g.noise(3.0, 5, 0.5, 233.0), 2.5), 3.0, clamp=True)
    stone = g.mix(g.math("MULTIPLY", moss, 0.8), stone, (0.30, 0.42, 0.14, 1.0))
    g.finish(g.mix(face, (0.16, 0.13, 0.10, 1.0), stone), height, 1.0)


def bark(g):
    fibres = g.noise(1.0, 6, 0.6, 241.0, distortion=1.5)
    lines = g.math("SINE", g.math("ADD", g.math("MULTIPLY", g.u, 90.0), g.math("MULTIPLY", fibres, 8.0)))
    colour = g.ramp(g.math("ADD", g.math("MULTIPLY", lines, 0.25), 0.5), [(0.2, (0.16, 0.10, 0.06)), (0.8, (0.42, 0.30, 0.18))])
    g.finish(colour, g.math("ADD", g.math("MULTIPLY", lines, 0.3), 0.5), 0.8)


def leaves(g):
    clusters = g.voronoi(8.0, "F1", "Distance", 243.0)
    shade = g.math("SUBTRACT", 1.0, clusters)
    tint = g.noise(4.0, 4, 0.5, 245.0)
    colour = g.ramp(g.math("ADD", g.math("MULTIPLY", shade, 0.7), g.math("MULTIPLY", tint, 0.3)),
                    [(0.2, (0.06, 0.18, 0.05)), (0.6, (0.22, 0.46, 0.10)), (0.9, (0.72, 0.78, 0.22))])
    g.finish(colour, shade, 0.9)


def mushroom(g):
    spots = g.math("LESS_THAN", g.voronoi(3.0, "F1", "Distance", 251.0), 0.30)
    cap = g.ramp(g.noise(6.0, 4, 0.5, 253.0), [(0.3, (0.62, 0.06, 0.08)), (0.8, (0.92, 0.22, 0.14))])
    g.finish(g.mix(spots, cap, (0.98, 0.94, 0.84, 1.0)), spots, 0.4)


def cloth(g):
    # Woven banner cloth with a row of diamond motifs; COLOR LIMB tints it per checkpoint state.
    weave = g.math("MULTIPLY", g.math("SINE", g.math("MULTIPLY", g.u, 400.0)), g.math("SINE", g.math("MULTIPLY", g.v, 400.0)))
    du = g.math("ABSOLUTE", g.math("SUBTRACT", g.math("FRACT", g.math("MULTIPLY", g.u, 3.0)), 0.5))
    dv = g.math("ABSOLUTE", g.math("SUBTRACT", g.math("FRACT", g.math("MULTIPLY", g.v, 3.0)), 0.5))
    diamond = g.math("LESS_THAN", g.math("ADD", du, dv), 0.22)
    outline = g.math("LESS_THAN", g.math("ABSOLUTE", g.math("SUBTRACT", g.math("ADD", du, dv), 0.30)), 0.025)
    base = g.ramp(g.math("ADD", 0.6, g.math("MULTIPLY", weave, 0.15)), [(0.4, (0.74, 0.74, 0.74)), (0.8, (0.95, 0.95, 0.95))])
    motif = g.mix(g.math("MAXIMUM", diamond, outline), base, (0.42, 0.36, 0.30, 1.0))
    g.finish(motif, g.math("ADD", 0.5, g.math("MULTIPLY", weave, 0.2)), 0.3)


def glow(g):
    _, _, radius, _ = polar(g)
    core = g.math("POWER", g.math("SUBTRACT", 1.0, g.math("MINIMUM", g.math("MULTIPLY", radius, 2.0), 1.0)), 2.2)
    g.finish(g.ramp(core, [(0.0, (0.0, 0.0, 0.0)), (0.5, (0.9, 0.55, 0.2)), (1.0, (1.0, 0.95, 0.8))]), core, 0.0)


def wood(g):
    edge, cell = g.cells(4, 1, 0.0)
    gap = g.math("GREATER_THAN", edge, 0.01)
    grain = g.noise(1.5, 3, 0.45, 261.0, distortion=0.3)
    stripes = g.math("SINE", g.math("ADD", g.math("MULTIPLY", g.u, 120.0), g.math("MULTIPLY", grain, 6.0)))
    timber = g.ramp(g.math("ADD", g.math("MULTIPLY", stripes, 0.15), g.math("ADD", 0.35, g.math("MULTIPLY", cell, 0.35))),
                    [(0.2, (0.30, 0.18, 0.08)), (0.8, (0.66, 0.46, 0.24))])
    g.finish(g.mix(gap, (0.08, 0.05, 0.02, 1.0), timber), gap, 0.5)


class HeightGraph(bt.Graph):
    """Emit the height instead of the lit colour, to derive a normal map."""

    def finish(self, colour, height, strength=0.8):
        emission = self.node("ShaderNodeEmission")
        self.link(self.combine_grey(height), emission.inputs["Color"])
        output = self.node("ShaderNodeOutputMaterial")
        self.link(emission.outputs[0], output.inputs["Surface"])


def bake_height(builder) -> np.ndarray:
    material_value = bpy.data.materials.new(builder.__name__ + "_height")
    material_value.use_nodes = True
    graph = HeightGraph(material_value)
    builder(graph)
    image = bpy.data.images.new(builder.__name__ + "_height", bt.SIZE, bt.SIZE, float_buffer=True)
    target = graph.node("ShaderNodeTexImage")
    target.image = image
    graph.nodes.active = target
    plane = bpy.context.active_object
    plane.data.materials.clear()
    plane.data.materials.append(material_value)
    bpy.ops.object.bake(type="EMIT", margin=0)
    pixels = np.empty(bt.SIZE * bt.SIZE * 4, dtype=np.float32)
    image.pixels.foreach_get(pixels)
    return pixels.reshape(bt.SIZE, bt.SIZE, 4)[..., 0]


def normal_map(height: np.ndarray, strength: float) -> np.ndarray:
    # Central differences that wrap, so the normal map tiles like its texture.
    dx = (np.roll(height, -1, axis=1) - np.roll(height, 1, axis=1)) * 0.5 * strength
    dy = (np.roll(height, -1, axis=0) - np.roll(height, 1, axis=0)) * 0.5 * strength
    normal = np.stack([-dx, -dy, np.ones_like(height)], axis=-1)
    normal /= np.linalg.norm(normal, axis=-1, keepdims=True)
    encoded = normal * 0.5 + 0.5
    return np.concatenate([encoded, np.ones_like(height)[..., None]], axis=-1)


def srgb(linear: np.ndarray) -> np.ndarray:
    rgb = np.clip(linear[..., :3], 0.0, 1.0)
    encoded = linear.copy()
    encoded[..., :3] = np.where(rgb <= 0.0031308, rgb * 12.92, 1.055 * np.power(rgb, 1.0 / 2.4) - 0.055)
    return encoded


def save_exact(display: np.ndarray, path: Path) -> None:
    """Write display-encoded RGBA rows (from the bottom) byte for byte.

    bake_textures.save_jpeg goes through the render pipeline's view transform, which
    shifts colours; the sky, the normal maps and the alpha cards need exact values."""
    height, width, _ = display.shape
    image = bpy.data.images.new(path.stem, width, height, alpha=True)
    image.pixels.foreach_set(np.clip(display, 0.0, 1.0).astype(np.float32).ravel())
    image.filepath_raw = str(path)
    image.file_format = "PNG" if path.suffix == ".png" else "JPEG"
    if path.suffix == ".png":
        image.save()
    else:
        image.save(quality=bt.JPEG_QUALITY)
    print(f"wrote {path.relative_to(bt.ROOT)}")


def fern_card(size=256) -> np.ndarray:
    """A tuft of grass blades with a few flowers, alpha-tested on a crossed card."""
    y, x = np.mgrid[0:size, 0:size].astype(np.float32) / size
    rgba = np.zeros((size, size, 4), dtype=np.float32)
    rng = np.random.default_rng(7)
    for _ in range(46):
        base = 0.5 + rng.normal(0, 0.08)
        tip_x = base + rng.normal(0, 0.22)
        tip_y = rng.uniform(0.45, 0.97)
        width = rng.uniform(0.012, 0.03)
        t = np.clip(y / tip_y, 0.0, 1.0)
        bend = rng.uniform(-0.08, 0.08)
        centre = base + (tip_x - base) * t + bend * t * t
        inside = (np.abs(x - centre) < width * (1.0 - t)) & (y <= tip_y)
        shade = 0.55 + 0.45 * t
        green = np.array([0.18 + 0.25 * rng.random(), 0.42 + 0.2 * rng.random(), 0.08])
        rgba[inside, :3] = green * shade[inside, None]
        rgba[inside, 3] = 1.0
    for _ in range(5):
        fx, fy = rng.uniform(0.25, 0.75), rng.uniform(0.55, 0.85)
        petal = (x - fx) ** 2 + (y - fy) ** 2 < 0.03 ** 2
        rgba[petal] = (1.0, 0.85, 0.35, 1.0)
        heart = (x - fx) ** 2 + (y - fy) ** 2 < 0.012 ** 2
        rgba[heart] = (0.9, 0.4, 0.1, 1.0)
    return rgba


def seed_icon(size=64) -> np.ndarray:
    """HUD icon: a glowing diamond on transparency."""
    y, x = np.mgrid[0:size, 0:size].astype(np.float32) / (size - 1) * 2.0 - 1.0
    diamond = np.abs(x) * 1.6 + np.abs(y)
    rgba = np.zeros((size, size, 4), dtype=np.float32)
    body = diamond < 0.85
    light = np.clip(1.0 - diamond, 0.0, 1.0)
    facet = np.where(x * y > 0, 1.0, 0.75)
    rgba[..., 0] = 0.25 + 0.75 * light * facet
    rgba[..., 1] = 0.85 * facet
    rgba[..., 2] = 0.75 * facet + 0.25 * light
    rgba[..., 3] = np.where(body, 1.0, np.clip((1.05 - diamond) * 3.0, 0.0, 0.6))
    return rgba


def build_textures() -> None:
    bt.setup_bake_scene()
    save_exact(srgb(sky_gradient()), OUT / "textures/sky.jpg")
    for builder, name in ((cloud_sea, "clouds"), (cloud_wisps, "wisps"), (grass, "grass"),
                          (island_rock, "rock"), (ruin, "ruin"), (bark, "bark"), (leaves, "leaves"),
                          (mushroom, "mushroom"), (cloth, "cloth"), (glow, "glow"), (wood, "wood")):
        save_exact(srgb(bt.bake(builder)), OUT / f"textures/{name}.jpg")
    save_exact(normal_map(bake_height(ruin), 9.0), OUT / "textures/ruin_normal.jpg")
    save_exact(normal_map(bake_height(island_rock), 5.0), OUT / "textures/rock_normal.jpg")
    save_exact(fern_card(), OUT / "textures/fern.png")
    save_exact(seed_icon(), OUT / "textures/seed_icon.png")


# ---------------------------------------------------------------------------
# Scenery (Blender: +Y is forward, +Z is up)

def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def lighten(ratio_for=None):
    """OpenDBC transforms and sorts triangles on the CPU: keep every piece light.

    Bevels drop to one segment and the named parts are decimated to the given ratio."""
    ratio_for = ratio_for or {}
    for obj in bpy.data.objects:
        for modifier in obj.modifiers:
            if modifier.type == "BEVEL":
                modifier.segments = 1
        ratio = next((value for prefix, value in ratio_for.items() if obj.name.startswith(prefix)), None)
        if ratio and obj.type == "MESH":
            obj.modifiers.new("decimate", "DECIMATE").ratio = ratio


def build_island():
    """A unit floating island: flat grass top at z = 0 with radius 1, a rocky root below."""
    grass_mat, rock_mat = material("grass"), material("rock")
    mesh = bpy.data.meshes.new("island")
    bm = bmesh.new()
    uv = bm.loops.layers.uv.new("UVMap")
    segments = 16
    rng = np.random.default_rng(11)
    # (z, radius, jitter) rings from the rim down to the tip of the root.
    rings = [(0.0, 1.0, 0.0), (-0.12, 1.03, 0.02), (-0.30, 0.95, 0.06), (-0.75, 0.66, 0.10),
             (-1.30, 0.30, 0.08)]
    loops = []
    for z, radius, jitter in rings:
        ring = []
        for index in range(segments):
            angle = 2 * math.pi * index / segments
            r = radius * (1.0 + rng.normal(0, jitter)) if jitter else radius
            ring.append(bm.verts.new((math.cos(angle) * r, math.sin(angle) * r, z + (rng.normal(0, 0.03) if jitter else 0))))
        loops.append(ring)
    centre = bm.verts.new((0, 0, 0))
    tip = bm.verts.new((0.05, -0.03, -1.9))
    grass_index, rock_index = 0, 1
    for index in range(segments):
        a, b = loops[0][index], loops[0][(index + 1) % segments]
        face = bm.faces.new((centre, a, b))
        face.material_index = grass_index
        for loop in face.loops:
            loop[uv].uv = (loop.vert.co.x * 2.0, loop.vert.co.y * 2.0)
    for level in range(len(loops) - 1):
        for index in range(segments):
            nxt = (index + 1) % segments
            face = bm.faces.new((loops[level + 1][index], loops[level + 1][nxt], loops[level][nxt], loops[level][index]))
            face.material_index = grass_index if level == 0 else rock_index
            u0, u1 = index / segments * 6.0, (index + 1) / segments * 6.0
            v0, v1 = -rings[level + 1][0] * 1.5, -rings[level][0] * 1.5
            for loop, coordinate in zip(face.loops, ((u0, v0), (u1, v0), (u1, v1), (u0, v1))):
                loop[uv].uv = coordinate
    for index in range(segments):
        face = bm.faces.new((tip, loops[-1][(index + 1) % segments], loops[-1][index]))
        face.material_index = rock_index
        for loop in face.loops:
            loop[uv].uv = (index / segments * 6.0, -loop.vert.co.z * 1.5)
    bm.to_mesh(mesh)
    bm.free()
    for polygon in mesh.polygons:
        polygon.use_smooth = polygon.material_index == rock_index
    obj = bpy.data.objects.new("island", mesh)
    bpy.context.scene.collection.objects.link(obj)
    obj.data.materials.append(grass_mat)
    obj.data.materials.append(rock_mat)
    return obj


def build_arch():
    stone = material("ruin")
    left = part("cube", "pillar_l", (1.4, 1.4, 7.0), (-3.5, 0, 0), None, 0, stone, (0, 0, 3.5), 0.08)
    part("cube", "pillar_r", (1.4, 1.4, 7.0), (7.0, 0, 0), left, 1, stone, (0, 0, 3.5), 0.08)
    lintel = part("cube", "lintel", (9.4, 1.8, 1.4), (3.5, 0, 7.0), left, 2, stone, (0, 0, 0.7), 0.08)
    lintel.rotation_euler = (0, 0.04, 0)  # slightly sagged, as ruins are
    part("cube", "block", (1.3, 1.0, 1.0), (7.2, 0.6, -6.6), lintel, 3, stone, (0, 0, 0.5), 0.1).rotation_euler = (0.2, 0.4, 0.5)
    return left


def build_tree():
    trunk_mat, leaf_mat = material("bark"), material("leaves")
    trunk = part("cylinder", "trunk", (0.9, 0.9, 4.6), (0, 0, 0), None, 0, trunk_mat, (0, 0, 2.3))
    for vertex in trunk.data.vertices:
        if vertex.co.z > 2.3:
            vertex.co.x *= 0.55
            vertex.co.y *= 0.55
    canopy = None
    for index, (x, y, z, r) in enumerate(((0, 0, 5.2, 2.6), (1.4, 0.5, 4.4, 1.8), (-1.2, -0.6, 4.6, 1.9),
                                          (0.2, -1.2, 5.8, 1.6))):
        bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=2, radius=r, location=(x, y, z))
        blob = bpy.context.active_object
        rng = np.random.default_rng(31 + index)
        for vertex in blob.data.vertices:
            vertex.co *= 1.0 + rng.normal(0, 0.06)
        bpy.ops.object.mode_set(mode="EDIT")
        bpy.ops.mesh.select_all(action="SELECT")
        bpy.ops.uv.sphere_project()
        bpy.ops.object.mode_set(mode="OBJECT")
        for loop in blob.data.uv_layers.active.data:
            loop.uv = (loop.uv[0] * 3.0, loop.uv[1] * 2.0)  # small leaves rather than giant scales
        for polygon in blob.data.polygons:
            polygon.use_smooth = True
        blob.data.materials.append(leaf_mat)
        if canopy is None:
            canopy = blob
        else:
            bpy.ops.object.select_all(action="DESELECT")
            blob.select_set(True)
            canopy.select_set(True)
            bpy.context.view_layer.objects.active = canopy
            bpy.ops.object.join()
    canopy.name = "canopy"
    # Keep the world placement while parenting: the canopy mesh is already in trunk space.
    canopy.parent = trunk
    canopy["limb_order"] = 1
    return trunk


def build_lantern():
    stone, light = material("ruin"), material("lantern_glow", (1.0, 0.85, 0.45, 1.0))
    post = part("cube", "post", (0.7, 0.7, 2.2), (0, 0, 0), None, 0, stone, (0, 0, 1.1), 0.06)
    part("cube", "cage", (0.6, 0.6, 0.7), (0, 0, 2.55), post, 1, light, bevel=0.04)
    part("cone", "roof", (1.3, 1.3, 0.7), (0, 0, 2.95), post, 2, stone, (0, 0, 0.35))
    return post


def build_banner():
    wood_mat, cloth_mat = material("wood"), material("cloth", (0.75, 0.75, 0.78, 1.0))
    pole = part("cylinder", "pole", (0.25, 0.25, 6.0), (0, 0, 0), None, 0, wood_mat, (0, 0, 3.0))
    part("cylinder", "bar", (0.16, 0.16, 2.6), (0, 0, 5.7), pole, 1, wood_mat).rotation_euler = (0, math.pi / 2, 0)
    # The flag hangs from the bar so ROTATE LIMB can make it sway around its top edge.
    flag = part("cube", "flag", (2.2, 0.06, 2.8), (1.2, 0, 5.6), pole, 2, cloth_mat, (0, 0, -1.4))
    for vertex in flag.data.vertices:
        if vertex.co.z < -2.6:
            vertex.co.z = -2.6 - 0.4 * (1.0 - abs(vertex.co.x) / 1.1)  # notched tail
    return pole


def build_mushroom():
    stem_mat, cap_mat = material("mushroom_stem", (0.95, 0.90, 0.78, 1.0)), material("mushroom")
    stem = part("cylinder", "stem", (1.2, 1.2, 1.4), (0, 0, 0), None, 0, stem_mat, (0, 0, 0.7), 0.1)
    bpy.ops.mesh.primitive_uv_sphere_add(segments=16, ring_count=8, radius=1.0)
    cap = bpy.context.active_object
    cap.name = "cap"
    for vertex in cap.data.vertices:
        vertex.co.z = max(vertex.co.z, -0.1) * 0.55
        vertex.co.x *= 2.0
        vertex.co.y *= 2.0
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.mesh.remove_doubles(threshold=0.01)
    bpy.ops.uv.sphere_project()
    bpy.ops.object.mode_set(mode="OBJECT")
    for polygon in cap.data.polygons:
        polygon.use_smooth = True
    cap.data.materials.append(cap_mat)
    cap.parent = stem
    cap.location = (0, 0, 1.3)
    cap["limb_order"] = 1
    return stem


def build_shrine():
    stone, gold = material("ruin"), material("shrine_gold", (1.0, 0.78, 0.30, 1.0))
    bpy.ops.mesh.primitive_cylinder_add(vertices=24, radius=6.0, depth=0.8, location=(0, 0, 0.4))
    base = bpy.context.active_object
    base.name = "dais"
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.cube_project(cube_size=4.0)
    bpy.ops.object.mode_set(mode="OBJECT")
    base.data.materials.append(stone)
    base["limb_order"] = 0
    for index in range(6):
        angle = index * math.pi / 3
        column = part("cylinder", f"column_{index + 1}", (1.0, 1.0, 7.5), (5.0 * math.cos(angle), 5.0 * math.sin(angle), 0.8),
                      base, 1 + index, stone, (0, 0, 3.75), 0.05)
        part("cube", f"capital_{index + 1}", (1.5, 1.5, 0.5), (0, 0, 7.6), column, 0, gold, bevel=0.05)
        bpy.ops.object.select_all(action="DESELECT")
        bpy.data.objects[f"capital_{index + 1}"].select_set(True)
        column.select_set(True)
        bpy.context.view_layer.objects.active = column
        bpy.ops.object.join()
    bpy.ops.mesh.primitive_torus_add(major_radius=5.0, minor_radius=0.35, major_segments=24, minor_segments=6,
                                     location=(0, 0, 8.9))
    ring = bpy.context.active_object
    ring.name = "ring"
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.cube_project(cube_size=2.0)
    bpy.ops.object.mode_set(mode="OBJECT")
    ring.data.materials.append(gold)
    ring.parent = base
    ring["limb_order"] = 7
    return base


def build_seed():
    """An elongated octahedron: the glowing seed the explorer collects."""
    gem = material("seed", (0.45, 1.0, 0.85, 1.0))
    mesh = bpy.data.meshes.new("seed")
    bm = bmesh.new()
    uv = bm.loops.layers.uv.new("UVMap")
    top, bottom = bm.verts.new((0, 0, 1.0)), bm.verts.new((0, 0, -1.0))
    ring = [bm.verts.new((math.cos(a) * 0.55, math.sin(a) * 0.55, 0.0)) for a in
            (index * math.pi / 3 for index in range(6))]
    for index in range(6):
        a, b = ring[index], ring[(index + 1) % 6]
        for face in (bm.faces.new((a, b, top)), bm.faces.new((b, a, bottom))):
            for loop, coordinate in zip(face.loops, ((0, 0), (1, 0), (0.5, 1))):
                loop[uv].uv = coordinate
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new("seed", mesh)
    bpy.context.scene.collection.objects.link(obj)
    obj.data.materials.append(gem)
    return obj


# ---------------------------------------------------------------------------
# The explorer: rigid limbs, one timeline holding every clip

# (first frame, length). Looping clips also key frame first + length with their first pose, so
# LOOP OBJECT first, first + length wraps without a jump; the next clip starts one frame later.
CLIPS = {"idle": (0, 40), "run": (41, 20), "jump": (62, 10), "fall": (73, 10), "wall": (84, 10)}


def join_into(target, names):
    bpy.ops.object.select_all(action="DESELECT")
    for name in names:
        bpy.data.objects[name].select_set(True)
    target.select_set(True)
    bpy.context.view_layer.objects.active = target
    bpy.ops.object.join()


def build_explorer():
    tunic = material("tunic", (0.22, 0.48, 0.62, 1.0))
    scarf = material("scarf", (0.92, 0.30, 0.16, 1.0))
    skin = material("skin", (0.96, 0.74, 0.58, 1.0))
    leather = material("leather", (0.42, 0.26, 0.14, 1.0))
    trousers = material("trousers", (0.86, 0.80, 0.64, 1.0))
    hat = material("hat", (0.76, 0.60, 0.36, 1.0))

    pelvis = part("cube", "pelvis", (0.62, 0.40, 0.34), (0, 0, 1.32), None, 0, trousers, bevel=0.08)
    torso = part("cube", "torso", (0.74, 0.46, 0.80), (0, 0, 0.12), pelvis, 1, tunic, (0, 0, 0.40), 0.14)
    part("cube", "belt", (0.78, 0.50, 0.12), (0, 0, 0.06), torso, 0, leather, bevel=0.03)
    part("cube", "pack", (0.62, 0.34, 0.62), (0, -0.40, 0.48), torso, 0, leather, bevel=0.08)
    part("cylinder", "bedroll", (0.30, 0.30, 0.72), (0, -0.42, 0.88), torso, 0, scarf).rotation_euler = (0, math.pi / 2, 0)
    part("cylinder", "collar", (0.56, 0.56, 0.16), (0, 0, 0.84), torso, 0, scarf, bevel=0.03)
    join_into(torso, ("belt", "pack", "bedroll", "collar"))

    head = part("sphere", "head", (0.78, 0.74, 0.80), (0, 0.02, 0.90), torso, 2, skin, (0, 0, 0.36))
    for polygon in head.data.polygons:
        polygon.use_smooth = True
    part("cylinder", "brim", (1.20, 1.20, 0.06), (0, 0, 0.62), head, 0, hat)
    part("cylinder", "crown", (0.70, 0.70, 0.34), (0, 0, 0.78), head, 0, hat, bevel=0.05)
    part("cube", "band", (0.72, 0.72, 0.07), (0, 0, 0.66), head, 0, scarf)
    part("cube", "nose", (0.12, 0.12, 0.12), (0, 0.38, 0.34), head, 0, skin)
    for side, sign in (("l", -1), ("r", 1)):
        part("cube", f"eye_{side}", (0.08, 0.05, 0.12), (sign * 0.15, 0.36, 0.44), head, 0,
             material("eye", (0.06, 0.05, 0.05, 1.0)))
    join_into(head, ("brim", "crown", "band", "nose", "eye_l", "eye_r"))

    # The scarf tail trails behind the neck and flutters on its own limb.
    tail = part("cube", "scarf_tail", (0.26, 0.06, 0.80), (0.12, -0.30, 0.80), torso, 3, scarf, (0, 0, -0.40))
    tail.rotation_euler = (-0.5, 0, 0)

    parts = {"pelvis": pelvis, "torso": torso, "head": head, "scarf_tail": tail}
    order = 4
    for side, sign in (("l", -1), ("r", 1)):
        arm = part("cube", f"arm_{side}", (0.22, 0.22, 0.52), (sign * 0.48, 0, 0.70), torso, order, tunic,
                   (0, 0, -0.24))
        fore = part("cube", f"forearm_{side}", (0.20, 0.20, 0.46), (0, 0, -0.48), arm, order + 1, skin, (0, 0, -0.20))
        part("cube", f"hand_{side}", (0.20, 0.20, 0.20), (0, 0, -0.46), fore, 0, skin)
        part("cube", f"glove_{side}", (0.23, 0.23, 0.16), (0, 0, -0.36), fore, 0, leather)
        join_into(fore, (f"hand_{side}", f"glove_{side}"))
        parts.update({arm.name: arm, fore.name: fore})
        order += 2
    for side, sign in (("l", -1), ("r", 1)):
        thigh = part("cube", f"thigh_{side}", (0.27, 0.29, 0.58), (sign * 0.17, 0, -0.10), pelvis, order, trousers,
                     (0, 0, -0.27))
        shin = part("cube", f"shin_{side}", (0.24, 0.26, 0.54), (0, 0, -0.56), thigh, order + 1, trousers, (0, 0, -0.25))
        part("cube", f"boot_{side}", (0.30, 0.46, 0.26), (0, 0.07, -0.50), shin, 0, leather, (0, 0, -0.03), 0.05)
        join_into(shin, (f"boot_{side}",))
        parts.update({thigh.name: thigh, shin.name: shin})
        order += 2
    parts["_rest"] = {name: obj.location.copy() for name, obj in parts.items() if not name.startswith("_")}
    parts["_rest_rotation"] = {name: obj.rotation_euler.copy() for name, obj in parts.items()
                               if not name.startswith("_")}
    return pelvis, parts


def pose(parts, frame, drop=0.0, lean=0.0, torso=0.0, head=0.0, arms=(0.0, 0.0), spread=(0.0, 0.0),
         forearms=(0.0, 0.0), legs=(0.0, 0.0), shins=(0.0, 0.0), scarf=-0.5, twist=0.0):
    rest, rest_rotation = parts["_rest"], parts["_rest_rotation"]
    keyframe(parts["pelvis"], frame, rotation=(lean, 0, 0), location=rest["pelvis"] + Vector((0, 0, -drop)))
    keyframe(parts["torso"], frame, rotation=(torso, 0, twist))
    keyframe(parts["head"], frame, rotation=(head, 0, -twist * 0.5))
    keyframe(parts["scarf_tail"], frame, rotation=(scarf, 0, rest_rotation["scarf_tail"].z))
    for index, side in enumerate(("l", "r")):
        sign = -1 if side == "l" else 1
        keyframe(parts[f"arm_{side}"], frame, rotation=(arms[index], -sign * spread[index], 0))
        keyframe(parts[f"forearm_{side}"], frame, rotation=(forearms[index], 0, 0))
        keyframe(parts[f"thigh_{side}"], frame, rotation=(legs[index], 0, 0))
        keyframe(parts[f"shin_{side}"], frame, rotation=(shins[index], 0, 0))


def animate(parts):
    for name, obj in parts.items():
        if not name.startswith("_"):
            obj.animation_data_clear()
    start, length = CLIPS["idle"]
    for k in range(length + 1):
        s = math.sin(2 * math.pi * k / length)
        pose(parts, start + k, drop=0.015 * (1 + s), torso=-0.03 * s, head=0.04 * s, arms=(0.06 * s, -0.06 * s),
             spread=(0.12, 0.12), forearms=(0.25, 0.25), scarf=-0.35 + 0.08 * s)
    start, length = CLIPS["run"]
    for k in range(length + 1):
        phase = 2 * math.pi * k / length
        s, c = math.sin(phase), math.cos(phase)
        pose(parts, start + k, drop=0.10 * abs(c), lean=-0.10, torso=-0.18, head=0.15,
             arms=(-0.95 * s, 0.95 * s), spread=(0.1, 0.1), forearms=(0.9, 0.9),
             legs=(1.0 * s, -1.0 * s), shins=(-1.1 * max(0.0, c), -1.1 * max(0.0, -c)),
             scarf=-1.25 + 0.15 * math.sin(2 * phase), twist=0.12 * s)
    start, length = CLIPS["jump"]
    for k in range(length):
        t = k / (length - 1)
        pose(parts, start + k, drop=-0.05 * t, lean=-0.05, torso=0.05, head=0.1,
             arms=(2.6 * t, 1.0 * t), spread=(0.3, 0.3), forearms=(0.3, 0.6),
             legs=(1.2 * t, -0.3 * t), shins=(-1.4 * t, -0.4 * t), scarf=-0.6 - 1.4 * t)
    start, length = CLIPS["fall"]
    for k in range(length + 1):
        s = math.sin(2 * math.pi * k / length)
        pose(parts, start + k, lean=0.1, torso=-0.1, head=-0.2, arms=(-0.6 + 0.3 * s, -0.6 - 0.3 * s),
             spread=(1.0, 1.0), forearms=(0.4, 0.4), legs=(0.4 * s, -0.4 * s), shins=(-0.6, -0.6),
             scarf=-2.4 + 0.3 * s)
    start, length = CLIPS["wall"]
    for k in range(length + 1):
        s = math.sin(2 * math.pi * k / length)
        pose(parts, start + k, lean=0.1, torso=0.1, head=0.1, arms=(0.4, 0.5 + 0.1 * s), spread=(1.3, 1.3),
             forearms=(0.6, 0.6), legs=(0.9, 0.1), shins=(-1.2, -0.3), scarf=-2.2 + 0.2 * s)


def main() -> None:
    build_textures()

    reset()
    pelvis, parts = build_explorer()
    lighten({"head": 0.5, "torso": 0.45})
    animate(parts)
    last = max(start + length for start, length in CLIPS.values())
    xexport.export(OUT / "models/explorer.x", [pelvis], {}, (0, last))

    pieces = (("island", build_island, {"grass": "grass.jpg", "rock": "rock.jpg"}),
              ("arch", build_arch, {"ruin": "ruin.jpg"}),
              ("tree", build_tree, {"bark": "bark.jpg", "leaves": "leaves.jpg"}),
              ("lantern", build_lantern, {"ruin": "ruin.jpg"}),
              ("banner", build_banner, {"wood": "wood.jpg", "cloth": "cloth.jpg"}),
              ("mushroom", build_mushroom, {"mushroom": "mushroom.jpg"}),
              ("shrine", build_shrine, {"ruin": "ruin.jpg"}),
              ("seed", build_seed, {}))
    for name, builder, textures in pieces:
        reset()
        root = builder()
        lighten({"cap": 0.4, "canopy": 0.6, "ring": 0.5})
        xexport.export(OUT / f"models/{name}.x", [root], textures)


if __name__ == "__main__":
    main()
    sys.exit(0)
