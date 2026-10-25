"""Build the Frost Outpost demo media: an ice imp with six clips, the outpost,
a pulse pistol, their textures and a strip of distant-enemy impostors.

Run from the repository root:
    /Applications/Blender.app/Contents/MacOS/Blender --background --factory-startup \
        --python assets-src/blender/frost_outpost.py
"""

import math
import sys
from pathlib import Path

import bpy
import numpy as np
from mathutils import Matrix, Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bake_textures as bt  # noqa: E402
import xexport  # noqa: E402
from flooded_vault import keyframe, material  # noqa: E402

OUT = bt.ROOT / "examples/dbc/demos/frost_outpost"
IMPOSTOR_FRAMES, IMPOSTOR_SIZE = 8, 128


# ---------------------------------------------------------------------------
# Textures

def snow(g):
    drift = g.noise(3.0, 8, 0.6, 71.0)
    sparkle = g.math("GREATER_THAN", g.noise(40.0, 2, 0.5, 72.0), 0.78)
    colour = g.ramp(drift, [(0.3, (0.70, 0.76, 0.86)), (0.8, (0.95, 0.97, 1.0))])
    g.finish(g.mix(g.math("MULTIPLY", sparkle, 0.5), colour, (1.0, 1.0, 1.0, 1.0)), drift, 0.6)


def packed_snow(g):
    ridges = g.math("SINE", g.math("ADD", g.math("MULTIPLY", g.v, 60.0), g.math("MULTIPLY", g.noise(2.0, 4, 0.5, 74.0), 12.0)))
    surface = g.noise(6.0, 6, 0.55, 75.0)
    colour = g.ramp(g.math("ADD", g.math("MULTIPLY", surface, 0.7), g.math("MULTIPLY", ridges, 0.1)),
                    [(0.3, (0.62, 0.66, 0.74)), (0.8, (0.85, 0.88, 0.94))])
    g.finish(colour, g.math("ADD", surface, g.math("MULTIPLY", ridges, 0.15)), 0.7)


def blue_ice(g):
    cracks = g.math("POWER", g.math("SUBTRACT", 1.0, g.voronoi(2.5, "DISTANCE_TO_EDGE", offset=77.0)), 20.0)
    depth = g.noise(3.0, 6, 0.5, 78.0)
    colour = g.ramp(depth, [(0.2, (0.20, 0.42, 0.62)), (0.8, (0.50, 0.75, 0.90))])
    g.finish(g.mix(cracks, colour, (0.92, 0.97, 1.0, 1.0)), g.math("SUBTRACT", depth, g.math("MULTIPLY", cracks, 0.3)), 0.6)


def rocky_snow(g):
    rock = g.voronoi(3.0, "DISTANCE_TO_EDGE", offset=81.0)
    cover = g.math("GREATER_THAN", g.noise(2.5, 6, 0.6, 82.0), 0.5)
    stone = g.ramp(g.noise(6.0, 6, 0.6, 83.0), [(0.3, (0.18, 0.18, 0.20)), (0.8, (0.38, 0.38, 0.40))])
    colour = g.mix(cover, stone, (0.88, 0.92, 0.98, 1.0))
    g.finish(colour, g.math("ADD", g.math("MULTIPLY", rock, 0.7), g.math("MULTIPLY", cover, 0.4)), 1.0)


def clouds(g):
    # Drawn additively over the sky: sparse wisps on black.
    density = g.math("POWER", g.noise(2.0, 8, 0.6, 85.0), 3.5)
    g.finish(g.ramp(density, [(0.12, (0.0, 0.0, 0.0)), (0.5, (0.42, 0.45, 0.50))]), density, 0.0)


def polar(g):
    dx, dy = g.math("SUBTRACT", g.u, 0.5), g.math("SUBTRACT", g.v, 0.5)
    radius = g.math("SQRT", g.math("ADD", g.math("MULTIPLY", dx, dx), g.math("MULTIPLY", dy, dy)))
    return dx, dy, radius, g.math("ARCTAN2", dy, dx)


