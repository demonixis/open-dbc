"""Build the Hive Defender demo media: the player's ship, the hive drone, the
mothership and its ring, a nebula backdrop and a strip of explosion frames.

Run from the repository root:
    /Applications/Blender.app/Contents/MacOS/Blender --background --factory-startup \
        --python assets-src/blender/hive_defender.py
"""

import math
import sys
from pathlib import Path

import bpy
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bake_textures as bt  # noqa: E402
import xexport  # noqa: E402
from classic_tutorials import black_world, camera, reset, sun  # noqa: E402
from dune_armour import AXLE, FORWARD, turned  # noqa: E402
from flooded_vault import material  # noqa: E402
from frost_outpost import part  # noqa: E402

OUT = bt.ROOT / "examples/dbc/demos/hive_defender"
FRAMES = 8
FRAME_SIZE = 128


# ---------------------------------------------------------------------------
# Textures

def hull_plates(g):
    edge, cell = g.cells(4.0, 6.0, 0.0)
    wear = g.noise(16.0, 6, 0.6, 301.0)
    plate, seam = bt.bevel(g, edge, 0.012, 0.03, wear)
    paint = g.ramp(cell, [(0.0, (0.70, 0.74, 0.80)), (1.0, (0.86, 0.88, 0.92))])
    stripe = g.math("LESS_THAN", g.math("ABSOLUTE", g.math("SUBTRACT", g.u, 0.5)), 0.08)
    paint = g.mix(stripe, paint, (0.85, 0.20, 0.15, 1.0))
    colour = g.mix(g.math("SUBTRACT", 1.0, seam), paint, (0.2, 0.22, 0.26, 1.0))
    g.finish(colour, plate, 0.6)


def chitin(g, base, light, offset):
    scales = g.math("POWER", g.math("SUBTRACT", 1.0, g.voronoi(8.0, "DISTANCE_TO_EDGE", offset=offset)), 6.0)
    sheen = g.noise(12.0, 5, 0.5, offset + 1.0)
    colour = g.ramp(sheen, [(0.3, base), (0.8, light)])
    colour = g.mix(g.math("MULTIPLY", scales, 0.7), colour, (0.02, 0.02, 0.03, 1.0))
    g.finish(colour, g.math("SUBTRACT", sheen, g.math("MULTIPLY", scales, 0.5)), 0.6)


def chitin_green(g):
    chitin(g, (0.08, 0.30, 0.10), (0.40, 0.85, 0.30), 303.0)


def chitin_violet(g):
    chitin(g, (0.25, 0.06, 0.32), (0.75, 0.35, 0.95), 305.0)


def chitin_amber(g):
    chitin(g, (0.40, 0.18, 0.02), (0.98, 0.70, 0.20), 307.0)


def saucer(g):
    edge, _ = g.cells(16.0, 2.0, 0.0)
    plate, seam = bt.bevel(g, edge, 0.01, 0.02, g.noise(10.0, 5, 0.5, 311.0))
    lights = g.math("LESS_THAN", g.math("ABSOLUTE", g.math("SUBTRACT", g.math("FRACT", g.math("MULTIPLY", g.u, 16.0)),
                                                                     0.5)), 0.12)
    colour = g.mix(g.math("MULTIPLY", lights, g.math("GREATER_THAN", g.v, 0.45)), (0.40, 0.42, 0.48, 1.0),
                   (1.0, 0.85, 0.30, 1.0))
    colour = g.mix(g.math("SUBTRACT", 1.0, seam), colour, (0.1, 0.1, 0.12, 1.0))
    g.finish(colour, plate, 0.5)


