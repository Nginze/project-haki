#!/usr/bin/env python3
"""Convert Assimp-supported model files into self-contained canonical GLB files."""

from __future__ import annotations

import argparse
import hashlib
import io
import math
import os
import shutil
import struct
import sys
import tempfile
from pathlib import Path, PureWindowsPath
from typing import Any, Iterable

try:
    from PIL import Image, ImageChops, UnidentifiedImageError
    from pygltflib import (
        GLTF2,
        Accessor,
        Animation,
        AnimationChannel,
        AnimationChannelTarget,
        AnimationSampler,
        Asset,
        Attributes,
        Buffer,
        BufferView,
        Image as GltfImage,
        Material,
        Mesh,
        Node,
        NormalMaterialTexture,
        OcclusionTextureInfo,
        PbrMetallicRoughness,
        Primitive,
        Scene,
        Skin,
        Texture,
        TextureInfo,
    )
except ImportError as error:
    raise SystemExit(
        "Pillow and pygltflib are required. Run: python -m pip install -r tools/requirements.txt"
    ) from error


SUPPORTED_EXTENSIONS = {
    ".3ds", ".3mf", ".ac", ".ase", ".b3d", ".blend", ".cob", ".dae", ".dxf",
    ".fbx", ".ifc", ".lwo", ".lws", ".md2", ".md3", ".md5mesh", ".mdl", ".ms3d",
    ".obj", ".off", ".ply", ".q3d", ".raw", ".sib", ".smd", ".stl", ".x", ".x3d",
}
ARRAY_BUFFER = 34962
ELEMENT_ARRAY_BUFFER = 34963
FLOAT = 5126
UNSIGNED_INT = 5125
UNSIGNED_SHORT = 5123
TRIANGLES = 4


class ConversionFailure(RuntimeError):
    pass


class BinaryBuilder:
    def __init__(self) -> None:
        self.data = bytearray()
        self.views: list[BufferView] = []

    def append(self, data: bytes, target: int | None = None) -> int:
        while len(self.data) % 4:
            self.data.append(0)
        index = len(self.views)
        self.views.append(BufferView(buffer=0, byteOffset=len(self.data), byteLength=len(data), target=target))
        self.data.extend(data)
        return index


def finite_floats(values: Iterable[float], label: str) -> list[float]:
    result = [float(value) for value in values]
    if not all(math.isfinite(value) for value in result):
        raise ConversionFailure(f"{label} contains a non-finite value")
    return result


def packed_floats(values: list[float]) -> bytes:
    return struct.pack(f"<{len(values)}f", *values)


def packed_uints(values: list[int]) -> bytes:
    return struct.pack(f"<{len(values)}I", *values)


def packed_ushorts(values: list[int]) -> bytes:
    return struct.pack(f"<{len(values)}H", *values)


def decompose_matrix(matrix: list[float], label: str) -> tuple[list[float], list[float], list[float]]:
    translation = matrix[12:15]
    columns = [[matrix[column * 4 + row] for row in range(3)] for column in range(3)]
    scale = [math.sqrt(sum(value * value for value in column)) for column in columns]
    if min(scale) < 1e-8:
        raise ConversionFailure(f"{label} has zero scale and cannot be animated as TRS")
    m = [[columns[column][row] / scale[column] for column in range(3)] for row in range(3)]
    determinant = (
        m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
        - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
        + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0])
    )
    if determinant < 0:
        scale[0] = -scale[0]
        for row in range(3):
            m[row][0] = -m[row][0]
    trace = m[0][0] + m[1][1] + m[2][2]
    if trace > 0:
        s = math.sqrt(trace + 1.0) * 2
        rotation = [(m[2][1] - m[1][2]) / s, (m[0][2] - m[2][0]) / s,
                    (m[1][0] - m[0][1]) / s, 0.25 * s]
    elif m[0][0] > m[1][1] and m[0][0] > m[2][2]:
        s = math.sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]) * 2
        rotation = [0.25 * s, (m[0][1] + m[1][0]) / s, (m[0][2] + m[2][0]) / s,
                    (m[2][1] - m[1][2]) / s]
    elif m[1][1] > m[2][2]:
        s = math.sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]) * 2
        rotation = [(m[0][1] + m[1][0]) / s, 0.25 * s, (m[1][2] + m[2][1]) / s,
                    (m[0][2] - m[2][0]) / s]
    else:
        s = math.sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]) * 2
        rotation = [(m[0][2] + m[2][0]) / s, (m[1][2] + m[2][1]) / s, 0.25 * s,
                    (m[1][0] - m[0][1]) / s]
    length = math.sqrt(sum(value * value for value in rotation))
    return translation, [value / length for value in rotation], scale