def compass(g):
    dx, dy, radius, angle = polar(g)
    ring = g.math("LESS_THAN", g.math("ABSOLUTE", g.math("SUBTRACT", radius, 0.42)), 0.025)
    sector = g.math("FRACT", g.math("DIVIDE", g.math("MULTIPLY", angle, 8.0), 2 * math.pi))
    tick = g.math("MULTIPLY", g.math("LESS_THAN", g.math("ABSOLUTE", g.math("SUBTRACT", sector, 0.5)), 0.06),
                  g.math("GREATER_THAN", radius, 0.30))
    tick = g.math("MULTIPLY", tick, g.math("LESS_THAN", radius, 0.45))
    north = g.math("MULTIPLY", g.math("LESS_THAN", g.math("ABSOLUTE", dx), g.math("MULTIPLY", g.math(
        "SUBTRACT", 0.48, dy), 0.35)), g.math("MULTIPLY", g.math("GREATER_THAN", dy, 0.30), g.math("LESS_THAN", dy, 0.48)))
    colour = g.mix(g.math("MAXIMUM", ring, tick), (0.0, 0.0, 0.0, 1.0), (0.75, 0.90, 1.0, 1.0))
    g.finish(g.mix(north, colour, (1.0, 0.25, 0.15, 1.0)), ring, 0.0)


def needle(g):
    dx, dy, radius, angle = polar(g)
    width = g.math("MULTIPLY", g.math("SUBTRACT", 0.40, g.math("ABSOLUTE", dy)), 0.12)
    blade = g.math("MULTIPLY", g.math("LESS_THAN", g.math("ABSOLUTE", dx), width), g.math("LESS_THAN", g.math(
        "ABSOLUTE", dy), 0.40))
    tip = g.math("GREATER_THAN", dy, 0.0)
    colour = g.mix(tip, (0.85, 0.90, 0.95, 1.0), (1.0, 0.30, 0.15, 1.0))
    g.finish(g.mix(blade, (0.0, 0.0, 0.0, 1.0), colour), blade, 0.0)


def glow(g):
    _, _, radius, _ = polar(g)
    core = g.math("POWER", g.math("SUBTRACT", 1.0, g.math("MINIMUM", g.math("MULTIPLY", radius, 2.0), 1.0)), 2.5)
    g.finish(g.ramp(core, [(0.0, (0.0, 0.0, 0.0)), (0.6, (0.3, 0.8, 1.0)), (1.0, (1.0, 1.0, 1.0))]), core, 0.0)


def imp_skin(g):
    cracks = g.math("POWER", g.math("SUBTRACT", 1.0, g.voronoi(5.0, "DISTANCE_TO_EDGE", offset=91.0)), 14.0)
    skin = g.ramp(g.noise(6.0, 6, 0.55, 92.0), [(0.3, (0.16, 0.30, 0.48)), (0.8, (0.38, 0.58, 0.78))])
    g.finish(g.mix(cracks, skin, (0.85, 0.95, 1.0, 1.0)), g.math("SUBTRACT", 1.0, cracks), 0.7)


def pistol_metal(g):
    edge, cell = g.cells(3, 3, 0.0)
    surface = g.noise(10.0, 6, 0.6, 95.0)
    height, face = bt.bevel(g, edge, 0.005, 0.02, surface, 0.2)
    metal = g.ramp(g.math("ADD", g.math("MULTIPLY", cell, 0.3), g.math("MULTIPLY", surface, 0.7)),
                   [(0.2, (0.08, 0.16, 0.18)), (0.8, (0.22, 0.42, 0.45))])
    g.finish(g.mix(face, (0.02, 0.02, 0.03, 1.0), metal), height, 1.0)


def crystal(g):
    facets = g.voronoi(1.5, "F1", "Distance", 97.0)
    colour = g.ramp(facets, [(0.0, (0.80, 0.95, 1.0)), (0.6, (0.30, 0.65, 0.90))])
    g.finish(colour, facets, 0.8)


def build_textures() -> None:
    bt.setup_bake_scene()
    tiles = [bt.bake(snow), bt.bake(packed_snow), bt.bake(blue_ice), bt.bake(rocky_snow)]
    size = bt.SIZE
    atlas = np.zeros((size * 2, size * 2, 4), dtype=np.float32)
    atlas[size:, :size], atlas[size:, size:] = tiles[0], tiles[1]
    atlas[:size, :size], atlas[:size, size:] = tiles[2], tiles[3]
    bt.save_jpeg(atlas, OUT / "terrain.jpg")
    for builder, name in ((clouds, "clouds"), (compass, "compass"), (needle, "needle"), (glow, "pulse"),
                          (imp_skin, "imp"), (pistol_metal, "pistol"), (crystal, "crystal"),
                          (bt.dark_rock, "stone")):
        bt.save_jpeg(bt.bake(builder), OUT / f"{name}.jpg")


# ---------------------------------------------------------------------------
# Modelling: every part pivots at the joint it hangs from.

