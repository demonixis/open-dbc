"""Bake the OpenDBC example textures from procedural Blender materials.

Run from the repository root:
    /Applications/Blender.app/Contents/MacOS/Blender --background --factory-startup \
        --python assets-src/blender/bake_textures.py

Every material is built from Blender procedural nodes only (no image input).
Noise is sampled on a 4D torus so each texture tiles seamlessly, a bump-mapped
normal is lit by a fixed light, and the result is baked as emission at 1024 x
1024 before being written as a high-quality JPEG. The outputs are recorded in
examples/ASSETS.tsv by scripts/audit_example_assets.py --refresh.
"""

import math
import sys
from pathlib import Path

import bpy
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
SIZE = 1024
LIGHT = (0.45, 0.55, 0.70)
JPEG_QUALITY = 92


class Graph:
    """Small helper over a material node tree."""

    def __init__(self, material):
        self.nodes = material.node_tree.nodes
        self.links = material.node_tree.links
        self.nodes.clear()
        coordinates = self.node("ShaderNodeTexCoord")
        uv = self.node("ShaderNodeSeparateXYZ")
        self.link(coordinates.outputs["UV"], uv.inputs[0])
        self.u, self.v = uv.outputs["X"], uv.outputs["Y"]

    def node(self, kind, **settings):
        created = self.nodes.new(kind)
        for name, value in settings.items():
            setattr(created, name, value)
        return created

    def link(self, source, target):
        self.links.new(source, target)

    def math(self, operation, a, b=None, clamp=False):
        node = self.node("ShaderNodeMath", operation=operation, use_clamp=clamp)
        for index, value in enumerate((a, b)):
            if value is None:
                continue
            if isinstance(value, (int, float)):
                node.inputs[index].default_value = value
            else:
                self.link(value, node.inputs[index])
        return node.outputs[0]

    def torus(self, radius):
        """Map (u, v) onto a 4D torus: periodic in u and v, so the texture tiles."""
        a = self.math("MULTIPLY", self.u, 2 * math.pi)
        b = self.math("MULTIPLY", self.v, 2 * math.pi)
        vector = self.node("ShaderNodeCombineXYZ")
        self.link(self.math("MULTIPLY", self.math("COSINE", a), radius), vector.inputs["X"])
        self.link(self.math("MULTIPLY", self.math("SINE", a), radius), vector.inputs["Y"])
        self.link(self.math("MULTIPLY", self.math("COSINE", b), radius), vector.inputs["Z"])
        return vector.outputs[0], self.math("MULTIPLY", self.math("SINE", b), radius)

    def noise(self, radius, detail=6.0, roughness=0.55, offset=0.0, distortion=0.0):
        vector, w = self.torus(radius)
        node = self.node("ShaderNodeTexNoise", noise_dimensions="4D")
        self.link(vector, node.inputs["Vector"])
        self.link(self.math("ADD", w, offset), node.inputs["W"])
        node.inputs["Scale"].default_value = 1.0
        node.inputs["Detail"].default_value = detail
        node.inputs["Roughness"].default_value = roughness
        node.inputs["Distortion"].default_value = distortion
        return node.outputs["Fac"]

    def voronoi(self, radius, feature="F1", output="Distance", offset=0.0):
        vector, w = self.torus(radius)
        node = self.node("ShaderNodeTexVoronoi", voronoi_dimensions="4D", feature=feature)
        self.link(vector, node.inputs["Vector"])
        self.link(self.math("ADD", w, offset), node.inputs["W"])
        node.inputs["Scale"].default_value = 1.0
        return node.outputs[output]

    def ramp(self, factor, stops):
        node = self.node("ShaderNodeValToRGB")
        elements = node.color_ramp.elements
        while len(elements) < len(stops):
            elements.new(0.5)
        for element, (position, colour) in zip(elements, stops):
            element.position = position
            element.color = (*colour, 1.0)
        self.link(factor, node.inputs["Fac"])
        return node.outputs["Color"]

    def mix(self, factor, a, b, blend="MIX"):
        node = self.node("ShaderNodeMix", data_type="RGBA", blend_type=blend)
        for socket, value in ((node.inputs[0], factor), (node.inputs[6], a), (node.inputs[7], b)):
            if isinstance(value, (int, float, tuple)):
                socket.default_value = value
            else:
                self.link(value, socket)
        return node.outputs[2]

    def finish(self, colour, height, strength=0.8):
        """Light the bump-mapped surface with a fixed light and emit it for baking."""
        bump = self.node("ShaderNodeBump")
        bump.inputs["Strength"].default_value = min(1.0, strength)
        # Heights are in 0..1 over a 2-unit plane: a large distance makes the relief readable.
        bump.inputs["Distance"].default_value = 12.0 * strength
        self.link(height, bump.inputs["Height"])
        light = self.node("ShaderNodeVectorMath", operation="DOT_PRODUCT")
        self.link(bump.outputs["Normal"], light.inputs[0])
        length = math.sqrt(sum(c * c for c in LIGHT))
        light.inputs[1].default_value = tuple(c / length for c in LIGHT)
        diffuse = self.math("MAXIMUM", light.outputs["Value"], 0.0)
        shade = self.math("ADD", self.math("MULTIPLY", diffuse, 0.75), 0.35)
        cavity = self.math("ADD", self.math("MULTIPLY", height, 0.35), 0.75)
        lit = self.mix(1.0, colour, self.combine_grey(self.math("MULTIPLY", shade, cavity)), "MULTIPLY")
        emission = self.node("ShaderNodeEmission")
        self.link(lit, emission.inputs["Color"])
        output = self.node("ShaderNodeOutputMaterial")
        self.link(emission.outputs[0], output.inputs["Surface"])

    def combine_grey(self, value):
        node = self.node("ShaderNodeCombineColor")
        for channel in ("Red", "Green", "Blue"):
            self.link(value, node.inputs[channel])
        return node.outputs[0]

    def cells(self, columns, rows, stagger=0.5):
        """Brick-style cells: returns (mortar distance, cell id) periodic over the texture."""
        y = self.math("MULTIPLY", self.v, rows)
        row = self.math("FLOOR", y)
        shift = self.math("MULTIPLY", self.math("MODULO", row, 2.0), stagger)
        x = self.math("ADD", self.math("MULTIPLY", self.u, columns), shift)
        column = self.math("FLOOR", x)
        fx = self.math("FRACT", x)
        fy = self.math("FRACT", y)
        edge_x = self.math("MINIMUM", fx, self.math("SUBTRACT", 1.0, fx))
        edge_y = self.math("MINIMUM", fy, self.math("SUBTRACT", 1.0, fy))
        edge = self.math("MINIMUM", self.math("DIVIDE", edge_x, rows / columns), edge_y)
        cell = self.math("FRACT", self.math("SINE", self.math("ADD", self.math("MULTIPLY", column, 12.9898),
                                                               self.math("MULTIPLY", row, 78.233))))
        cell = self.math("FRACT", self.math("MULTIPLY", cell, 43758.5453))
        return edge, cell