def material_color(material: dict[str, Any], key: str, default: list[float], size: int) -> list[float]:
    value = material.get(key, default)
    result = finite_floats(value, key)[:size]
    return result + default[len(result):size]


def bone_name(bone: Any) -> str:
    return str(getattr(bone, "node_name", None) or bone.name or "").strip()


class ModelConverter:
    def __init__(self, source: Path, assimp: Any) -> None:
        self.source = source.resolve()
        self.assimp = assimp
        self.binary = BinaryBuilder()
        self.accessors: list[Accessor] = []
        self.images: list[GltfImage] = []
        self.textures: list[Texture] = []
        self.image_cache: dict[str, int] = {}
        self.warnings: list[str] = []

    def convert(self) -> GLTF2:
        flags = (
            self.assimp.Process_Triangulate
            | self.assimp.Process_JoinIdenticalVertices
            | self.assimp.Process_GenSmoothNormals
            | self.assimp.Process_CalcTangentSpace
            | self.assimp.Process_FlipUVs
            | self.assimp.Process_ImproveCacheLocality
            | self.assimp.Process_SortByPType
            | self.assimp.Process_ValidateDataStructure
            | self.assimp.Process_LimitBoneWeights
            | self.assimp.Process_PopulateArmatureData
        )
        try:
            imported = self.assimp.import_file(str(self.source), flags)
        except (FileNotFoundError, RuntimeError) as error:
            raise ConversionFailure(f"Could not import '{self.source}': {error}") from error

        if not imported.meshes:
            raise ConversionFailure(f"{self.source.name}: model contains no meshes")
        source_materials = imported.materials or [{}]
        materials = [self.convert_material(material, index) for index, material in enumerate(source_materials)]
        nodes: list[Node] = []
        node_indices: dict[str, int] = {}
        node_mesh_indices: list[tuple[Node, list[int]]] = []
        referenced_meshes: set[int] = set()

        def visit(imported_node: Any) -> int:
            node_index = len(nodes)
            name = str(imported_node.name or "").strip()
            if not name:
                raise ConversionFailure(f"{self.source.name}: node {node_index} has an empty name")
            if name in node_indices:
                raise ConversionFailure(f"{self.source.name}: duplicate node name '{name}'")
            matrix = self.node_matrix(imported_node)
            translation, rotation, scale = decompose_matrix(matrix, f"Node '{name}' transform")
            node = Node(name=name, translation=translation, rotation=rotation, scale=scale)
            nodes.append(node)
            node_indices[name] = node_index
            children: list[int] = []
            mesh_indices = [int(index) for index in imported_node.mesh_indices]
            node_mesh_indices.append((node, mesh_indices))
            for mesh_index in mesh_indices:
                if mesh_index < 0 or mesh_index >= len(imported.meshes):
                    raise ConversionFailure(f"{self.source.name}: node references missing mesh {mesh_index}")
                referenced_meshes.add(mesh_index)
            children.extend(visit(child) for child in imported_node.children)
            node.children = children or None
            return node_index

        roots = [visit(imported.root_node)] if imported.root_node is not None else []
        meshes = [self.convert_mesh(mesh, index, len(materials)) for index, mesh in enumerate(imported.meshes)]
        skins: list[Skin] = []
        mesh_skins: dict[int, int] = {}
        for mesh_index, source_mesh in enumerate(imported.meshes):
            if int(source_mesh.num_bones) or source_mesh.bones:
                mesh_skins[mesh_index] = len(skins)
                skins.append(self.convert_skin(source_mesh, mesh_index, node_indices))
        for node, mesh_indices in node_mesh_indices:
            if len(mesh_indices) == 1:
                node.mesh = mesh_indices[0]
                node.skin = mesh_skins.get(mesh_indices[0])
            elif mesh_indices:
                children = list(node.children or [])
                for mesh_index in mesh_indices:
                    children.insert(0, len(nodes))
                    nodes.append(Node(name=f"{node.name}-mesh-{mesh_index}", mesh=mesh_index,
                                      skin=mesh_skins.get(mesh_index)))
                node.children = children
        for mesh_index in range(len(meshes)):
            if mesh_index not in referenced_meshes:
                roots.append(len(nodes))
                nodes.append(Node(name=f"unreferenced-mesh-{mesh_index}", mesh=mesh_index,
                                  skin=mesh_skins.get(mesh_index)))
                self.warnings.append(f"{self.source.name}: mesh {mesh_index} was not referenced by the node hierarchy")

        source_animations = list(imported.animations or [])
        animation_names = [str(animation.name or "").strip() for animation in source_animations]
        if any(not name for name in animation_names):
            raise ConversionFailure(f"{self.source.name}: animation names must not be empty")
        if len(animation_names) != len(set(animation_names)):
            raise ConversionFailure(f"{self.source.name}: animation names must be unique")
        animations = [self.convert_animation(animation, index, node_indices)
                      for index, animation in enumerate(source_animations)]

        blob = bytes(self.binary.data)
        gltf = GLTF2(
            asset=Asset(version="2.0", generator="Haki Python model converter"),
            buffers=[Buffer(byteLength=len(blob))],
            bufferViews=self.binary.views,
            accessors=self.accessors,
            images=self.images,
            textures=self.textures,
            materials=materials,
            meshes=meshes,
            nodes=nodes,
            skins=skins,
            animations=animations,
            scenes=[Scene(nodes=roots)],
            scene=0,
        )
        gltf.set_binary_blob(blob)
        return gltf

    @staticmethod
    def node_matrix(node: Any) -> list[float]:
        rows = node.transformation
        if len(rows) != 4 or any(len(row) != 4 for row in rows):
            raise ConversionFailure(f"Node '{node.name}' has an invalid transform")
        return finite_floats((rows[row][column] for column in range(4) for row in range(4)),
                             f"Node '{node.name}' transform")

    def add_accessor(
        self,
        values: list[float] | list[int],
        component_type: int,
        value_type: str,
        count: int,
        target: int | None,
        minimum: list[float] | None = None,
        maximum: list[float] | None = None,
    ) -> int:
        if component_type == FLOAT:
            data = packed_floats(values)
        elif component_type == UNSIGNED_SHORT:
            data = packed_ushorts(values)
        else:
            data = packed_uints(values)
        view = self.binary.append(data, target)
        index = len(self.accessors)
        self.accessors.append(Accessor(
            bufferView=view,
            byteOffset=0,
            componentType=component_type,
            count=count,
            type=value_type,
            min=minimum,
            max=maximum,
        ))
        return index

    def convert_mesh(self, mesh: Any, mesh_index: int, material_count: int) -> Mesh:
        vertex_count = int(mesh.num_vertices)
        if vertex_count <= 0:
            raise ConversionFailure(f"{self.source.name}: mesh {mesh_index} contains no vertices")
        positions = finite_floats(mesh.vertices, f"Mesh {mesh_index} positions")
        if len(positions) != vertex_count * 3:
            raise ConversionFailure(f"{self.source.name}: mesh {mesh_index} has malformed positions")
        position_vectors = [positions[offset:offset + 3] for offset in range(0, len(positions), 3)]
        attributes = Attributes(POSITION=self.add_accessor(
            positions,
            FLOAT,
            "VEC3",
            vertex_count,
            ARRAY_BUFFER,
            [min(vector[axis] for vector in position_vectors) for axis in range(3)],
            [max(vector[axis] for vector in position_vectors) for axis in range(3)],
        ))

        normals: list[float] | None = None
        if mesh.normals is not None:
            normals = finite_floats(mesh.normals, f"Mesh {mesh_index} normals")
            if len(normals) != vertex_count * 3:
                raise ConversionFailure(f"{self.source.name}: mesh {mesh_index} has malformed normals")
            attributes.NORMAL = self.add_accessor(normals, FLOAT, "VEC3", vertex_count, ARRAY_BUFFER)

        if mesh.texcoords:
            components = int(mesh.num_uv_components[0])
            raw_uvs = finite_floats(mesh.texcoords[0], f"Mesh {mesh_index} UVs")
            if components < 2 or len(raw_uvs) != vertex_count * components:
                raise ConversionFailure(f"{self.source.name}: mesh {mesh_index} has malformed UVs")
            uvs = [value for vertex in range(vertex_count)
                   for value in raw_uvs[vertex * components:vertex * components + 2]]
            attributes.TEXCOORD_0 = self.add_accessor(uvs, FLOAT, "VEC2", vertex_count, ARRAY_BUFFER)
            if len(mesh.texcoords) > 1:
                self.warnings.append(f"{self.source.name}: mesh {mesh_index} has additional UV sets; only UV0 is exported")

        if mesh.colors:
            colors = finite_floats(mesh.colors[0], f"Mesh {mesh_index} colors")
            if len(colors) != vertex_count * 4:
                raise ConversionFailure(f"{self.source.name}: mesh {mesh_index} has malformed vertex colors")
            attributes.COLOR_0 = self.add_accessor(colors, FLOAT, "VEC4", vertex_count, ARRAY_BUFFER)
            if len(mesh.colors) > 1:
                self.warnings.append(f"{self.source.name}: mesh {mesh_index} has additional color sets; only set 0 is exported")

        if int(mesh.num_bones) or mesh.bones:
            joints = [[0, 0, 0, 0] for _ in range(vertex_count)]
            weights = [[0.0, 0.0, 0.0, 0.0] for _ in range(vertex_count)]
            influence_counts = [0] * vertex_count
            for joint_index, bone in enumerate(mesh.bones):
                name = bone_name(bone)
                if not name:
                    raise ConversionFailure(f"{self.source.name}: mesh {mesh_index} has a bone with an empty name")
                if hasattr(bone, "weight_vertex_ids"):
                    if len(bone.weight_vertex_ids) != len(bone.weights):
                        raise ConversionFailure(
                            f"{self.source.name}: mesh {mesh_index} bone '{name}' has mismatched weight arrays"
                        )
                    influences = zip(bone.weight_vertex_ids, bone.weights)
                else:
                    influences = ((influence.vertex_id, influence.weight) for influence in bone.weights)
                for raw_vertex, raw_weight in influences:
                    vertex = int(raw_vertex)
                    weight = float(raw_weight)
                    if vertex < 0 or vertex >= vertex_count:
                        raise ConversionFailure(f"{self.source.name}: mesh {mesh_index} bone '{name}' has an invalid vertex")
                    if not math.isfinite(weight) or weight < 0.0:
                        raise ConversionFailure(f"{self.source.name}: mesh {mesh_index} bone '{name}' has an invalid weight")
                    slot = influence_counts[vertex]
                    if slot >= 4:
                        raise ConversionFailure(
                            f"{self.source.name}: mesh {mesh_index} vertex {vertex} has more than four bone influences"
                        )
                    joints[vertex][slot] = joint_index
                    weights[vertex][slot] = weight
                    influence_counts[vertex] += 1
            for vertex, vertex_weights in enumerate(weights):
                total = sum(vertex_weights)
                if total <= 0.0:
                    raise ConversionFailure(
                        f"{self.source.name}: mesh {mesh_index} vertex {vertex} has no positive bone influence"
                    )
                weights[vertex] = [weight / total for weight in vertex_weights]
            attributes.JOINTS_0 = self.add_accessor(
                [value for vertex in joints for value in vertex], UNSIGNED_SHORT, "VEC4", vertex_count, ARRAY_BUFFER
            )
            attributes.WEIGHTS_0 = self.add_accessor(
                [value for vertex in weights for value in vertex], FLOAT, "VEC4", vertex_count, ARRAY_BUFFER
            )

        if normals is not None and mesh.tangents is not None and mesh.bitangents is not None:
            tangent_xyz = finite_floats(mesh.tangents, f"Mesh {mesh_index} tangents")
            bitangents = finite_floats(mesh.bitangents, f"Mesh {mesh_index} bitangents")
            if len(tangent_xyz) != vertex_count * 3 or len(bitangents) != vertex_count * 3:
                raise ConversionFailure(f"{self.source.name}: mesh {mesh_index} has malformed tangents")
            tangents: list[float] = []
            for vertex in range(vertex_count):
                offset = vertex * 3
                normal = normals[offset:offset + 3]
                tangent = tangent_xyz[offset:offset + 3]
                bitangent = bitangents[offset:offset + 3]
                cross = [
                    normal[1] * tangent[2] - normal[2] * tangent[1],
                    normal[2] * tangent[0] - normal[0] * tangent[2],
                    normal[0] * tangent[1] - normal[1] * tangent[0],
                ]
                handedness = -1.0 if sum(cross[axis] * bitangent[axis] for axis in range(3)) < 0.0 else 1.0
                tangents.extend((*tangent, handedness))
            attributes.TANGENT = self.add_accessor(tangents, FLOAT, "VEC4", vertex_count, ARRAY_BUFFER)

        indices = [int(value) for value in mesh.indices]
        if not indices or len(indices) % 3 or int(mesh.num_faces) * 3 != len(indices):
            raise ConversionFailure(f"{self.source.name}: mesh {mesh_index} does not contain triangle indices")
        if min(indices) < 0 or max(indices) >= vertex_count:
            raise ConversionFailure(f"{self.source.name}: mesh {mesh_index} contains an out-of-range index")
        index_accessor = self.add_accessor(indices, UNSIGNED_INT, "SCALAR", len(indices), ELEMENT_ARRAY_BUFFER,
                                           [min(indices)], [max(indices)])
        material_index = int(mesh.material_index)
        if material_index < 0 or material_index >= material_count:
            raise ConversionFailure(f"{self.source.name}: mesh {mesh_index} references missing material {material_index}")
        name = (mesh.name or "mesh").strip() or "mesh"
        return Mesh(name=f"{name}-{mesh_index}", primitives=[Primitive(
            attributes=attributes,
            indices=index_accessor,
            material=material_index,
            mode=TRIANGLES,
        )])

    def convert_skin(self, mesh: Any, mesh_index: int, node_indices: dict[str, int]) -> Skin:
        joints: list[int] = []
        matrices: list[float] = []
        names: set[str] = set()
        armature_names: set[str] = set()
        for bone in mesh.bones:
            name = bone_name(bone)
            if not name or name in names:
                raise ConversionFailure(f"{self.source.name}: mesh {mesh_index} has an empty or duplicate bone name")
            names.add(name)
            armature_name = str(getattr(bone, "armature_name", None) or "").strip()
            if armature_name:
                armature_names.add(armature_name)
            if name not in node_indices:
                raise ConversionFailure(f"{self.source.name}: mesh {mesh_index} bone '{name}' has no matching node")
            joints.append(node_indices[name])
            rows = bone.offset_matrix
            if len(rows) != 4 or any(len(row) != 4 for row in rows):
                raise ConversionFailure(f"{self.source.name}: mesh {mesh_index} bone '{name}' has an invalid offset matrix")
            matrices.extend(finite_floats(
                (rows[row][column] for column in range(4) for row in range(4)),
                f"Mesh {mesh_index} bone '{name}' offset matrix",
            ))
        if len(armature_names) > 1:
            raise ConversionFailure(f"{self.source.name}: mesh {mesh_index} references multiple armatures")
        skeleton = None
        if armature_names:
            armature_name = next(iter(armature_names))
            if armature_name not in node_indices:
                raise ConversionFailure(
                    f"{self.source.name}: mesh {mesh_index} armature '{armature_name}' has no matching node"
                )
            skeleton = node_indices[armature_name]
        accessor = self.add_accessor(matrices, FLOAT, "MAT4", len(joints), None)
        return Skin(name=f"{(mesh.name or 'mesh').strip() or 'mesh'}-{mesh_index}-skin",
                    joints=joints, skeleton=skeleton, inverseBindMatrices=accessor)

    def convert_animation(self, animation: Any, animation_index: int, node_indices: dict[str, int]) -> Animation:
        name = str(animation.name or "").strip()
        if not name:
            raise ConversionFailure(f"{self.source.name}: animation {animation_index} has an empty name")
        ticks_per_second = float(animation.ticks_per_second)
        duration = float(animation.duration)
        if not math.isfinite(ticks_per_second) or ticks_per_second <= 0.0:
            raise ConversionFailure(f"{self.source.name}: animation '{name}' has invalid ticks per second")
        if not math.isfinite(duration) or duration < 0.0:
            raise ConversionFailure(f"{self.source.name}: animation '{name}' has invalid duration")
        samplers: list[AnimationSampler] = []
        channels: list[AnimationChannel] = []
        for node_channel in animation.channels:
            node_name = str(node_channel.node_name or "").strip()
            if not node_name or node_name not in node_indices:
                raise ConversionFailure(f"{self.source.name}: animation '{name}' targets unknown node '{node_name}'")
            tracks = (("translation", "position", 3, False),
                      ("rotation", "rotation", 4, True),
                      ("scale", "scaling", 3, False))
            for path, assimp_path, components, quaternion in tracks:
                if hasattr(node_channel, f"{assimp_path}_key_times"):
                    raw_times = list(getattr(node_channel, f"{assimp_path}_key_times"))
                    raw_values = list(getattr(node_channel, f"{assimp_path}_key_values"))
                    try:
                        float(raw_values[0]) if raw_values else 0.0
                        scalar_values = True
                    except (TypeError, ValueError):
                        scalar_values = False
                    if scalar_values:
                        if len(raw_values) != len(raw_times) * components:
                            raise ConversionFailure(
                                f"{self.source.name}: animation '{name}' {node_name} {path} has mismatched key arrays"
                            )
                        keys = list(zip(raw_times, [raw_values[offset:offset + components]
                                                   for offset in range(0, len(raw_values), components)]))
                    else:
                        if len(raw_values) != len(raw_times):
                            raise ConversionFailure(
                                f"{self.source.name}: animation '{name}' {node_name} {path} has mismatched key arrays"
                            )
                        keys = list(zip(raw_times, raw_values))
                else:
                    keys = list(getattr(node_channel, f"{assimp_path}_keys"))
                if not keys:
                    continue
                times: list[float] = []
                values: list[float] = []
                for key in keys:
                    raw_time = key.time if hasattr(key, "time") else key[0]
                    raw_value = key.value if hasattr(key, "value") else key[1]
                    time = float(raw_time) / ticks_per_second
                    if not math.isfinite(time) or time < 0.0 or (times and time <= times[-1]):
                        raise ConversionFailure(
                            f"{self.source.name}: animation '{name}' {node_name} {path} times must be finite and increasing"
                        )
                    if float(raw_time) > duration + 1e-6:
                        raise ConversionFailure(
                            f"{self.source.name}: animation '{name}' {node_name} {path} key exceeds its duration"
                        )
                    times.append(time)
                    if quaternion and all(hasattr(raw_value, field) for field in ("x", "y", "z", "w")):
                        value = [raw_value.x, raw_value.y, raw_value.z, raw_value.w]
                    else:
                        value = list(raw_value)
                    value = finite_floats(value, f"Animation '{name}' {node_name} {path}")
                    if len(value) != components:
                        raise ConversionFailure(
                            f"{self.source.name}: animation '{name}' {node_name} {path} key has the wrong size"
                        )
                    if quaternion:
                        length = math.sqrt(sum(component * component for component in value))
                        if length < 1e-8:
                            raise ConversionFailure(
                                f"{self.source.name}: animation '{name}' {node_name} has a zero rotation"
                            )
                        value = [component / length for component in value]
                    values.extend(value)
                input_accessor = self.add_accessor(times, FLOAT, "SCALAR", len(times), None,
                                                   [times[0]], [times[-1]])
                output_accessor = self.add_accessor(values, FLOAT, f"VEC{components}", len(times), None)
                sampler_index = len(samplers)
                samplers.append(AnimationSampler(input=input_accessor, output=output_accessor, interpolation="LINEAR"))
                channels.append(AnimationChannel(
                    sampler=sampler_index,
                    target=AnimationChannelTarget(node=node_indices[node_name], path=path),
                ))
        if not channels:
            raise ConversionFailure(f"{self.source.name}: animation '{name}' contains no TRS keys")
        return Animation(name=name, samplers=samplers, channels=channels)

    def convert_material(self, source: dict[str, Any], material_index: int) -> Material:
        diffuse = material_color(source, "COLOR_DIFFUSE", [1.0, 1.0, 1.0, 1.0], 4)
        opacity = float(source.get("OPACITY", 1.0))
        if not math.isfinite(opacity):
            raise ConversionFailure(f"Material {material_index} has non-finite opacity")
        diffuse[3] = max(0.0, min(1.0, diffuse[3] * opacity))
        shininess = float(source.get("SHININESS", 0.0))
        if not math.isfinite(shininess):
            raise ConversionFailure(f"Material {material_index} has non-finite shininess")
        shininess = max(0.0, shininess)
        roughness = max(0.04, min(1.0, math.sqrt(2.0 / (shininess + 2.0))))
        textures = source.get("TEXTURES", {})
        diffuse_path = self.first_texture(textures, self.assimp.TextureType_DIFFUSE, material_index)
        opacity_path = self.first_texture(textures, self.assimp.TextureType_OPACITY, material_index)
        base_texture = self.combined_base_texture(diffuse_path, opacity_path, material_index)
        pbr = PbrMetallicRoughness(
            baseColorFactor=diffuse,
            metallicFactor=0.0,
            roughnessFactor=roughness,
            baseColorTexture=TextureInfo(index=base_texture) if base_texture is not None else None,
        )

        normal_path = (
            self.first_texture(textures, self.assimp.TextureType_NORMALS, material_index)
            or self.first_texture(textures, self.assimp.TextureType_HEIGHT, material_index)
        )
        emissive_path = self.first_texture(textures, self.assimp.TextureType_EMISSIVE, material_index)
        lightmap_path = self.first_texture(textures, self.assimp.TextureType_LIGHTMAP, material_index)
        emissive = material_color(source, "COLOR_EMISSIVE", [0.0, 0.0, 0.0], 3)
        if emissive_path and max(emissive) <= 0.0:
            emissive = [1.0, 1.0, 1.0]
        bump_scale = float(source.get("BUMPSCALING", 1.0))
        if not math.isfinite(bump_scale):
            raise ConversionFailure(f"Material {material_index} has non-finite bump scaling")

        ignored_types = {
            self.assimp.TextureType_SPECULAR: "specular",
            self.assimp.TextureType_SHININESS: "shininess",
            self.assimp.TextureType_REFLECTION: "reflection",
            self.assimp.TextureType_DISPLACEMENT: "displacement",
        }
        for texture_type, label in ignored_types.items():
            if textures.get(texture_type):
                self.warnings.append(
                    f"{self.source.name}: material {material_index} {label} texture is not representable in core glTF PBR"
                )

        name = str(source.get("NAME") or f"material-{material_index}")
        return Material(
            name=name,
            pbrMetallicRoughness=pbr,
            normalTexture=(
                NormalMaterialTexture(index=self.add_texture(normal_path, f"{name}-normal"), scale=bump_scale)
                if normal_path else None
            ),
            emissiveTexture=(TextureInfo(index=self.add_texture(emissive_path, f"{name}-emissive"))
                             if emissive_path else None),
            occlusionTexture=(OcclusionTextureInfo(index=self.add_texture(lightmap_path, f"{name}-occlusion"))
                              if lightmap_path else None),
            emissiveFactor=emissive,
            alphaMode="BLEND" if diffuse[3] < 0.999 or opacity_path else "OPAQUE",
            doubleSided=bool(source.get("TWOSIDED", False)),
        )

    def first_texture(self, textures: dict[Any, Any], texture_type: int, material_index: int) -> Path | None:
        paths = textures.get(texture_type, [])
        if not paths:
            return None
        if len(paths) > 1:
            self.warnings.append(
                f"{self.source.name}: material {material_index} has multiple textures of one type; only the first is exported"
            )
        raw_path = str(paths[0])
        if raw_path.startswith("*"):
            raise ConversionFailure(
                f"{self.source.name}: embedded texture '{raw_path}' cannot be read through assimp-py"
            )
        normalized = raw_path.replace("\\", "/")
        candidate = Path(normalized)
        candidates = [candidate] if candidate.is_absolute() else [self.source.parent / candidate]
        candidates.append(self.source.parent / PureWindowsPath(raw_path).name)
        for path in candidates:
            if path.is_file():
                return path.resolve()
        raise ConversionFailure(f"{self.source.name}: referenced texture was not found: {raw_path}")

    def normalized_image(self, path: Path) -> Image.Image:
        try:
            with Image.open(path) as image:
                return image.convert("RGBA")
        except (OSError, UnidentifiedImageError) as error:
            raise ConversionFailure(f"Could not decode texture '{path}': {error}") from error

    def combined_base_texture(self, diffuse: Path | None, opacity: Path | None, material_index: int) -> int | None:
        if diffuse is None and opacity is None:
            return None
        image = self.normalized_image(diffuse) if diffuse else Image.new("RGBA", self.normalized_image(opacity).size, "white")
        if opacity:
            opacity_image = self.normalized_image(opacity).convert("L")
            if opacity_image.size != image.size:
                raise ConversionFailure(
                    f"{self.source.name}: material {material_index} diffuse and opacity textures have different sizes"
                )
            image.putalpha(ImageChops.multiply(image.getchannel("A"), opacity_image))
        return self.add_image(image, f"material-{material_index}-base-color")

    def add_texture(self, path: Path, name: str) -> int:
        return self.add_image(self.normalized_image(path), name)

    def add_image(self, image: Image.Image, name: str) -> int:
        output = io.BytesIO()
        image.save(output, format="PNG", optimize=True)
        data = output.getvalue()
        digest = hashlib.sha256(data).hexdigest()
        if digest in self.image_cache:
            return self.image_cache[digest]
        view = self.binary.append(data)
        image_index = len(self.images)
        texture_index = len(self.textures)
        self.images.append(GltfImage(name=name, bufferView=view, mimeType="image/png"))
        self.textures.append(Texture(name=name, source=image_index))
        self.image_cache[digest] = texture_index
        return texture_index