def part(kind, name, size, location, parent, order, mat, offset=(0, 0, 0), bevel=0.0, smooth=False):
    if kind == "cube":
        bpy.ops.mesh.primitive_cube_add(size=1.0)
    elif kind == "sphere":
        bpy.ops.mesh.primitive_uv_sphere_add(segments=16, ring_count=8, radius=0.5)
    elif kind == "cone":
        bpy.ops.mesh.primitive_cone_add(vertices=8, radius1=0.5, depth=1.0)
    else:
        bpy.ops.mesh.primitive_cylinder_add(vertices=10, radius=0.5, depth=1.0)
    obj = bpy.context.active_object
    obj.name = name
    obj.data.transform(Matrix.Diagonal((*size, 1.0)))
    obj.data.transform(Matrix.Translation(Vector(offset)))
    if bevel:
        modifier = obj.modifiers.new("bevel", "BEVEL")
        modifier.width = bevel
        modifier.segments = 1 if smooth else 2
    if smooth:
        # Organic parts: one subdivision level rounds the boxes, smooth shading hides the facets.
        obj.modifiers.new("subdivision", "SUBSURF").levels = 1
        for polygon in obj.data.polygons:
            polygon.use_smooth = True
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.cube_project(cube_size=1.0)
    bpy.ops.object.mode_set(mode="OBJECT")
    obj.data.materials.append(mat)
    obj.parent = parent
    obj.location = location
    obj["limb_order"] = order
    return obj


def build_imp():
    skin, claw = material("imp"), material("imp_claw", (0.85, 0.93, 1.0, 1.0))
    pelvis = part("cube", "pelvis", (1.0, 0.6, 0.5), (0, 0, 1.9), None, 0, skin, bevel=0.1, smooth=True)
    torso = part("cube", "torso", (1.4, 0.9, 1.2), (0, 0, 0.2), pelvis, 1, skin, (0, 0, 0.6), 0.2, smooth=True)
    head = part("sphere", "head", (1.0, 0.9, 0.9), (0, 0.05, 1.2), torso, 2, skin, (0, 0.1, 0.4), smooth=True)
    for side, sign in (("l", -1), ("r", 1)):
        horn = part("cone", f"horn_{side}", (0.22, 0.22, 0.7), (sign * 0.3, 0.1, 0.65), head, 0, claw, (0, 0, 0.35))
        horn.rotation_euler = (0, sign * 0.5, 0)
    # Join the horns into the head so the head stays one limb.
    bpy.ops.object.select_all(action="DESELECT")
    for name in ("horn_l", "horn_r"):
        bpy.data.objects[name].select_set(True)
    head.select_set(True)
    bpy.context.view_layer.objects.active = head
    bpy.ops.object.join()
    parts = {"pelvis": pelvis, "torso": torso, "head": head}
    order = 3
    for side, sign in (("l", -1), ("r", 1)):
        arm = part("cube", f"arm_{side}", (0.32, 0.32, 0.8), (sign * 0.85, 0, 1.0), torso, order, skin, (0, 0, -0.4), 0.06, smooth=True)
        fore = part("cube", f"forearm_{side}", (0.28, 0.28, 0.75), (0, 0, -0.8), arm, order + 1, skin, (0, 0, -0.37), 0.05, smooth=True)
        hand = part("cube", f"hand_{side}", (0.4, 0.3, 0.4), (0, 0, -0.75), fore, order + 2, claw, (0, 0.05, -0.18), 0.06)
        parts.update({arm.name: arm, fore.name: fore, hand.name: hand})
        order += 3
    for side, sign in (("l", -1), ("r", 1)):
        thigh = part("cube", f"thigh_{side}", (0.36, 0.36, 0.8), (sign * 0.3, 0, -0.2), pelvis, order, skin, (0, 0, -0.4), 0.06, smooth=True)
        shin = part("cube", f"shin_{side}", (0.3, 0.3, 0.8), (0, 0, -0.8), thigh, order + 1, skin, (0, 0, -0.4), 0.05, smooth=True)
        foot = part("cube", f"foot_{side}", (0.38, 0.6, 0.2), (0, 0, -0.8), shin, order + 2, claw, (0, 0.15, -0.1), 0.05)
        parts.update({thigh.name: thigh, shin.name: shin, foot.name: foot})
        order += 3
    parts["_rest"] = {name: obj.location.copy() for name, obj in parts.items() if not name.startswith("_")}
    parts["_rest_rotation"] = {name: obj.rotation_euler.copy() for name, obj in parts.items()
                               if not name.startswith("_")}
    return pelvis, parts