# ---------------------------------------------------------------------------
# Materials: each builds a colour and a 0..1 height, then calls finish.

def bevel(g, edge, mortar_width, bevel_width, surface, roughness=0.15):
    """Height of a bevelled block (0 in the joint, 1 on the face) and its joint mask."""
    rise = g.math("DIVIDE", g.math("SUBTRACT", edge, mortar_width), bevel_width)
    rise = g.math("MINIMUM", g.math("MAXIMUM", rise, 0.0), 1.0)
    face = g.math("ADD", 1.0 - roughness, g.math("MULTIPLY", surface, roughness))
    return g.math("MULTIPLY", rise, face), g.math("GREATER_THAN", edge, mortar_width)


def dark_rock(g):
    height = g.math("ADD", g.math("MULTIPLY", g.noise(3.0, 8, 0.6), 0.7),
                    g.math("MULTIPLY", g.voronoi(5.0, "DISTANCE_TO_EDGE"), 0.6))
    colour = g.ramp(g.noise(4.0, 4, 0.5, 7.0), [(0.3, (0.06, 0.055, 0.07)), (0.7, (0.24, 0.23, 0.26))])
    g.finish(colour, height, 1.2)


def crystal_rock(g):
    rock = g.noise(3.0, 8, 0.6, 3.0)
    veins = g.math("POWER", g.math("SUBTRACT", 1.0, g.voronoi(2.5, "DISTANCE_TO_EDGE", offset=5.0)), 30.0)
    base = g.ramp(rock, [(0.3, (0.05, 0.06, 0.09)), (0.7, (0.20, 0.22, 0.28))])
    glow = g.ramp(veins, [(0.2, (0.0, 0.0, 0.0)), (1.0, (0.45, 0.85, 1.0))])
    g.finish(g.mix(veins, base, glow), g.math("ADD", rock, g.math("MULTIPLY", veins, 0.4)), 1.0)


