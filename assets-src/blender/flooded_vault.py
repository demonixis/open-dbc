"""Build the Flooded Vault demo media: a cistern room, a mechanical crab and their textures.

Run from the repository root:
    /Applications/Blender.app/Contents/MacOS/Blender --background --factory-startup \
        --python assets-src/blender/flooded_vault.py

Everything is modelled from Blender primitives and procedural materials. The
models are written as DirectX .x text by xexport.py (one rigid mesh per limb),
the textures are baked to 1024 x 1024 JPEGs by bake_textures.py.
"""

import math
import sys
from pathlib import Path

import bmesh
import bpy

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bake_textures as bt  # noqa: E402
import xexport  # noqa: E402

OUT = bt.ROOT / "examples/dbc/demos/flooded_vault"


# ---------------------------------------------------------------------------
# Textures

def water(g):
    caustic = g.math("POWER", g.math("SUBTRACT", 1.0, g.voronoi(3.0, "DISTANCE_TO_EDGE", offset=41.0)), 10.0)
    depth = g.noise(2.0, 4, 0.5, 43.0)
    # The water is drawn additively over the floor, so it stays dark apart from the caustics.
    base = g.ramp(depth, [(0.2, (0.0, 0.015, 0.025)), (0.8, (0.01, 0.05, 0.07))])
    g.finish(g.mix(g.math("MULTIPLY", caustic, 0.6), base, (0.10, 0.28, 0.30, 1.0)), depth, 0.3)


def ripple(g):
    # A bright ring on black: shown additively on a ghosted plain.
    dx = g.math("SUBTRACT", g.u, 0.5)
    dy = g.math("SUBTRACT", g.v, 0.5)
    radius = g.math("SQRT", g.math("ADD", g.math("MULTIPLY", dx, dx), g.math("MULTIPLY", dy, dy)))
    ring = g.math("POWER", g.math("SUBTRACT", 1.0, g.math("MINIMUM", g.math(
        "MULTIPLY", g.math("ABSOLUTE", g.math("SUBTRACT", radius, 0.38)), 12.0), 1.0)), 2.0)
    g.finish(g.mix(ring, (0.0, 0.0, 0.0, 1.0), (0.55, 0.95, 1.0, 1.0)), ring, 0.0)


def crab_plates(g):
    edge, cell = g.cells(4, 4, 0.5)
    surface = g.noise(8.0, 6, 0.6, 51.0)
    height, face = bt.bevel(g, edge, 0.004, 0.02, surface, 0.2)
    wear = g.math("GREATER_THAN", g.noise(4.0, 6, 0.65, 53.0), 0.62)
    paint = g.ramp(g.math("ADD", g.math("MULTIPLY", cell, 0.4), g.math("MULTIPLY", surface, 0.6)),
                   [(0.2, (0.55, 0.20, 0.05)), (0.8, (0.85, 0.40, 0.10))])
    bare = g.ramp(surface, [(0.2, (0.25, 0.25, 0.27)), (0.8, (0.55, 0.56, 0.58))])
    rivet = g.math("LESS_THAN", g.voronoi(16.0, "F1", "Distance", 55.0), 0.08)
    colour = g.mix(wear, paint, bare)
    colour = g.mix(rivet, colour, (0.7, 0.7, 0.72, 1.0))
    colour = g.mix(face, (0.05, 0.05, 0.05, 1.0), colour)
    g.finish(colour, g.math("ADD", height, g.math("MULTIPLY", rivet, 0.3)), 1.0)