def load_assimp() -> Any:
    try:
        import assimp_py
    except ImportError as error:
        raise ConversionFailure(
            "assimp-py is required. Run: python -m pip install -r tools/requirements.txt"
        ) from error
    return assimp_py


def convert_file(source: Path, destination: Path, assimp: Any, dry_run: bool = False) -> list[str]:
    converter = ModelConverter(source, assimp)
    gltf = converter.convert()
    if not dry_run:
        destination.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.NamedTemporaryFile(suffix=".glb", dir=destination.parent, delete=False) as output:
            temporary = Path(output.name)
        try:
            gltf.save_binary(str(temporary))
            os.replace(temporary, destination)
        finally:
            temporary.unlink(missing_ok=True)
    return converter.warnings


def discover_sources(source: Path) -> tuple[Path, list[Path]]:
    source = source.resolve()
    if source.is_file():
        if source.suffix.lower() in {".gltf", ".glb"}:
            raise ConversionFailure("glTF files are already canonical; pass them directly to import_gltf_pack.py")
        if source.suffix.lower() not in SUPPORTED_EXTENSIONS:
            raise ConversionFailure(f"Unsupported source extension: {source.suffix or '<none>'}")
        return source.parent, [source]
    if not source.is_dir():
        raise ConversionFailure(f"Source does not exist: {source}")
    paths = sorted(path for path in source.rglob("*") if path.suffix.lower() in SUPPORTED_EXTENSIONS)
    if not paths:
        raise ConversionFailure(f"No supported model files found under {source}")
    return source, paths