def gravel(g):
    stones = g.voronoi(7.0, "F1", "Distance", 9.0)
    height = g.math("SUBTRACT", 1.0, g.math("MULTIPLY", stones, 1.4), clamp=True)
    pebble = g.voronoi(7.0, "F1", "Color", 9.0)
    grey = g.node("ShaderNodeRGBToBW")
    g.link(pebble, grey.inputs[0])
    shade = g.math("ADD", g.math("MULTIPLY", grey.outputs[0], 0.6), g.math("MULTIPLY", g.noise(9.0, 4, 0.5, 11.0), 0.4))
    stones_colour = g.ramp(shade, [(0.2, (0.16, 0.13, 0.10)), (0.8, (0.52, 0.46, 0.37))])
    g.finish(stones_colour, height, 1.0)


def magma_rock(g):
    crust = g.voronoi(2.0, "DISTANCE_TO_EDGE", offset=13.0)
    cracks = g.math("POWER", g.math("SUBTRACT", 1.0, crust), 25.0)
    rock = g.ramp(g.noise(5.0, 6, 0.55, 17.0), [(0.3, (0.08, 0.04, 0.03)), (0.8, (0.30, 0.16, 0.10))])
    lava = g.ramp(cracks, [(0.0, (0.5, 0.08, 0.0)), (1.0, (1.0, 0.65, 0.15))])
    g.finish(g.mix(cracks, rock, lava), g.math("SUBTRACT", crust, g.math("MULTIPLY", cracks, 0.5)), 1.1)


def flagstones(g):
    edge, cell = g.cells(4, 4, 0.5)
    surface = g.noise(6.0, 8, 0.6, 21.0)
    height, face = bevel(g, edge, 0.008, 0.03, surface, 0.3)
    stone = g.ramp(g.math("ADD", g.math("MULTIPLY", cell, 0.45), g.math("MULTIPLY", surface, 0.55)),
                   [(0.2, (0.30, 0.28, 0.25)), (0.8, (0.64, 0.61, 0.55))])
    colour = g.mix(face, (0.10, 0.09, 0.08, 1.0), stone)
    g.finish(colour, height, 1.0)


def bricks(g):
    edge, cell = g.cells(4, 8, 0.5)
    surface = g.noise(10.0, 8, 0.65, 25.0)
    height, face = bevel(g, edge, 0.012, 0.02, surface, 0.35)
    brick = g.ramp(g.math("ADD", g.math("MULTIPLY", cell, 0.55), g.math("MULTIPLY", surface, 0.45)),
                   [(0.15, (0.28, 0.09, 0.05)), (0.85, (0.66, 0.32, 0.21))])
    mortar = g.ramp(g.noise(20.0, 4, 0.5, 26.0), [(0.3, (0.42, 0.40, 0.36)), (0.7, (0.62, 0.60, 0.55))])
    colour = g.mix(face, mortar, brick)
    g.finish(colour, height, 1.0)


def mossy_blocks(g):
    edge, cell = g.cells(2, 3, 0.25)
    surface = g.noise(5.0, 8, 0.6, 29.0)
    height, face = bevel(g, edge, 0.006, 0.04, surface, 0.4)
    moss = g.math("POWER", g.noise(3.0, 5, 0.5, 31.0), 3.0)
    stone = g.ramp(g.math("ADD", g.math("MULTIPLY", cell, 0.4), g.math("MULTIPLY", surface, 0.6)),
                   [(0.2, (0.26, 0.27, 0.26)), (0.8, (0.52, 0.53, 0.50))])
    mossy = g.mix(g.math("MULTIPLY", moss, 4.0, clamp=True), stone, (0.16, 0.28, 0.08, 1.0))
    colour = g.mix(face, (0.07, 0.08, 0.06, 1.0), mossy)
    g.finish(colour, height, 1.0)