def nebula(g):
    cloud = g.noise(1.2, 6, 0.58, 313.0, distortion=0.3)
    dust = g.noise(6.0, 6, 0.55, 315.0)
    colour = g.ramp(cloud, [(0.50, (0.0, 0.0, 0.01)), (0.56, (0.03, 0.02, 0.10)), (0.64, (0.22, 0.07, 0.34)),
                            (0.74, (0.62, 0.26, 0.55))])
    colour = g.mix(g.math("MULTIPLY", dust, 0.25), colour, (0.04, 0.10, 0.25, 1.0))
    # Stars: the centres of small Voronoi cells, two sizes
    stars = g.math("LESS_THAN", g.voronoi(40.0, offset=317.0), 0.09)
    bright = g.math("LESS_THAN", g.voronoi(12.0, offset=319.0), 0.08)
    colour = g.mix(g.math("MAXIMUM", stars, bright), colour, (1.0, 1.0, 0.95, 1.0))
    g.finish(colour, cloud, 0.0)


def fireball(g):
    flame = g.noise(3.0, 8, 0.6, 321.0, distortion=2.0)
    g.finish(g.ramp(flame, [(0.3, (0.6, 0.08, 0.0)), (0.55, (1.0, 0.5, 0.05)), (0.8, (1.0, 0.95, 0.6))]), flame, 0.0)


def build_textures() -> None:
    bt.setup_bake_scene()
    for builder, name in ((hull_plates, "hull"), (chitin_green, "drone_green"), (chitin_violet, "drone_violet"),
                          (chitin_amber, "drone_amber"), (saucer, "saucer"), (nebula, "nebula"),
                          (fireball, "fireball")):
        bt.save_jpeg(bt.bake(builder), OUT / f"textures/{name}.jpg")


# ---------------------------------------------------------------------------
# Models (Blender: +Y is forward, +Z is up)

def build_ship():
    hull, glass, engine = material("hull"), material("canopy", (0.2, 0.7, 1.0, 1.0)), material("engine",
                                                                                                   (0.25, 0.25, 0.3, 1.0))
    body = part("cube", "body", (1.6, 5.0, 0.9), (0, 0, 0), None, 0, hull, bevel=0.3)
    part("sphere", "cockpit", (1.0, 1.8, 0.8), (0, 0.6, 0.5), body, 1, glass, smooth=True)
    for side, sign in (("l", -1), ("r", 1)):
        wing = part("cube", f"wing_{side}", (3.6, 2.2, 0.25), (sign * 2.2, -0.9, 0), body, 2 + (sign > 0), hull,
                    bevel=0.1)
        wing.rotation_euler = (0, sign * -0.12, sign * 0.25)
        turned("cylinder", f"engine_{side}", (0.6, 0.6, 1.6), (sign * 1.0, -2.6, 0), body, 4 + (sign > 0), engine,
               FORWARD)
    return body


def build_drone():
    shell, wing_skin = material("chitin"), material("wing", (0.70, 0.85, 1.0, 1.0))
    thorax = part("sphere", "thorax", (2.2, 2.0, 1.6), (0, 0, 0), None, 0, shell, smooth=True)
    part("sphere", "head", (1.4, 1.2, 1.2), (0, 1.5, 0.2), thorax, 1, shell, smooth=True)
    part("sphere", "abdomen", (1.8, 2.8, 1.5), (0, -2.2, -0.2), thorax, 2, shell, smooth=True)
    for side, sign in (("l", -1), ("r", 1)):
        # Wings hinge at the thorax: limb 3 and 4 flap with ROTATE LIMB
        part("cube", f"wing_{side}", (3.4, 1.6, 0.08), (sign * 0.6, 0.0, 0.7), thorax, 3 + (sign > 0), wing_skin,
             (sign * 1.7, -0.3, 0))
    for side, sign in (("l", -1), ("r", 1)):
        part("cone", f"mandible_{side}", (0.35, 0.35, 1.0), (sign * 0.4, 2.1, 0.0), thorax, 5 + (sign > 0), shell,
             ).rotation_euler = (-math.pi / 2, 0, 0)
    return thorax


def build_mothership():
    hull, glass = material("saucer"), material("dome", (0.5, 1.0, 0.6, 1.0))
    disc = part("sphere", "disc", (12.0, 12.0, 2.4), (0, 0, 0), None, 0, hull, smooth=True)
    part("sphere", "dome", (4.5, 4.5, 3.0), (0, 0, 0.9), disc, 1, glass, smooth=True)
    bpy.ops.mesh.primitive_torus_add(major_radius=8.0, minor_radius=0.5, major_segments=32, minor_segments=8)
    ring = bpy.context.active_object
    ring.name = "ring"
    ring.data.materials.append(hull)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.cube_project(cube_size=1.0)
    bpy.ops.object.mode_set(mode="OBJECT")
    ring.parent = disc
    ring.location = (0, 0, -0.4)
    ring.rotation_euler = (0.25, 0, 0)
    ring["limb_order"] = 2
    return disc