def door(g):
    edge, cell = g.cells(5, 1, 0.0)
    gap = g.math("GREATER_THAN", edge, 0.008)
    grain = g.noise(1.5, 3, 0.45, 61.0, distortion=0.3)
    stripes = g.math("SINE", g.math("ADD", g.math("MULTIPLY", g.u, 130.0), g.math("MULTIPLY", grain, 5.0)))
    wood = g.ramp(g.math("ADD", g.math("MULTIPLY", stripes, 0.15), g.math("ADD", 0.35, g.math("MULTIPLY", cell, 0.3))),
                  [(0.2, (0.14, 0.08, 0.04)), (0.8, (0.40, 0.25, 0.12))])
    band_y = g.math("FRACT", g.math("MULTIPLY", g.v, 3.0))
    band = g.math("LESS_THAN", g.math("ABSOLUTE", g.math("SUBTRACT", band_y, 0.5)), 0.07)
    iron = g.ramp(g.noise(10.0, 6, 0.6, 63.0), [(0.3, (0.10, 0.10, 0.11)), (0.8, (0.30, 0.28, 0.27))])
    colour = g.mix(band, g.mix(gap, (0.03, 0.02, 0.01, 1.0), wood), iron)
    g.finish(colour, g.math("ADD", g.math("MULTIPLY", gap, 0.8), g.math("MULTIPLY", band, 0.2)), 0.6)


def grate(g):
    bars_x = g.math("LESS_THAN", g.math("ABSOLUTE", g.math("SUBTRACT", g.math("FRACT", g.math("MULTIPLY", g.u, 8.0)), 0.5)), 0.08)
    bars_y = g.math("LESS_THAN", g.math("ABSOLUTE", g.math("SUBTRACT", g.math("FRACT", g.math("MULTIPLY", g.v, 8.0)), 0.5)), 0.06)
    bars = g.math("MAXIMUM", bars_x, bars_y)
    metal = g.ramp(g.noise(12.0, 6, 0.6, 67.0), [(0.3, (0.20, 0.30, 0.28)), (0.8, (0.45, 0.60, 0.55))])
    g.finish(g.mix(bars, (0.0, 0.0, 0.0, 1.0), metal), bars, 0.5)


def build_textures() -> None:
    bt.setup_bake_scene()
    for builder, name in ((bt.mossy_blocks, "wall"), (bt.flagstones, "floor"), (bt.dark_rock, "vault"),
                          (water, "water"), (ripple, "ripple"), (crab_plates, "crab"), (door, "door"),
                          (grate, "grate")):
        bt.save_jpeg(bt.bake(builder), OUT / f"{name}.jpg")


# ---------------------------------------------------------------------------
# Modelling helpers

def material(name, colour=(0.8, 0.8, 0.8, 1.0)):
    created = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    created.diffuse_color = colour
    return created


def mesh_object(name, build, mat, parent=None, order=0):
    mesh = bpy.data.meshes.new(name)
    bm = bmesh.new()
    uv = bm.loops.layers.uv.new("UVMap")
    build(bm, uv)
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    obj.data.materials.append(mat)
    obj["limb_order"] = order
    if parent:
        obj.parent = parent
    return obj


def quad(bm, uv, corners, uvs):
    face = bm.faces.new([bm.verts.new(c) for c in corners])
    for loop, coordinate in zip(face.loops, uvs):
        loop[uv].uv = coordinate
    return face


SIDES, RADIUS, HEIGHT = 8, 30.0, 18.0


def octagon_point(index, radius=RADIUS):
    angle = 2 * math.pi * (index + 0.5) / SIDES
    return math.cos(angle) * radius, math.sin(angle) * radius


def wall_builder(index):
    def build(bm, uv):
        x0, y0 = octagon_point(index)
        x1, y1 = octagon_point(index + 1)
        length = math.dist((x0, y0), (x1, y1))
        u = length / 12.0
        # Inner face only: the room is seen from inside.
        quad(bm, uv, [(x0, y0, 0), (x1, y1, 0), (x1, y1, HEIGHT), (x0, y0, HEIGHT)],
             [(0, 0), (u, 0), (u, HEIGHT / 12.0), (0, HEIGHT / 12.0)])
        bm.normal_update()
        for face in bm.faces:
            face.normal_flip() if face.normal.dot((-(x0 + x1), -(y0 + y1), 0)) < 0 else None
    return build


def floor_builder(height, facing_up, scale):
    def build(bm, uv):
        centre = bm.verts.new((0, 0, height))
        ring = [bm.verts.new((*octagon_point(i), height)) for i in range(SIDES)]
        for i in range(SIDES):
            a, b = ring[i], ring[(i + 1) % SIDES]
            verts = [centre, a, b] if facing_up else [centre, b, a]
            face = bm.faces.new(verts)
            for loop in face.loops:
                loop[uv].uv = (loop.vert.co.x / scale, loop.vert.co.y / scale)
    return build


