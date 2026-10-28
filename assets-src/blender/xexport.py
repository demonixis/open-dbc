"""Minimal DirectX .x text exporter for rigid object hierarchies.

DarkBASIC models are rigid frame hierarchies: one mesh per frame (limb), no
armature, one animation clip per file. Blender is Z-up right-handed and
DirectX is Y-up left-handed, so every position and matrix is mirrored by
swapping Y and Z; that reflection also turns Blender's counter-clockwise front
faces into DirectX's clockwise ones, so the vertex order is kept.

Demo models are written to models/ and their textures to the sibling
textures/ folder, so TextureFilename names a file in ../textures/.
"""

from pathlib import Path

import bpy
from mathutils import Matrix

SWAP = Matrix(((1, 0, 0, 0), (0, 0, 1, 0), (0, 1, 0, 0), (0, 0, 0, 1)))


def f(value: float) -> str:
    text = f"{value:.6f}"
    return "0.000000" if text == "-0.000000" else text


def x_matrix(matrix: Matrix) -> str:
    converted = SWAP @ matrix @ SWAP
    # DirectX stores row-vector matrices: write the transpose, row by row.
    values = [converted[column][row] for row in range(4) for column in range(4)]
    return ",".join(f(value) for value in values)


def mesh_block(obj, textures: dict) -> str:
    depsgraph = bpy.context.evaluated_depsgraph_get()
    evaluated = obj.evaluated_get(depsgraph)
    mesh = evaluated.to_mesh()
    mesh.calc_loop_triangles()
    uv_layer = mesh.uv_layers.active
    vertices, normals, uvs, faces, face_materials = [], [], [], [], []
    shared = {}  # one vertex per distinct (position, normal, uv) corner
    for triangle in mesh.loop_triangles:
        corner = []
        for loop_index in triangle.loops:
            loop = mesh.loops[loop_index]
            co = mesh.vertices[loop.vertex_index].co
            normal = triangle.normal if not triangle.use_smooth else loop.normal
            uv = uv_layer.data[loop_index].uv if uv_layer else (0.0, 0.0)
            key = (round(co.x, 5), round(co.y, 5), round(co.z, 5), round(normal.x, 4), round(normal.y, 4),
                   round(normal.z, 4), round(uv[0], 5), round(uv[1], 5))
            index = shared.get(key)
            if index is None:
                index = shared[key] = len(vertices)
                vertices.append((co.x, co.z, co.y))
                normals.append((normal.x, normal.z, normal.y))
                uvs.append((uv[0], 1.0 - uv[1]))
            corner.append(index)
        faces.append(corner)
        face_materials.append(triangle.material_index)
    evaluated.to_mesh_clear()

    materials = [slot.material for slot in obj.material_slots] or [None]
    lines = [f"Mesh {obj.name}_mesh {{", f"{len(vertices)};"]
    lines += [f"{f(x)};{f(y)};{f(z)};" + ("," if i + 1 < len(vertices) else ";") for i, (x, y, z) in
              enumerate(vertices)]
    lines.append(f"{len(faces)};")
    lines += [f"3;{a},{b},{c};" + ("," if i + 1 < len(faces) else ";") for i, (a, b, c) in enumerate(faces)]
    lines += ["MeshNormals {", f"{len(normals)};"]
    lines += [f"{f(x)};{f(y)};{f(z)};" + ("," if i + 1 < len(normals) else ";") for i, (x, y, z) in
              enumerate(normals)]
    lines.append(f"{len(faces)};")
    lines += [f"3;{a},{b},{c};" + ("," if i + 1 < len(faces) else ";") for i, (a, b, c) in enumerate(faces)]
    lines += ["}", "MeshTextureCoords {", f"{len(uvs)};"]
    lines += [f"{f(u)};{f(v)};" + ("," if i + 1 < len(uvs) else ";") for i, (u, v) in enumerate(uvs)]
    lines += ["}", "MeshMaterialList {", f"{len(materials)};", f"{len(faces)};",
              ",\n".join(str(min(index, len(materials) - 1)) for index in face_materials) + ";;"]
    for material in materials:
        colour = material.diffuse_color if material else (0.8, 0.8, 0.8, 1.0)
        lines += ["Material {", f"{f(colour[0])};{f(colour[1])};{f(colour[2])};{f(colour[3])};;",
                  "16.000000;", "0.200000;0.200000;0.200000;;", "0.000000;0.000000;0.000000;;"]
        texture = textures.get(material.name) if material else None
        if texture:
            lines += ["TextureFilename {", f"\"../textures/{texture}\";", "}"]
        lines.append("}")
    lines += ["}", "}"]
    return "\n".join(lines)


def frame_block(obj, textures: dict) -> str:
    parts = [f"Frame {obj.name} {{", "FrameTransformMatrix {", x_matrix(obj.matrix_local) + ";;", "}"]
    if obj.type == "MESH":
        parts.append(mesh_block(obj, textures))
    for child in sorted(obj.children, key=lambda child: child.get("limb_order", 0)):
        parts.append(frame_block(child, textures))
    parts.append("}")
    return "\n".join(parts)


def animation_block(roots, first: int, last: int) -> str:
    scene = bpy.context.scene
    objects = []
    for root in roots:
        stack = [root]
        while stack:
            obj = stack.pop(0)
            objects.append(obj)
            stack[0:0] = sorted(obj.children, key=lambda child: child.get("limb_order", 0))
    keys = {obj.name: [] for obj in objects}
    for frame in range(first, last + 1):
        scene.frame_set(frame)
        for obj in objects:
            keys[obj.name].append(f"{frame - first};16;{x_matrix(obj.matrix_local)};;")
    blocks = ["AnimationSet clip {"]
    for obj in objects:
        blocks += ["Animation {", f"{{{obj.name}}}", "AnimationKey {", "4;", f"{len(keys[obj.name])};",
                   ",\n".join(keys[obj.name]) + ";", "}", "}"]
    blocks.append("}")
    return "\n".join(blocks)


def export(path: Path, roots, textures: dict, frames: tuple[int, int] | None = None) -> None:
    # Transforms set from Python are only reflected in matrix_local after a depsgraph update.
    bpy.context.view_layer.update()
    body = ["xof 0302txt 0032", "# OpenDBC example model exported by assets-src/blender/xexport.py"]
    body += [frame_block(root, textures) for root in roots]
    if frames:
        body.append(animation_block(roots, *frames))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(body) + "\n", encoding="ascii")
    print(f"wrote {path}")
