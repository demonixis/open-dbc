"""Render the pictures used by the Classic 2D tutorials: a harbour scene for the
bitmap tutorials, then top-down sprites on black (DarkBASIC's transparent colour)
and a sea background for the sprite tutorials. Models and textures come from the
Wave Racer demo. Classic loads BMP files, so every picture is saved as a 24-bit BMP.

Run from the repository root:
    /Applications/Blender.app/Contents/MacOS/Blender --background --factory-startup \
        --python assets-src/blender/classic_tutorials.py
"""

import math
import sys
from pathlib import Path

import bpy
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bake_textures as bt  # noqa: E402
from frost_outpost import part  # noqa: E402
from wave_racer import build_buoy, build_lighthouse, build_rider, build_scooter  # noqa: E402

WAVE = bt.ROOT / "examples/dbc/demos/wave_racer"
TUTORIALS = bt.ROOT / "examples/dbc/tutorials"


def reset() -> None:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.use_denoising = True
    scene.view_settings.view_transform = "Standard"
    scene.render.film_transparent = False
    scene.render.image_settings.file_format = "BMP"
    scene.render.image_settings.color_mode = "RGB"


def textured(name: str, image: str, scale: float = 1.0) -> None:
    """Feed a Wave Racer texture into a material created by the model builders."""
    mat = bpy.data.materials[name]
    mat.use_nodes = True
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    shader = next(node for node in nodes if node.type == "BSDF_PRINCIPLED")
    texture = nodes.new("ShaderNodeTexImage")
    texture.image = bpy.data.images.load(str(WAVE / image), check_existing=True)
    if scale != 1.0:
        mapping = nodes.new("ShaderNodeMapping")
        mapping.inputs["Scale"].default_value = (scale, scale, 1.0)
        coords = nodes.new("ShaderNodeTexCoord")
        links.new(coords.outputs["UV"], mapping.inputs["Vector"])
        links.new(mapping.outputs["Vector"], texture.inputs["Vector"])
    links.new(texture.outputs["Color"], shader.inputs["Base Color"])


def plain_colours() -> None:
    """Materials without a texture render with their viewport colour."""
    for mat in bpy.data.materials:
        mat.use_nodes = True
        shader = next((node for node in mat.node_tree.nodes if node.type == "BSDF_PRINCIPLED"), None)
        if shader and not any(link.to_socket == shader.inputs["Base Color"] for link in mat.node_tree.links):
            shader.inputs["Base Color"].default_value = mat.diffuse_color


def black_world() -> None:
    world = bpy.data.worlds.new("black")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs["Color"].default_value = (0, 0, 0, 1)
    bpy.context.scene.world = world


def sun(energy: float, rotation, colour=(1.0, 1.0, 1.0)) -> None:
    light = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN"))
    light.data.energy = energy
    light.data.color = colour
    light.rotation_euler = rotation
    bpy.context.scene.collection.objects.link(light)


def camera(location, rotation, ortho_scale=None, lens=35.0) -> None:
    cam = bpy.data.objects.new("camera", bpy.data.cameras.new("camera"))
    if ortho_scale:
        cam.data.type = "ORTHO"
        cam.data.ortho_scale = ortho_scale
    else:
        cam.data.lens = lens
    cam.location = location
    cam.rotation_euler = rotation
    bpy.context.scene.collection.objects.link(cam)
    bpy.context.scene.camera = cam


def sea_plane(size: float, tiles: float, roughness: float = 0.15):
    bpy.ops.mesh.primitive_plane_add(size=size)
    plane = bpy.context.active_object
    mat = bpy.data.materials.new("sea")
    plane.data.materials.append(mat)
    textured("sea", "sea.jpg", tiles)
    shader = next(node for node in mat.node_tree.nodes if node.type == "BSDF_PRINCIPLED")
    shader.inputs["Roughness"].default_value = roughness
    return plane