def dome_builder(bm, uv):
    # A low octagonal vault seen from below, rising from the wall tops to a central oculus.
    rings = [(RADIUS, HEIGHT), (RADIUS * 0.7, HEIGHT + 6), (RADIUS * 0.35, HEIGHT + 9), (4.0, HEIGHT + 10)]
    loops = [[bm.verts.new((*octagon_point(i, r), z)) for i in range(SIDES)] for r, z in rings]
    for level in range(len(loops) - 1):
        for i in range(SIDES):
            a, b = loops[level][i], loops[level][(i + 1) % SIDES]
            c, d = loops[level + 1][(i + 1) % SIDES], loops[level + 1][i]
            face = bm.faces.new([a, d, c, b])  # facing inwards
            for loop in face.loops:
                loop[uv].uv = (loop.vert.co.x / 16.0, loop.vert.co.y / 16.0)


def door_builder(bm, uv):
    # The door sits in front of wall 0, slightly inside the room.
    x0, y0 = octagon_point(0, RADIUS - 0.3)
    x1, y1 = octagon_point(1, RADIUS - 0.3)
    mx, my = (x0 + x1) / 2, (y0 + y1) / 2
    dx, dy = (x1 - x0) / math.dist((x0, y0), (x1, y1)), (y1 - y0) / math.dist((x0, y0), (x1, y1))
    half = 4.0
    a, b = (mx - dx * half, my - dy * half), (mx + dx * half, my + dy * half)
    quad(bm, uv, [(*a, 0), (*b, 0), (*b, 11), (*a, 11)], [(0, 0), (1, 0), (1, 1), (0, 1)])
    bm.normal_update()
    for face in bm.faces:
        if face.normal.dot((-mx, -my, 0)) < 0:
            face.normal_flip()


def grate_builder(bm, uv):
    # A square grate in the floor where the water drains.
    size, z = 5.0, 0.05
    quad(bm, uv, [(-size, -size, z), (size, -size, z), (size, size, z), (-size, size, z)],
         [(0, 0), (1, 0), (1, 1), (0, 1)])


def build_room():
    stone, ground, vault = material("wall"), material("floor"), material("vault")
    wood, bars = material("door"), material("grate")
    root = mesh_object("vault", dome_builder, vault, order=0)
    mesh_object("floor", floor_builder(0.0, True, 12.0), ground, root, 1)
    for index in range(SIDES):
        mesh_object(f"wall_{index + 1}", wall_builder(index), stone, root, 2 + index)
    mesh_object("door", door_builder, wood, root, 10)
    mesh_object("grate", grate_builder, bars, root, 11)
    textures = {"wall": "wall.jpg", "floor": "floor.jpg", "vault": "vault.jpg", "door": "door.jpg",
                "grate": "grate.jpg"}
    xexport.export(OUT / "room.x", [root], textures)


# ---------------------------------------------------------------------------
# The mechanical crab: 21 rigid parts parented into one hierarchy.

def primitive(kind, name, location, scale, parent, order, mat, bevel=0.0, rotation=(0, 0, 0)):
    if kind == "cube":
        bpy.ops.mesh.primitive_cube_add(size=1.0)
    elif kind == "sphere":
        bpy.ops.mesh.primitive_uv_sphere_add(segments=12, ring_count=6, radius=0.5)
    else:
        bpy.ops.mesh.primitive_cylinder_add(vertices=8, radius=0.5, depth=1.0)
    obj = bpy.context.active_object
    obj.name = name
    obj.scale = scale
    obj.rotation_euler = rotation
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    if bevel:
        modifier = obj.modifiers.new("bevel", "BEVEL")
        modifier.width = bevel
        modifier.segments = 2
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.cube_project(cube_size=2.0)
    bpy.ops.object.mode_set(mode="OBJECT")
    obj.data.materials.append(mat)
    if parent:
        obj.parent = parent
    obj.location = location
    obj["limb_order"] = order
    return obj