def pose(parts, rest, frame, pelvis_drop=0.0, pelvis_pitch=0.0, torso=0.0, arms=(0.0, 0.0),
         forearms=(0.0, 0.0), legs=(0.0, 0.0), shins=(0.0, 0.0), twist=0.0):
    keyframe(parts["pelvis"], frame, rotation=(pelvis_pitch, 0, 0),
             location=rest["pelvis"] + Vector((0, 0, -pelvis_drop)))
    keyframe(parts["torso"], frame, rotation=(torso, 0, twist))
    for index, side in enumerate(("l", "r")):
        keyframe(parts[f"arm_{side}"], frame, rotation=(arms[index], 0, 0))
        keyframe(parts[f"forearm_{side}"], frame, rotation=(forearms[index], 0, 0))
        keyframe(parts[f"thigh_{side}"], frame, rotation=(legs[index], 0, 0))
        keyframe(parts[f"shin_{side}"], frame, rotation=(shins[index], 0, 0))


CLIPS = {"idle": 20, "walk": 24, "charge": 16, "punch": 24, "die_a": 30, "die_b": 30}


def animate(parts, clip):
    # Every clip starts from the modelled rest pose, never from the previous clip's last pose.
    rest = parts["_rest"]
    for name, obj in parts.items():
        if name.startswith("_"):
            continue
        obj.animation_data_clear()
        obj.location = rest[name]
        obj.rotation_euler = parts["_rest_rotation"][name]
    length = CLIPS[clip]
    for frame in range(length + 1):
        t = frame / length
        s = math.sin(2 * math.pi * t)
        if clip == "idle":
            pose(parts, rest, frame, torso=0.05 * s, arms=(0.08 * s, -0.08 * s), forearms=(0.2, 0.2))
        elif clip in ("walk", "charge"):
            stride = 0.55 if clip == "walk" else 0.85
            pose(parts, rest, frame, pelvis_drop=0.08 * abs(s), torso=0.1 if clip == "walk" else 0.35,
                 arms=(-stride * s, stride * s), forearms=(0.4, 0.4), legs=(stride * s, -stride * s),
                 shins=(-0.5 * max(0.0, -s), -0.5 * max(0.0, s)))
        elif clip == "punch":
            swing = math.sin(math.pi * min(1.0, t * 1.6))
            pose(parts, rest, frame, torso=0.2 * swing, arms=(0.2, 1.7 * swing), forearms=(0.3, 0.2),
                 twist=-0.4 * swing)
        else:
            fall = min(1.0, t * 1.4)
            direction = -1 if clip == "die_a" else 1
            pose(parts, rest, frame, pelvis_drop=1.5 * fall, pelvis_pitch=direction * 1.5 * fall,
                 arms=(direction * 1.2 * fall, direction * 0.9 * fall), legs=(-direction * 0.3 * fall, 0.2 * fall))
    for name, obj in parts.items():
        if not name.startswith("_") and obj.animation_data is None:
            keyframe(obj, 0, rotation=obj.rotation_euler, location=obj.location)
            keyframe(obj, length, rotation=obj.rotation_euler, location=obj.location)


def build_pistol():
    metal, core = material("pistol"), material("pistol_core", (0.3, 0.9, 1.0, 1.0))
    body = part("cube", "body", (0.35, 1.2, 0.45), (0, 0, 0), None, 0, metal, bevel=0.06)
    part("cylinder", "barrel", (0.22, 0.22, 1.1), (0, 0.9, 0.05), body, 1, metal, bevel=0.0).rotation_euler = (
        math.pi / 2, 0, 0)
    part("cube", "breech", (0.3, 0.4, 0.3), (0, -0.55, 0.15), body, 2, core, bevel=0.04)
    grip = part("cube", "grip", (0.28, 0.35, 0.8), (0, -0.25, -0.2), body, 3, metal, (0, 0, -0.4), 0.05)
    grip.rotation_euler = (-0.3, 0, 0)
    part("cube", "fin_l", (0.05, 0.6, 0.3), (-0.2, 0.2, 0.25), body, 4, core, bevel=0.0)
    part("cube", "fin_r", (0.05, 0.6, 0.3), (0.2, 0.2, 0.25), body, 5, core, bevel=0.0)
    return body