def render(path: Path, width: int, height: int, samples: int) -> None:
    scene = bpy.context.scene
    scene.render.resolution_x, scene.render.resolution_y = width, height
    scene.cycles.samples = samples
    scene.render.filepath = str(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.render.render(write_still=True)
    print(f"wrote {path}")


def harbour() -> None:
    """An evening view across the water to the lighthouse, for the bitmap tutorials."""
    reset()
    scene = bpy.context.scene
    world = bpy.data.worlds.new("sky")
    world.use_nodes = True
    sky = world.node_tree.nodes.new("ShaderNodeTexSky")
    sky.sun_elevation = math.radians(9.0)
    sky.sun_rotation = math.radians(200.0)
    world.node_tree.links.new(sky.outputs["Color"], world.node_tree.nodes["Background"].inputs["Color"])
    world.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.2
    scene.world = world
    scene.view_settings.exposure = -1.5
    sea_plane(4000.0, 160.0, 0.45)
    lighthouse = build_lighthouse()
    lighthouse.location = (35.0, 150.0, 0.0)
    # The demo's island is a low-poly dome; subdivide and roughen it so it reads as rock up close.
    lighthouse.modifiers.new("subdivision", "SUBSURF").levels = 3
    lumps = bpy.data.textures.new("island", "CLOUDS")
    lumps.noise_scale = 7.0
    displace = lighthouse.modifiers.new("displace", "DISPLACE")
    displace.texture = lumps
    displace.strength = 6.0
    displace.mid_level = 0.6
    scooter = build_scooter()
    rider = build_rider()
    for model in (scooter, rider):
        model.location = (-4.0, 30.0, 0.5)
        model.rotation_euler = (0.0, 0.0, math.radians(-75.0))
    for index, (x, y) in enumerate(((-40.0, 90.0), (30.0, 70.0), (-90.0, 160.0))):
        buoy = build_buoy()
        buoy.location = (x, y, 0.0)
        buoy.scale = (2.0, 2.0, 2.0)
    textured("rock", "rock.jpg")
    textured("bands", "bands.jpg")
    textured("livery", "livery.jpg")
    textured("suit", "suit.jpg")
    plain_colours()
    sun(2.5, (math.radians(70.0), 0.0, math.radians(150.0)), (1.0, 0.8, 0.6))
    camera((0.0, 0.0, 14.0), (math.radians(84.0), 0.0, 0.0), lens=32.0)
    render(TUTORIALS / "bitmap/harbour.bmp", 640, 480, 96)


def sprite(builders, textures, path: Path, size: int, ortho: float, tilt: float = 0.0,
           light=(math.radians(30.0), 0.0, math.radians(-30.0))) -> None:
    """Render models from above. DarkBASIC draws every pure black pixel of a sprite as transparent,
    so the background is made exactly black and the model keeps no near-black fringe."""
    reset()
    black_world()
    for builder in builders:
        builder()
    for material_name, image in textures.items():
        textured(material_name, image)
    plain_colours()
    sun(4.0, light)
    distance = 20.0
    camera((0.0, -math.sin(tilt) * distance, math.cos(tilt) * distance), (tilt, 0.0, 0.0), ortho)
    scene = bpy.context.scene
    scene.render.film_transparent = True
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    frame = path.with_suffix(".png")
    render(frame, size, size, 64)
    rendered = bpy.data.images.load(str(frame), check_existing=False)
    pixels = np.empty(size * size * 4, dtype=np.float32)
    rendered.pixels.foreach_get(pixels)
    bpy.data.images.remove(rendered)
    frame.unlink()
    pixels = pixels.reshape(size, size, 4)
    opaque = pixels[:, :, 3] >= 0.5
    colour = np.where(opaque[:, :, None], np.maximum(pixels[:, :, :3], 2.0 / 255.0), 0.0)
    keyed = np.concatenate([colour, np.ones((size, size, 1), dtype=np.float32)], axis=2)
    # The PNG was already display-encoded; store the keyed pixels as they are.
    image = bpy.data.images.new("sprite", size, size)
    image.pixels.foreach_set(keyed.ravel())
    scene.render.image_settings.file_format = "BMP"
    scene.render.image_settings.color_mode = "RGB"
    image.save_render(str(path), scene=scene)
    print(f"wrote {path}")


def rock():
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=4, radius=1.0)
    obj = bpy.context.active_object
    texture = bpy.data.textures.new("lumps", "CLOUDS")
    texture.noise_scale = 0.6
    displace = obj.modifiers.new("displace", "DISPLACE")
    displace.texture = texture
    displace.strength = 0.45
    obj.scale = (1.0, 0.85, 0.7)
    bpy.ops.object.shade_smooth()
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.sphere_project()
    bpy.ops.object.mode_set(mode="OBJECT")
    obj.data.materials.append(bpy.data.materials.new("rock"))
    return obj


def sea_background() -> None:
    reset()
    black_world()
    # One texture tile is exactly the 24 units the camera sees vertically, so the picture
    # wraps from bottom to top when a program scrolls it.
    sea_plane(64.0, 64.0 / 24.0, 0.45)
    sun(3.0, (math.radians(35.0), 0.0, math.radians(-30.0)))
    camera((0.0, -32.0 + 24.0 + 12.0, 20.0), (0.0, 0.0, 0.0), 32.0)
    render(TUTORIALS / "sprite/sea.bmp", 640, 480, 32)


def main() -> None:
    if "--sprites-only" not in sys.argv:
        harbour()
    sprite((build_scooter, build_rider), {"livery": "livery.jpg", "suit": "suit.jpg"},
           TUTORIALS / "sprite/scooter.bmp", 64, 5.2)
    sprite((build_buoy,), {"bands": "bands.jpg"}, TUTORIALS / "sprite/buoy.bmp", 32, 4.4, math.radians(40.0))
    sprite((rock,), {"rock": "rock.jpg"}, TUTORIALS / "sprite/rock.bmp", 48, 2.9)
    sea_background()


if __name__ == "__main__":
    main()
    sys.exit(0)