def output_paths(source: Path, destination: Path) -> tuple[list[tuple[Path, Path]], bool]:
    source_root, sources = discover_sources(source)
    if source.is_file():
        if destination.suffix.lower() != ".glb":
            raise ConversionFailure("The destination for a single model must end in .glb")
        return [(sources[0], destination.resolve())], False
    destination = destination.resolve()
    outputs: list[tuple[Path, Path]] = []
    seen: dict[Path, Path] = {}
    for path in sources:
        relative = path.relative_to(source_root).with_suffix(".glb")
        if relative in seen:
            raise ConversionFailure(f"Output collision between '{seen[relative]}' and '{path}'")
        seen[relative] = path
        outputs.append((path, destination / relative))
    return outputs, True


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="source model file or directory")
    parser.add_argument("destination", type=Path, help="output .glb file or directory")
    parser.add_argument("--dry-run", action="store_true", help="load and validate without writing files")
    parser.add_argument("--force", action="store_true", help="replace an existing output file or directory")
    return parser.parse_args(argv)


def remove_path(path: Path) -> None:
    if path.is_dir():
        shutil.rmtree(path)
    else:
        path.unlink(missing_ok=True)


def run(args: argparse.Namespace) -> None:
    outputs, directory_mode = output_paths(args.source, args.destination)
    destination = args.destination.resolve()
    if destination.exists() and not args.force and not args.dry_run:
        raise ConversionFailure(f"Destination already exists; use --force: {destination}")
    assimp = load_assimp()
    print(f"Models: {len(outputs)}")

    if args.dry_run:
        for source, output in outputs:
            for warning in convert_file(source, output, assimp, dry_run=True):
                print(f"warning: {warning}", file=sys.stderr)
        print("Dry run: no files changed")
        return

    if not directory_mode:
        for warning in convert_file(outputs[0][0], outputs[0][1], assimp):
            print(f"warning: {warning}", file=sys.stderr)
        print(f"Converted to {destination}")
        return

    destination.parent.mkdir(parents=True, exist_ok=True)
    stage = destination.with_name(f".{destination.name}.staging")
    backup = destination.with_name(f".{destination.name}.backup")
    if stage.exists():
        remove_path(stage)
    stage.mkdir(parents=True)
    try:
        for source, output in outputs:
            staged_output = stage / output.relative_to(destination)
            for warning in convert_file(source, staged_output, assimp):
                print(f"warning: {warning}", file=sys.stderr)
        if backup.exists():
            remove_path(backup)
        if destination.exists():
            destination.rename(backup)
        stage.rename(destination)
        if backup.exists():
            remove_path(backup)
    except Exception:
        if not destination.exists() and backup.exists():
            backup.rename(destination)
        raise
    finally:
        if stage.exists():
            remove_path(stage)
    print(f"Converted to {destination}")


def main(argv: list[str] | None = None) -> int:
    try:
        run(parse_args(argv or sys.argv[1:]))
        return 0
    except ConversionFailure as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