def build_outpost():
    stone, ice = material("stone"), material("crystal")
    gate = part("cube", "gate_left", (2.0, 2.0, 12.0), (-5, 0, 0), None, 0, stone, (0, 0, 6), 0.2)
    part("cube", "gate_right", (2.0, 2.0, 12.0), (10, 0, 0), gate, 1, stone, (0, 0, 6), 0.2)
    part("cube", "lintel", (14.0, 2.4, 2.0), (5, 0, 12), gate, 2, stone, (0, 0, 1), 0.2)
    for index in range(4):
        angle = math.pi / 4 + index * math.pi / 2
        part("cone", f"pylon_{index + 1}", (2.5, 2.5, 9.0), (5 + 22 * math.cos(angle), 22 * math.sin(angle), 0),
             gate, 3 + index, ice, (0, 0, 4.5))
    return gate


def render_impostors(parts) -> None:
    """Render the walking imp from the front into a strip of eight frames on black."""
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.samples = 16
    scene.render.resolution_x = scene.render.resolution_y = IMPOSTOR_SIZE
    scene.render.film_transparent = False
    world = bpy.data.worlds.new("black")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs["Color"].default_value = (0, 0, 0, 1)
    scene.world = world
    camera = bpy.data.objects.new("camera", bpy.data.cameras.new("camera"))
    camera.data.type = "ORTHO"
    camera.data.ortho_scale = 4.6
    camera.location = (0, 12, 2.2)
    camera.rotation_euler = (math.pi / 2, 0, math.pi)
    scene.collection.objects.link(camera)
    scene.camera = camera
    sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN"))
    sun.data.energy = 4.0
    sun.rotation_euler = (-0.64, 0.0, 0.35)  # from the camera side, slightly from above
    scene.collection.objects.link(sun)
    image_path = OUT / "imp.jpg"
    for mat_name in ("imp", "imp_claw"):
        mat = bpy.data.materials[mat_name]
        mat.use_nodes = True
        nodes = mat.node_tree.nodes
        shader = next((node for node in nodes if node.type == "BSDF_PRINCIPLED"), None)
        if mat_name == "imp" and shader:
            texture = nodes.new("ShaderNodeTexImage")
            texture.image = bpy.data.images.load(str(image_path))
            mat.node_tree.links.new(texture.outputs["Color"], shader.inputs["Base Color"])
        elif shader:
            shader.inputs["Base Color"].default_value = mat.diffuse_color
    animate(parts, "walk")
    strip = np.zeros((IMPOSTOR_SIZE, IMPOSTOR_SIZE * IMPOSTOR_FRAMES, 4), dtype=np.float32)
    for index in range(IMPOSTOR_FRAMES):
        scene.frame_set(index * CLIPS["walk"] // IMPOSTOR_FRAMES)
        scene.render.filepath = str(OUT / "_frame.png")
        bpy.ops.render.render(write_still=True)
        frame = bpy.data.images.load(scene.render.filepath, check_existing=False)
        pixels = np.empty(IMPOSTOR_SIZE * IMPOSTOR_SIZE * 4, dtype=np.float32)
        frame.pixels.foreach_get(pixels)
        strip[:, index * IMPOSTOR_SIZE:(index + 1) * IMPOSTOR_SIZE] = pixels.reshape(IMPOSTOR_SIZE, IMPOSTOR_SIZE, 4)
        bpy.data.images.remove(frame)
    (OUT / "_frame.png").unlink()
    # The rendered frames are already display-encoded; store them without a second sRGB encode.
    image = bpy.data.images.new("impostors", strip.shape[1], strip.shape[0])
    image.pixels.foreach_set(np.clip(strip, 0, 1).ravel())
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGB"
    image.save_render(str(OUT / "impostors.png"), scene=scene)
    print(f"wrote {OUT / 'impostors.png'}")


def main() -> None:
    build_textures()
    bpy.ops.wm.read_factory_settings(use_empty=True)
    pelvis, parts = build_imp()
    textures = {"imp": "imp.jpg"}
    for clip, length in CLIPS.items():
        animate(parts, clip)
        xexport.export(OUT / f"imp_{clip}.x", [pelvis], textures, (0, length))
    render_impostors(parts)

    bpy.ops.wm.read_factory_settings(use_empty=True)
    xexport.export(OUT / "pistol.x", [build_pistol()], {"pistol": "pistol.jpg"})
    bpy.ops.wm.read_factory_settings(use_empty=True)
    xexport.export(OUT / "outpost.x", [build_outpost()], {"stone": "stone.jpg", "crystal": "crystal.jpg"})


if __name__ == "__main__":
    main()
    sys.exit(0)