def planks(g):
    edge, cell = g.cells(6, 1, 0.0)
    gap = g.math("GREATER_THAN", edge, 0.006)
    # Grain runs along the vertical boards: stripes vary across u, gently warped by noise.
    grain = g.noise(1.5, 3, 0.45, 37.0, distortion=0.3)
    stripes = g.math("SINE", g.math("ADD", g.math("MULTIPLY", g.u, 150.0), g.math("MULTIPLY", grain, 5.0)))
    wood = g.ramp(g.math("ADD", g.math("MULTIPLY", stripes, 0.2), g.math("ADD", 0.4, g.math("MULTIPLY", cell, 0.3))),
                  [(0.2, (0.22, 0.12, 0.05)), (0.8, (0.55, 0.36, 0.18))])
    colour = g.mix(gap, (0.05, 0.03, 0.02, 1.0), wood)
    g.finish(colour, g.math("MULTIPLY", gap, g.math("ADD", 0.9, g.math("MULTIPLY", stripes, 0.03))), 0.5)


# ---------------------------------------------------------------------------

def bake(builder) -> np.ndarray:
    material = bpy.data.materials.new(builder.__name__)
    material.use_nodes = True
    graph = Graph(material)
    builder(graph)
    image = bpy.data.images.new(builder.__name__, SIZE, SIZE, float_buffer=True)
    target = graph.node("ShaderNodeTexImage")
    target.image = image
    graph.nodes.active = target
    plane = bpy.context.active_object
    plane.data.materials.clear()
    plane.data.materials.append(material)
    bpy.ops.object.bake(type="EMIT", margin=0)
    pixels = np.empty(SIZE * SIZE * 4, dtype=np.float32)
    image.pixels.foreach_get(pixels)
    return pixels.reshape(SIZE, SIZE, 4)


def save_jpeg(pixels: np.ndarray, path: Path) -> None:
    height, width, _ = pixels.shape
    # The bake is linear; the byte image stores sRGB-encoded values.
    encoded = np.clip(pixels, 0.0, 1.0).copy()
    rgb = encoded[..., :3]
    encoded[..., :3] = np.where(rgb <= 0.0031308, rgb * 12.92, 1.055 * np.power(rgb, 1.0 / 2.4) - 0.055)
    image = bpy.data.images.new(path.stem, width, height)
    image.pixels.foreach_set(encoded.ravel())
    scene = bpy.context.scene
    scene.render.image_settings.file_format = "JPEG"
    scene.render.image_settings.quality = JPEG_QUALITY
    scene.render.image_settings.color_management = "OVERRIDE"
    scene.view_settings.view_transform = "Standard"
    path.parent.mkdir(parents=True, exist_ok=True)
    image.save_render(str(path), scene=scene)
    print(f"wrote {path.relative_to(ROOT)}")


def setup_bake_scene() -> None:
    """Start from an empty scene with a single plane to bake on."""
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = 1
    bpy.ops.mesh.primitive_plane_add(size=2.0)


def main() -> None:
    setup_bake_scene()

    # Matrix atlas for Cavern Dash: tiles 1-4 read left to right, top to bottom.
    tiles = [bake(dark_rock), bake(crystal_rock), bake(gravel), bake(magma_rock)]
    atlas = np.zeros((SIZE * 2, SIZE * 2, 4), dtype=np.float32)
    # Blender pixel rows start at the bottom of the image.
    atlas[SIZE:, :SIZE], atlas[SIZE:, SIZE:] = tiles[0], tiles[1]
    atlas[:SIZE, :SIZE], atlas[:SIZE, SIZE:] = tiles[2], tiles[3]
    save_jpeg(atlas, ROOT / "examples/dbc/demos/cavern_dash/tiles.jpg")

    maze = ROOT / "examples/open-dbc/maze_runner/assets"
    for builder, name in ((flagstones, "floor"), (bricks, "wall1"), (planks, "roof")):
        save_jpeg(bake(builder), maze / f"{name}.jpg")


if __name__ == "__main__":
    main()
    sys.exit(0)