def build_explosions() -> None:
    """Eight frames of a fireball growing and burning out, side by side on black."""
    reset()
    black_world()
    scene = bpy.context.scene
    image = bpy.data.images.load(str(OUT / "textures/fireball.jpg"))
    strip = np.zeros((FRAME_SIZE, FRAME_SIZE * FRAMES, 4), dtype=np.float32)
    for frame in range(FRAMES):
        for obj in list(bpy.data.objects):
            bpy.data.objects.remove(obj)
        t = frame / (FRAMES - 1)
        bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=4, radius=0.6 + 1.6 * t)
        ball = bpy.context.active_object
        lumps = bpy.data.textures.new(f"lumps{frame}", "CLOUDS")
        lumps.noise_scale = 0.4
        displace = ball.modifiers.new("displace", "DISPLACE")
        displace.texture = lumps
        displace.strength = 0.4 + 0.6 * t
        glow = bpy.data.materials.new(f"glow{frame}")
        glow.use_nodes = True
        nodes, links = glow.node_tree.nodes, glow.node_tree.links
        nodes.clear()
        texture = nodes.new("ShaderNodeTexImage")
        texture.image = image
        emission = nodes.new("ShaderNodeEmission")
        # The fire burns brightest early and cools to a dim smoky red.
        emission.inputs["Strength"].default_value = 2.5 * (1.0 - t) + 0.25
        links.new(texture.outputs["Color"], emission.inputs["Color"])
        output = nodes.new("ShaderNodeOutputMaterial")
        links.new(emission.outputs[0], output.inputs["Surface"])
        ball.data.materials.append(glow)
        bpy.ops.object.mode_set(mode="EDIT")
        bpy.ops.mesh.select_all(action="SELECT")
        bpy.ops.uv.sphere_project()
        bpy.ops.object.mode_set(mode="OBJECT")
        camera((0.0, -20.0, 0.0), (math.pi / 2, 0.0, 0.0), 5.0)
        scene.render.resolution_x = scene.render.resolution_y = FRAME_SIZE
        scene.cycles.samples = 32
        scene.render.image_settings.file_format = "PNG"
        scene.render.filepath = str(OUT / "textures/_frame.png")
        bpy.ops.render.render(write_still=True)
        rendered = bpy.data.images.load(scene.render.filepath, check_existing=False)
        pixels = np.empty(FRAME_SIZE * FRAME_SIZE * 4, dtype=np.float32)
        rendered.pixels.foreach_get(pixels)
        strip[:, frame * FRAME_SIZE:(frame + 1) * FRAME_SIZE] = pixels.reshape(FRAME_SIZE, FRAME_SIZE, 4)
        bpy.data.images.remove(rendered)
    (OUT / "textures/_frame.png").unlink()
    result = bpy.data.images.new("explosions", strip.shape[1], strip.shape[0])
    result.pixels.foreach_set(np.clip(strip, 0, 1).ravel())
    scene.render.image_settings.color_mode = "RGB"
    result.save_render(str(OUT / "textures/explosions.png"), scene=scene)
    print(f"wrote {OUT / 'textures/explosions.png'}")


def main() -> None:
    build_textures()
    for builder, name, textures in ((build_ship, "ship", {"hull": "hull.jpg"}),
                                    (build_drone, "drone", {"chitin": "drone_green.jpg"}),
                                    (build_mothership, "mothership", {"saucer": "saucer.jpg"})):
        bpy.ops.wm.read_factory_settings(use_empty=True)
        xexport.export(OUT / f"models/{name}.x", [builder()], textures)
    build_explosions()


if __name__ == "__main__":
    main()
    sys.exit(0)