def build_crab():
    hull, joint = material("crab"), material("crab_joint", (0.08, 0.08, 0.09, 1.0))
    eye = material("crab_eye", (1.0, 0.25, 0.1, 1.0))
    body = primitive("cube", "chassis", (0, 0, 1.6), (2.6, 2.0, 0.7), None, 0, hull, 0.2)
    shell = primitive("sphere", "carapace", (0, 0, 0.35), (3.0, 2.4, 1.0), body, 1, hull)
    eyes = primitive("cylinder", "eyes", (0, 1.05, 0.55), (0.18, 0.18, 0.9), shell, 2, eye,
                     rotation=(0, math.pi / 2, 0))
    parts = {"chassis": body, "carapace": shell, "eyes": eyes}
    order = 3
    for side, sign in (("l", -1), ("r", 1)):
        arm = primitive("cube", f"arm_{side}", (sign * 1.2, 1.0, 0.0), (0.35, 1.2, 0.35), body, order, joint, 0.08)
        fore = primitive("cube", f"forearm_{side}", (0, 1.1, 0.0), (0.45, 1.0, 0.45), arm, order + 1, hull, 0.1)
        claw = primitive("cube", f"claw_{side}", (0, 0.9, 0.0), (0.7, 0.9, 0.25), fore, order + 2, hull, 0.12)
        parts.update({arm.name: arm, fore.name: fore, claw.name: claw})
        order += 3
    for index in range(6):
        sign = -1 if index < 3 else 1
        y = (index % 3 - 1) * 0.7
        upper = primitive("cube", f"leg_{index + 1}", (sign * 1.3, y, -0.1), (1.3, 0.22, 0.22), body, order,
                          joint, 0.05)
        lower = primitive("cube", f"foot_{index + 1}", (sign * 0.75, 0, -0.6), (0.2, 0.2, 1.2), upper, order + 1,
                          hull, 0.06)
        parts.update({upper.name: upper, lower.name: lower})
        order += 2
    return body, parts


def keyframe(obj, frame, rotation=None, location=None):
    if rotation is not None:
        obj.rotation_euler = rotation
        obj.keyframe_insert("rotation_euler", frame=frame)
    if location is not None:
        obj.location = location
        obj.keyframe_insert("location", frame=frame)


def animate(parts, walking: bool, length: int):
    for obj in parts.values():
        obj.animation_data_clear()
    rest = {name: obj.location.copy() for name, obj in parts.items()}
    for frame in range(0, length + 1):
        phase = 2 * math.pi * frame / length
        bob = 0.08 * abs(math.sin(phase)) if walking else 0.04 * math.sin(phase)
        keyframe(parts["chassis"], frame, location=rest["chassis"] + type(rest["chassis"])((0, 0, bob)))
        for index in range(6):
            leg = parts[f"leg_{index + 1}"]
            sign = -1 if index < 3 else 1
            offset = math.pi * (index % 2)
            if walking:
                lift = 0.35 * max(0.0, math.sin(phase + offset))
                swing = 0.35 * math.cos(phase + offset)
            else:
                lift, swing = 0.03 * math.sin(phase + index), 0.0
            keyframe(leg, frame, rotation=(0, sign * lift, swing))
        for side, sign in (("l", -1), ("r", 1)):
            snap = 0.25 * max(0.0, math.sin(phase * (1 if walking else 2)))
            keyframe(parts[f"claw_{side}"], frame, rotation=(0, 0, sign * snap))
            keyframe(parts[f"arm_{side}"], frame, rotation=(0.1 * math.sin(phase), 0, sign * 0.3))
    for name, obj in parts.items():
        if obj.animation_data is None:
            keyframe(obj, 0, rotation=obj.rotation_euler, location=obj.location)
            keyframe(obj, length, rotation=obj.rotation_euler, location=obj.location)


def main() -> None:
    build_textures()
    bpy.ops.wm.read_factory_settings(use_empty=True)
    build_room()
    body, parts = build_crab()
    textures = {"crab": "crab.jpg"}
    animate(parts, False, 20)
    xexport.export(OUT / "crab_idle.x", [body], textures, (0, 20))
    animate(parts, True, 24)
    xexport.export(OUT / "crab_walk.x", [body], textures, (0, 24))


if __name__ == "__main__":
    main()
    sys.exit(0)
