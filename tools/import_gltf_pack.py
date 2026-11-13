#!/usr/bin/env python3
"""Import glTF packages into Haki's catalog, material, and scene formats."""

from __future__ import annotations

import argparse
import base64
import hashlib
import io
import json
import math
import os
import re
import shutil
import struct
import sys
import tempfile
import zipfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any
from urllib.parse import unquote, unquote_to_bytes

try:
    from pygltflib import GLTF2
    from PIL import Image, UnidentifiedImageError
except ImportError as error:
    raise SystemExit(
        f"Importer dependency failed in Python {sys.version.split()[0]} ({sys.executable}): {error}. "
        "Use the project's Python environment with tools/requirements.txt installed. "
        "If Blender runs in Flatpak, execute the importer on the host."
    ) from error


from blender.haki_editor.metadata import validate_metadata, validate_transform, validate_scene_metadata, MetadataError


CATALOG_TYPE = "haki.asset-catalog"
MANIFEST_TYPE = "haki.scene-manifest"
PBR_SHADER_ID = "engine.shader.gltf-pbr"
IDENTIFIER_PATTERN = re.compile(r"[^a-z0-9]+")
ZIP_MAX_MEMBERS = 20000
ZIP_MAX_UNCOMPRESSED_BYTES = 4 * 1024 ** 3
TEXTURED_PARAMETERS = (
    "BaseColorTexture", "MetallicRoughnessTexture", "NormalTexture", "EmissiveTexture",
    "OcclusionTexture", "TransmissionTexture", "ThicknessTexture",
)
# Material extensions imported exactly or approximated (reported as notes, not warnings).
EXACT_MATERIAL_EXTENSIONS = {
    "KHR_materials_pbrSpecularGlossiness", "KHR_materials_transmission", "KHR_materials_volume",
    "KHR_materials_ior", "KHR_materials_emissive_strength",
}
APPROXIMATED_MATERIAL_EXTENSIONS = {
    "KHR_materials_specular", "KHR_materials_unlit", "KHR_materials_clearcoat", "KHR_materials_sheen",
}
# Document-level extensions that need no further handling or have their own diagnostics.
KNOWN_DOCUMENT_EXTENSIONS = (
    EXACT_MATERIAL_EXTENSIONS | APPROXIMATED_MATERIAL_EXTENSIONS |
    {"KHR_lights_punctual", "KHR_texture_transform", "KHR_materials_variants"}
)


class ImportFailure(RuntimeError):
    pass


@dataclass
class TextureOutput:
    logical_id: str
    filename: str
    data: bytes
    srgb: bool


@dataclass
class MaterialOutput:
    logical_id: str
    filename: str
    document: dict[str, Any]


@dataclass
class EntityOutput:
    entity_id: str
    name: str
    matrix: list[float]
    mesh_id: str | None = None
    materials: list[str] = field(default_factory=list)
    skeleton_id: str | None = None
    clips: dict[str, str] = field(default_factory=dict)
    camera: dict[str, Any] | None = None
    light_type: str | None = None
    light: dict[str, Any] | None = None
    authored_components: dict[str, Any] = field(default_factory=dict)


@dataclass
class ModelPlan:
    path: Path
    relative_path: Path
    gltf: Any
    entities: list[EntityOutput] = field(default_factory=list)
    minimum: list[float] = field(default_factory=lambda: [math.inf, math.inf, math.inf])
    maximum: list[float] = field(default_factory=lambda: [-math.inf, -math.inf, -math.inf])


def slug(value: str, fallback: str = "asset") -> str:
    cleaned = IDENTIFIER_PATTERN.sub("-", value.strip().lower()).strip("-")
    return cleaned or fallback


def short_hash(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()[:10]


def json_bytes(value: Any) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":")).encode("utf-8")


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ImportFailure(f"Could not read JSON '{path}': {error}") from error
    if not isinstance(value, dict):
        raise ImportFailure(f"Expected a JSON object in '{path}'")
    return value


def atomic_json_write(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile("w", encoding="utf-8", dir=path.parent, delete=False) as output:
        json.dump(value, output, indent=2)
        output.write("\n")
        temporary = Path(output.name)
    os.replace(temporary, path)


def decode_data_uri(uri: str) -> bytes:
    try:
        header, payload = uri.split(",", 1)
    except ValueError as error:
        raise ImportFailure("Malformed data URI") from error
    return base64.b64decode(payload) if ";base64" in header else unquote_to_bytes(payload)


def external_uri_path(model_path: Path, uri: str) -> Path:
    if "://" in uri:
        raise ImportFailure(f"Remote URI is not supported: {uri}")
    return (model_path.parent / unquote(uri)).resolve()


def buffer_bytes(gltf: Any, model_path: Path, index: int) -> bytes:
    buffer = gltf.buffers[index]
    if buffer.uri:
        if buffer.uri.startswith("data:"):
            return decode_data_uri(buffer.uri)
        path = external_uri_path(model_path, buffer.uri)
        if not path.is_file():
            raise ImportFailure(f"Missing glTF buffer: {path}")
        return path.read_bytes()
    blob = gltf.binary_blob()
    if blob is None:
        raise ImportFailure(f"glTF buffer {index} has no URI or GLB binary data")
    return bytes(blob)


def float_accessor_values(plan: ModelPlan, accessor_index: int) -> list[float]:
    accessors = plan.gltf.accessors or []
    if accessor_index < 0 or accessor_index >= len(accessors):
        raise ImportFailure(f"{plan.relative_path}: accessor {accessor_index} is missing")
    accessor = accessors[accessor_index]
    if accessor.componentType != 5126 or accessor.bufferView is None:
        raise ImportFailure(f"{plan.relative_path}: accessor {accessor_index} must contain FLOAT data")
    if accessor.sparse is not None and accessor.sparse.count:
        raise ImportFailure(f"{plan.relative_path}: sparse morph-weight accessors are not supported")
    components = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}.get(accessor.type)
    if components is None:
        raise ImportFailure(f"{plan.relative_path}: accessor {accessor_index} has an unsupported value type")
    views = plan.gltf.bufferViews or []
    if accessor.bufferView < 0 or accessor.bufferView >= len(views):
        raise ImportFailure(f"{plan.relative_path}: accessor {accessor_index} references a missing buffer view")
    view = views[accessor.bufferView]
    data = buffer_bytes(plan.gltf, plan.path, view.buffer)
    element_size = components * 4
    stride = view.byteStride or element_size
    if stride < element_size:
        raise ImportFailure(f"{plan.relative_path}: accessor {accessor_index} has an invalid byte stride")
    offset = (view.byteOffset or 0) + (accessor.byteOffset or 0)
    end = offset if accessor.count == 0 else offset + (accessor.count - 1) * stride + element_size
    view_end = (view.byteOffset or 0) + view.byteLength
    if offset < (view.byteOffset or 0) or end > view_end or end > len(data):
        raise ImportFailure(f"{plan.relative_path}: accessor {accessor_index} exceeds its buffer view")
    values: list[float] = []
    for index in range(accessor.count):
        values.extend(struct.unpack_from(f"<{components}f", data, offset + index * stride))
    return values


def morph_target_count(plan: ModelPlan, mesh_index: int) -> int:
    mesh = plan.gltf.meshes[mesh_index]
    counts = {len(primitive.targets or []) for primitive in mesh.primitives}
    if len(counts) > 1:
        raise ImportFailure(f"{plan.relative_path}: mesh {mesh_index} primitives have different morph target counts")
    return counts.pop() if counts else 0


def validate_inert_morph_defaults(plan: ModelPlan, node_index: int, mesh_index: int) -> None:
    target_count = morph_target_count(plan, mesh_index)
    if target_count == 0:
        return
    node = plan.gltf.nodes[node_index]
    mesh = plan.gltf.meshes[mesh_index]
    node_weights = getattr(node, "weights", None)
    weights = node_weights if node_weights is not None else getattr(mesh, "weights", None)
    effective = list(weights) if weights is not None else [0.0] * target_count
    if len(effective) != target_count or any(not math.isfinite(value) or value != 0.0 for value in effective):
        raise ImportFailure(
            f"{plan.relative_path}: node '{getattr(node, 'name', None) or node_index}' uses active morph targets; "
            "runtime morph deformation is not supported"
        )


def image_bytes(gltf: Any, model_path: Path, image_index: int) -> tuple[bytes, str]:
    image = gltf.images[image_index]
    if image.uri:
        if image.uri.startswith("data:"):
            mime = image.uri.split(";", 1)[0].removeprefix("data:")
            return decode_data_uri(image.uri), extension_for_mime(mime)
        path = external_uri_path(model_path, image.uri)
        if not path.is_file():
            raise ImportFailure(f"Missing glTF image: {path}")
        return path.read_bytes(), path.suffix.lower() or extension_for_mime(image.mimeType)
    if image.bufferView is None:
        raise ImportFailure(f"glTF image {image_index} has no URI or buffer view")
    view = gltf.bufferViews[image.bufferView]
    data = buffer_bytes(gltf, model_path, view.buffer)
    offset = view.byteOffset or 0
    return data[offset : offset + view.byteLength], extension_for_mime(image.mimeType)


def extension_for_mime(mime: str | None) -> str:
    return {
        "image/png": ".png",
        "image/jpeg": ".jpg",
        "image/webp": ".webp",
    }.get(mime or "", ".bin")


def normalize_image(data: bytes) -> bytes:
    try:
        with Image.open(io.BytesIO(data)) as image:
            output = io.BytesIO()
            image.convert("RGBA").save(output, format="PNG", compress_level=6)
            return output.getvalue()
    except (UnidentifiedImageError, OSError) as error:
        raise ImportFailure("Image format is unsupported; PNG, JPEG, and Pillow-supported WebP are accepted") from error


def identity_matrix() -> list[float]:
    return [1.0, 0.0, 0.0, 0.0,
            0.0, 1.0, 0.0, 0.0,
            0.0, 0.0, 1.0, 0.0,
            0.0, 0.0, 0.0, 1.0]


def multiply_matrix(left: list[float], right: list[float]) -> list[float]:
    result = [0.0] * 16
    for column in range(4):
        for row in range(4):
            result[column * 4 + row] = sum(
                left[k * 4 + row] * right[column * 4 + k] for k in range(4)
            )
    return result


def trs_matrix(translation: list[float], rotation: list[float], scale: list[float]) -> list[float]:
    x, y, z, w = rotation
    sx, sy, sz = scale
    xx, yy, zz = x * x, y * y, z * z
    xy, xz, yz = x * y, x * z, y * z
    wx, wy, wz = w * x, w * y, w * z
    return [
        (1 - 2 * (yy + zz)) * sx, 2 * (xy + wz) * sx, 2 * (xz - wy) * sx, 0.0,
        2 * (xy - wz) * sy, (1 - 2 * (xx + zz)) * sy, 2 * (yz + wx) * sy, 0.0,
        2 * (xz + wy) * sz, 2 * (yz - wx) * sz, (1 - 2 * (xx + yy)) * sz, 0.0,
        translation[0], translation[1], translation[2], 1.0,
    ]


def node_matrix(node: Any) -> list[float]:
    if node.matrix and len(node.matrix) == 16:
        return [float(value) for value in node.matrix]
    return trs_matrix(
        list(node.translation or [0.0, 0.0, 0.0]),
        list(node.rotation or [0.0, 0.0, 0.0, 1.0]),
        list(node.scale or [1.0, 1.0, 1.0]),
    )


def object_member(value: Any, name: str, default: Any = None) -> Any:
    return value.get(name, default) if isinstance(value, dict) else getattr(value, name, default)


def finite_number(value: Any, description: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ImportFailure(f"{description} must be a finite number")
    return float(value)


def transform_point(matrix: list[float], point: list[float]) -> list[float]:
    return [
        matrix[0] * point[0] + matrix[4] * point[1] + matrix[8] * point[2] + matrix[12],
        matrix[1] * point[0] + matrix[5] * point[1] + matrix[9] * point[2] + matrix[13],
        matrix[2] * point[0] + matrix[6] * point[1] + matrix[10] * point[2] + matrix[14],
    ]


def normalize_quaternion(quaternion: list[float]) -> list[float]:
    length = math.sqrt(sum(value * value for value in quaternion))
    return [value / length for value in quaternion] if length > 1e-8 else [0.0, 0.0, 0.0, 1.0]


def quaternion_from_rotation(columns: list[list[float]]) -> list[float]:
    m00, m01, m02 = columns[0][0], columns[1][0], columns[2][0]
    m10, m11, m12 = columns[0][1], columns[1][1], columns[2][1]
    m20, m21, m22 = columns[0][2], columns[1][2], columns[2][2]
    trace = m00 + m11 + m22
    if trace > 0:
        s = math.sqrt(trace + 1.0) * 2
        q = [(m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25 * s]
    elif m00 > m11 and m00 > m22:
        s = math.sqrt(1.0 + m00 - m11 - m22) * 2
        q = [0.25 * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s]
    elif m11 > m22:
        s = math.sqrt(1.0 + m11 - m00 - m22) * 2
        q = [(m01 + m10) / s, 0.25 * s, (m12 + m21) / s, (m02 - m20) / s]
    else:
        s = math.sqrt(1.0 + m22 - m00 - m11) * 2
        q = [(m02 + m20) / s, (m12 + m21) / s, 0.25 * s, (m10 - m01) / s]
    return normalize_quaternion(q)


def decompose_matrix(matrix: list[float]) -> tuple[list[float], list[float], list[float]]:
    translation = [matrix[12], matrix[13], matrix[14]]
    columns = [[matrix[column * 4 + row] for row in range(3)] for column in range(3)]
    scale = [math.sqrt(sum(value * value for value in column)) for column in columns]
    if min(scale) < 1e-8:
        raise ImportFailure("Cannot decompose a node transform with zero scale")
    rotation_columns = [[columns[column][row] / scale[column] for row in range(3)] for column in range(3)]
    determinant = (
        rotation_columns[0][0] * (rotation_columns[1][1] * rotation_columns[2][2] - rotation_columns[1][2] * rotation_columns[2][1])
        - rotation_columns[1][0] * (rotation_columns[0][1] * rotation_columns[2][2] - rotation_columns[0][2] * rotation_columns[2][1])
        + rotation_columns[2][0] * (rotation_columns[0][1] * rotation_columns[1][2] - rotation_columns[0][2] * rotation_columns[1][1])
    )
    if determinant < 0:
        scale[0] = -scale[0]
        rotation_columns[0] = [-value for value in rotation_columns[0]]
    return translation, quaternion_from_rotation(rotation_columns), scale


def quaternion_from_euler(x_degrees: float, y_degrees: float, z_degrees: float = 0.0) -> list[float]:
    x, y, z = (math.radians(value) * 0.5 for value in (x_degrees, y_degrees, z_degrees))
    cx, sx, cy, sy, cz, sz = math.cos(x), math.sin(x), math.cos(y), math.sin(y), math.cos(z), math.sin(z)
    return normalize_quaternion([
        sx * cy * cz - cx * sy * sz,
        cx * sy * cz + sx * cy * sz,
        cx * cy * sz - sx * sy * cz,
        cx * cy * cz + sx * sy * sz,
    ])


def extract_zip(archive: Path, destination: Path) -> None:
    """Extract a downloaded model archive, refusing anything that could escape `destination`."""
    try:
        with zipfile.ZipFile(archive) as bundle:
            members = bundle.infolist()
            if len(members) > ZIP_MAX_MEMBERS:
                raise ImportFailure(f"Archive has too many entries ({len(members)})")
            if sum(member.file_size for member in members) > ZIP_MAX_UNCOMPRESSED_BYTES:
                raise ImportFailure("Archive expands to more than 4 GB")
            root = destination.resolve()
            for member in members:
                name = member.filename.replace("\\", "/")
                parts = Path(name).parts
                if name.startswith("/") or ".." in parts or (parts and ":" in parts[0]):
                    raise ImportFailure(f"Archive entry escapes the extraction folder: {member.filename}")
                if (member.external_attr >> 16) & 0o170000 == 0o120000:
                    raise ImportFailure(f"Archive entry is a symbolic link: {member.filename}")
                if not parts or parts[0] == "__MACOSX" or member.is_dir():
                    continue
                target = (root / name).resolve()
                if not target.is_relative_to(root):
                    raise ImportFailure(f"Archive entry escapes the extraction folder: {member.filename}")
                target.parent.mkdir(parents=True, exist_ok=True)
                with bundle.open(member) as source, target.open("wb") as output:
                    shutil.copyfileobj(source, output)
    except zipfile.BadZipFile as error:
        raise ImportFailure(f"'{archive}' is not a valid zip archive: {error}") from error


def discover_models(source: Path) -> tuple[Path, list[Path]]:
    source = source.resolve()
    if source.is_file():
        if source.suffix.lower() not in {".gltf", ".glb"}:
            raise ImportFailure("Source file must be .gltf, .glb, or a .zip containing them")
        return source.parent, [source]
    if not source.is_dir():
        raise ImportFailure(f"Source does not exist: {source}")
    models = sorted(path for path in source.rglob("*") if path.suffix.lower() in {".gltf", ".glb"})
    if not models:
        raise ImportFailure(f"No .gltf or .glb files found under {source}")
    return source, models


class PackImporter:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.project_root = args.project_root.resolve()
        self.assets_root = self.project_root / "Assets"
        self.pack = slug(args.pack)
        self.scene_id = args.scene or f"imported.{self.pack}"
        self.modern = (self.assets_root / "Game/Project.json").exists()
        self.scene_folder = self.assets_root / "Game/Scenes" / slug(self.scene_id)
        self.final_root = self.scene_folder if self.modern else self.assets_root / "Imported" / self.pack
        self.stage_root = self.final_root.with_name(f".{self.final_root.name}.staging")
        self.generated_files: set[str] = set()
        self.catalog_entries: list[dict[str, Any]] = []
        self.texture_outputs: dict[tuple[str, bool], TextureOutput] = {}
        self.normalized_images: dict[tuple[Path, int], tuple[bytes, str]] = {}
        self.material_outputs: dict[str, MaterialOutput] = {}
        self.warnings: list[str] = []
        self.notes: list[str] = []
        self.texcoord_cache: dict[int, list[float] | None] = {}

    def warn(self, message: str) -> None:
        if message not in self.warnings:
            self.warnings.append(message)

    def note(self, message: str) -> None:
        if message not in self.notes:
            self.notes.append(message)

    def run(self) -> int:
        source = Path(self.args.source).expanduser()
        if source.is_file() and source.suffix.lower() == ".zip":
            with tempfile.TemporaryDirectory(prefix="haki-import-") as extracted:
                extract_zip(source, Path(extracted))
                return self.run_source(Path(extracted))
        return self.run_source(source)

    def run_source(self, source: Path) -> int:
        source_root, paths = discover_models(source)
        models = [self.plan_model(path, path.relative_to(source_root)) for path in paths]
        self.place_models(models)
        scene = self.build_scene(models)
        catalog, receipt = self.merge_catalog_preview()
        manifest = self.merge_manifest_preview()

        print(f"Pack: {self.pack}")
        print(f"Models: {len(models)}")
        print(f"Textures: {len(self.texture_outputs)}")
        print(f"Materials: {len(self.material_outputs)}")
        print(f"Entities: {sum(len(model.entities) for model in models)}")
        for note in self.notes:
            print(f"note: {note}")
        for warning in self.warnings:
            print(f"warning: {warning}", file=sys.stderr)
        strict_failure = getattr(self.args, "strict", False) is True and bool(self.warnings)
        if strict_failure:
            print(f"error: --strict: {len(self.warnings)} warning(s); the import would not match the source",
                  file=sys.stderr)
        if self.args.dry_run:
            print("Dry run: no files changed")
            return 2 if strict_failure else 0
        if strict_failure:
            return 2

        if self.modern and self.final_root.exists():
            old = load_json(self.final_root/"ImportReceipt.json") if (self.final_root/"ImportReceipt.json").exists() else {}
            owned = set(old.get("generatedFiles", []))
            planned = {"Catalog.json", "ImportReceipt.json", f"{slug(self.scene_id)}.scene.json"}
            planned.update("Materials/"+o.filename for o in self.material_outputs.values())
            planned.update("Textures/"+o.filename for o in self.texture_outputs.values())
            dependencies = {m.path for m in models}
            for model in models:
                for resource in list(model.gltf.buffers or []) + list(model.gltf.images or []):
                    if resource.uri and not resource.uri.startswith("data:"):
                        dependencies.add(external_uri_path(model.path, resource.uri))
            for file in dependencies:
                planned.add((Path("Models")/file.relative_to(source_root)).as_posix())
            for name in planned:
                if (self.final_root/name).exists() and name not in owned:
                    raise ImportFailure(f"Import would overwrite an authored file: {name}")
        self.write_stage(source_root, models, scene, receipt)
        backup = self.final_root.with_name(f".{self.pack}.backup")
        if backup.exists():
            shutil.rmtree(backup)
        if self.final_root.exists():
            self.final_root.rename(backup)
        self.stage_root.rename(self.final_root)
        try:
            if not self.modern:
                atomic_json_write(self.assets_root / "AssetCatalog.json", catalog)
            if self.modern:
                latest = load_json(self.assets_root / "Game/Project.json")
                registration = next(e for e in manifest["scenes"] if e["id"] == self.scene_id)
                latest["scenes"] = [e for e in latest["scenes"] if e["id"] != self.scene_id] + [registration]
                if self.args.set_entry: latest["entry"] = self.scene_id
                manifest = latest
            atomic_json_write(self.assets_root / "Game/Project.json" if self.modern else self.assets_root / "Scenes/SceneManifest.json", manifest)
        except Exception:
            if self.modern:
                shutil.rmtree(self.final_root)
                if backup.exists(): backup.rename(self.final_root)
            raise
        if backup.exists():
            shutil.rmtree(backup)
        print(f"Imported to {self.final_root}")
        return 0

    def plan_model(self, path: Path, relative_path: Path) -> ModelPlan:
        try:
            gltf = GLTF2().load(str(path))
        except Exception as error:
            raise ImportFailure(f"Could not parse '{path}': {error}") from error
        plan = ModelPlan(path, relative_path, gltf)
        file_slug = slug(relative_path.with_suffix("").as_posix().replace("/", "-"))
        material_ids = self.plan_materials(plan, file_slug)
        mesh_ids: dict[int, str] = {}
        names = [mesh.name for mesh in gltf.meshes or []]
        has_morph_targets = False
        for mesh_index, mesh in enumerate(gltf.meshes or []):
            if not mesh.primitives:
                continue
            mesh_name = mesh.name if mesh.name and names.count(mesh.name) == 1 else f"mesh-{mesh_index}"
            mesh_id = f"{self.pack}.mesh.{file_slug}.{slug(mesh_name)}"
            base_mesh_id = mesh_id
            suffix = 2
            while mesh_id in mesh_ids.values():
                mesh_id = f"{base_mesh_id}-{suffix}"
                suffix += 1
            selector = mesh.name if mesh.name and names.count(mesh.name) == 1 else f"#{mesh_index}"
            mesh_ids[mesh_index] = mesh_id
            self.catalog_entries.append({
                "id": mesh_id,
                "type": "mesh",
                "source": ((Path("Models") if self.modern else Path("Imported") / self.pack / "Source") / relative_path).as_posix(),
                "mesh": selector,
            })
            for primitive in mesh.primitives:
                if primitive.mode not in (None, 4):
                    raise ImportFailure(f"{relative_path}: only triangle primitives are supported")
                if primitive.targets:
                    has_morph_targets = True
                extensions = primitive.extensions or {}
                if "KHR_draco_mesh_compression" in extensions or "EXT_meshopt_compression" in extensions:
                    raise ImportFailure(f"{relative_path}: compressed mesh primitives are not supported")

        self.check_document_extensions(plan)
        roots = self.root_nodes(gltf)
        used_ids: set[str] = set()
        animation_selectors, animation_names = self.validate_animations(plan)
        node_parents = self.node_parents(plan)
        punctual_lights = self.validate_punctual_lights(plan)

        def visit(node_index: int, parent: list[float]) -> None:
            node = gltf.nodes[node_index]
            world = multiply_matrix(parent, node_matrix(node))
            mesh_id: str | None = None
            slots: list[str] = []
            skeleton_id: str | None = None
            clips: dict[str, str] = {}
            if node.mesh is not None:
                if (isinstance(node.mesh, bool) or not isinstance(node.mesh, int) or
                        node.mesh < 0 or node.mesh >= len(gltf.meshes or [])):
                    raise ImportFailure(
                        f"{relative_path}: node '{node.name or node_index}' references missing mesh {node.mesh}"
                    )
                if node.skin is not None:
                    if (isinstance(node.skin, bool) or not isinstance(node.skin, int) or
                            node.skin < 0 or node.skin >= len(gltf.skins or [])):
                        raise ImportFailure(
                            f"{relative_path}: node '{node.name or node_index}' references missing skin {node.skin}"
                        )
                mesh = gltf.meshes[node.mesh]
                validate_inert_morph_defaults(plan, node_index, node.mesh)
                mesh_id = mesh_ids.get(node.mesh)
                if mesh_id is None:
                    raise ImportFailure(f"{relative_path}: node '{node.name or node_index}' references an empty mesh")
                if node.skin is not None:
                    for primitive in mesh.primitives:
                        if (getattr(primitive.attributes, "JOINTS_0", None) is None or
                                getattr(primitive.attributes, "WEIGHTS_0", None) is None):
                            raise ImportFailure(
                                f"{relative_path}: skinned node '{node.name or node_index}' lacks JOINTS_0 or WEIGHTS_0"
                            )
                    baked_mesh_id = f"{mesh_ids[node.mesh]}.node-{node_index}"
                    self.catalog_entries.append({
                        "id": baked_mesh_id,
                        "type": "mesh",
                        "source": ((Path("Models") if self.modern else Path("Imported") / self.pack / "Source") / relative_path).as_posix(),
                        "mesh": (mesh.name if mesh.name and names.count(mesh.name) == 1 else f"#{node.mesh}"),
                        "node": f"#{node_index}",
                    })
                    skeleton_id = f"{self.pack}.skeleton.{file_slug}.skin-{node.skin}.node-{node_index}"
                    skin = gltf.skins[node.skin]
                    self.validate_skin(plan, node.skin, node_index, node_parents)
                    self.catalog_entries.append({
                        "id": skeleton_id,
                        "type": "skeleton",
                        "source": ((Path("Models") if self.modern else Path("Imported") / self.pack / "Source") / relative_path).as_posix(),
                        "skin": self.index_selector(skin.name, node.skin, gltf.skins),
                        "node": f"#{node_index}",
                    })
                    pose_nodes = set(skin.joints or [])
                    for joint in skin.joints or []:
                        ancestor = node_parents.get(joint)
                        while ancestor is not None:
                            pose_nodes.add(ancestor)
                            ancestor = node_parents.get(ancestor)
                    for animation_index, animation in enumerate(gltf.animations or []):
                        targets = {
                            channel.target.node for channel in animation.channels or []
                            if channel.target is not None and channel.target.node is not None
                        }
                        if not targets.intersection(pose_nodes):
                            continue
                        clip_id = (
                            f"{self.pack}.animation-clip.{file_slug}.animation-{animation_index}"
                            f".skin-{node.skin}.node-{node_index}"
                        )
                        self.catalog_entries.append({
                            "id": clip_id,
                            "type": "animationClip",
                            "source": ((Path("Models") if self.modern else Path("Imported") / self.pack / "Source") / relative_path).as_posix(),
                            "animation": animation_selectors[animation_index],
                            "skeleton": skeleton_id,
                        })
                        clips[animation_names[animation_index]] = clip_id
                slots = [material_ids[primitive.material if primitive.material is not None else -1]
                         for primitive in mesh.primitives]
                self.include_mesh_bounds(plan, mesh, world)

            metadata = None
            raw_metadata = (node.extras or {}).get("haki") if isinstance(node.extras, dict) else None
            if isinstance(node.extras, dict) and "haki" in node.extras:
                try:
                    metadata = validate_metadata(raw_metadata)
                    validate_transform(world, metadata["components"])
                except MetadataError as error:
                    raise ImportFailure(f"{relative_path}: node '{node.name or node_index}': {error}") from error
            camera = self.camera_component(plan, node_index)
            light_type, light = self.node_light_component(plan, node_index, punctual_lights)
            if mesh_id is not None or camera is not None or light is not None or metadata is not None:
                entity_base = slug(node.name or f"node-{node_index}")
                entity_id = f"{file_slug}.{entity_base}"
                if metadata is not None:
                    entity_id = metadata["id"]
                    if entity_id in used_ids:
                        raise ImportFailure(f"{relative_path}: duplicate authored ID {entity_id}; repair IDs in Blender")
                suffix = 2
                while entity_id in used_ids:
                    entity_id = f"{file_slug}.{entity_base}-{suffix}"
                    suffix += 1
                used_ids.add(entity_id)
                plan.entities.append(EntityOutput(
                    entity_id=entity_id,
                    name=node.name or entity_id,
                    matrix=world,
                    mesh_id=mesh_id,
                    materials=slots,
                    skeleton_id=skeleton_id,
                    clips=clips,
                    camera=camera,
                    light_type=light_type,
                    light=light,
                    authored_components=metadata["components"] if metadata else {},
                ))
            for child in node.children or []:
                visit(child, world)

        for root in roots:
            visit(root, identity_matrix())
        if not any(entity.mesh_id for entity in plan.entities):
            self.warn(f"{relative_path}: no renderable nodes were found; creating one entity per mesh")
            for mesh_index, mesh in enumerate(gltf.meshes or []):
                if mesh_index not in mesh_ids:
                    continue
                if morph_target_count(plan, mesh_index):
                    mesh_weights = getattr(mesh, "weights", None)
                    weights = list(mesh_weights) if mesh_weights is not None else [0.0] * morph_target_count(plan, mesh_index)
                    if len(weights) != morph_target_count(plan, mesh_index) or any(
                            not math.isfinite(value) or value != 0.0 for value in weights):
                        raise ImportFailure(
                            f"{relative_path}: unreferenced mesh {mesh_index} uses active morph targets; "
                            "runtime morph deformation is not supported"
                        )
                entity_id = f"{file_slug}.{slug(mesh.name or f'mesh-{mesh_index}')}"
                slots = [material_ids[primitive.material if primitive.material is not None else -1]
                         for primitive in mesh.primitives]
                plan.entities.append(EntityOutput(entity_id=entity_id, name=mesh.name or entity_id,
                                                  matrix=identity_matrix(), mesh_id=mesh_ids[mesh_index],
                                                  materials=slots))
                self.include_mesh_bounds(plan, mesh, identity_matrix())
        if has_morph_targets:
            # All weights are proven zero above, so dropping the targets changes nothing visible.
            self.note(f"{relative_path}: morph targets are all at zero weight and were skipped")
        return plan

    def check_document_extensions(self, plan: ModelPlan) -> None:
        """Report extensions the file declares that nothing else in the importer accounts for."""
        used = set(plan.gltf.extensionsUsed or [])
        material_extensions = {name for material in plan.gltf.materials or []
                               for name in (material.extensions or {})}
        for name in sorted(used - KNOWN_DOCUMENT_EXTENSIONS - material_extensions):
            if name in {"KHR_draco_mesh_compression", "EXT_meshopt_compression"}:
                continue  # rejected with a specific error when the compressed primitive is planned
            self.warn(f"{plan.relative_path}: extension {name} is not supported; its data is ignored")
        variants = ((plan.gltf.extensions or {}).get("KHR_materials_variants") or {}).get("variants") or []
        if variants:
            names = ", ".join(str(variant.get("name", f"#{i}")) for i, variant in enumerate(variants))
            self.note(f"{plan.relative_path}: material variants ({names}) are not selectable; using the default")

    def camera_component(self, plan: ModelPlan, node_index: int) -> dict[str, Any] | None:
        node = plan.gltf.nodes[node_index]
        camera_index = getattr(node, "camera", None)
        if camera_index is None:
            return None
        cameras = plan.gltf.cameras or []
        if (isinstance(camera_index, bool) or not isinstance(camera_index, int) or
                camera_index < 0 or camera_index >= len(cameras)):
            raise ImportFailure(
                f"{plan.relative_path}: node '{node.name or node_index}' references missing camera {camera_index}"
            )
        camera = cameras[camera_index]
        camera_type = object_member(camera, "type")
        if camera_type == "orthographic":
            return self.orthographic_camera_component(plan, camera, camera_index)
        if camera_type != "perspective":
            raise ImportFailure(f"{plan.relative_path}: camera {camera_index} has invalid type '{camera_type}'")
        perspective = object_member(camera, "perspective")
        if perspective is None or (not isinstance(perspective, dict) and not hasattr(perspective, "yfov")):
            raise ImportFailure(f"{plan.relative_path}: camera {camera_index} perspective must be an object")
        yfov = finite_number(object_member(perspective, "yfov"),
                             f"{plan.relative_path}: camera {camera_index} perspective.yfov")
        near = finite_number(object_member(perspective, "znear"),
                             f"{plan.relative_path}: camera {camera_index} perspective.znear")
        if yfov <= 0.0 or yfov >= math.pi:
            raise ImportFailure(f"{plan.relative_path}: camera {camera_index} perspective.yfov must be between 0 and pi")
        if near <= 0.0:
            raise ImportFailure(f"{plan.relative_path}: camera {camera_index} perspective.znear must be positive")
        far_value = object_member(perspective, "zfar")
        if far_value is None:
            far = 1000.0
            self.warn(f"{plan.relative_path}: camera {camera_index} has an infinite far plane; using 1000")
        else:
            far = finite_number(far_value, f"{plan.relative_path}: camera {camera_index} perspective.zfar")
            if far <= near:
                raise ImportFailure(
                    f"{plan.relative_path}: camera {camera_index} perspective.zfar must be greater than znear"
                )
        return {"fovYDegrees": math.degrees(yfov), "near": near, "far": far, "primary": False}

    def orthographic_camera_component(self, plan: ModelPlan, camera: Any, camera_index: int) -> dict[str, Any]:
        # Blender's exporter writes ymag = ortho_scale / 2 (half the view height), which is Haki's orthoSize.
        # xmag only encodes the authoring aspect ratio; Haki takes the width from the window instead.
        label = f"{plan.relative_path}: camera {camera_index} orthographic"
        orthographic = object_member(camera, "orthographic")
        if orthographic is None or (not isinstance(orthographic, dict) and not hasattr(orthographic, "ymag")):
            raise ImportFailure(f"{label} must be an object")
        ymag = finite_number(object_member(orthographic, "ymag"), f"{label}.ymag")
        near = finite_number(object_member(orthographic, "znear"), f"{label}.znear")
        far = finite_number(object_member(orthographic, "zfar"), f"{label}.zfar")
        size = abs(ymag)
        if size <= 0.0:
            raise ImportFailure(f"{label}.ymag must not be zero")
        if near < 0.0:
            raise ImportFailure(f"{label}.znear must not be negative")
        if near == 0.0:
            # glTF allows a zero near plane for orthographic cameras; Haki needs a positive one.
            near = min(0.01, far * 0.001)
        if far <= near:
            raise ImportFailure(f"{label}.zfar must be greater than znear")
        return {"projection": "orthographic", "orthoSize": size, "near": near, "far": far, "primary": False}

    def validate_punctual_lights(self, plan: ModelPlan) -> list[tuple[str, dict[str, Any]]]:
        extensions = plan.gltf.extensions or {}
        if not isinstance(extensions, dict):
            raise ImportFailure(f"{plan.relative_path}: glTF extensions must be an object")
        extension = extensions.get("KHR_lights_punctual")
        if extension is None:
            return []
        if not isinstance(extension, dict):
            raise ImportFailure(f"{plan.relative_path}: KHR_lights_punctual must be an object")
        lights = extension.get("lights")
        if not isinstance(lights, list):
            raise ImportFailure(f"{plan.relative_path}: KHR_lights_punctual.lights must be an array")
        parsed: list[tuple[str, dict[str, Any]]] = []
        for index, light in enumerate(lights):
            if not isinstance(light, dict):
                raise ImportFailure(f"{plan.relative_path}: punctual light {index} must be an object")
            light_type = light.get("type")
            if light_type not in {"directional", "point", "spot"}:
                raise ImportFailure(f"{plan.relative_path}: punctual light {index} has invalid type '{light_type}'")
            color = light.get("color", [1.0, 1.0, 1.0])
            if (not isinstance(color, list) or len(color) != 3 or
                    any(isinstance(value, bool) or not isinstance(value, (int, float)) or
                        not math.isfinite(value) or value < 0.0 or value > 1.0 for value in color)):
                raise ImportFailure(
                    f"{plan.relative_path}: punctual light {index} color must contain three numbers between 0 and 1"
                )
            intensity = finite_number(light.get("intensity", 1.0),
                                      f"{plan.relative_path}: punctual light {index} intensity")
            if intensity < 0.0:
                raise ImportFailure(f"{plan.relative_path}: punctual light {index} intensity must not be negative")
            component: dict[str, Any] = {
                "color": [float(value) for value in color],
                "intensity": intensity,
            }
            if light_type in {"point", "spot"}:
                range_value = light.get("range")
                if range_value is None:
                    component["range"] = 10.0
                    self.warn(f"{plan.relative_path}: punctual light {index} has infinite range; using 10")
                else:
                    light_range = finite_number(range_value, f"{plan.relative_path}: punctual light {index} range")
                    if light_range <= 0.0:
                        raise ImportFailure(f"{plan.relative_path}: punctual light {index} range must be positive")
                    component["range"] = light_range
            if light_type == "spot":
                spot = light.get("spot", {})
                if not isinstance(spot, dict):
                    raise ImportFailure(f"{plan.relative_path}: punctual light {index} spot must be an object")
                inner = finite_number(spot.get("innerConeAngle", 0.0),
                                      f"{plan.relative_path}: punctual light {index} innerConeAngle")
                outer = finite_number(spot.get("outerConeAngle", math.pi / 4.0),
                                      f"{plan.relative_path}: punctual light {index} outerConeAngle")
                if inner < 0.0 or inner >= outer or outer > math.pi / 2.0:
                    raise ImportFailure(f"{plan.relative_path}: punctual light {index} has invalid spot cone angles")
                component["innerAngleDegrees"] = math.degrees(inner)
                component["outerAngleDegrees"] = math.degrees(outer)
            parsed.append((light_type, component))
        return parsed

    @staticmethod
    def node_light_component(
        plan: ModelPlan, node_index: int, lights: list[tuple[str, dict[str, Any]]]
    ) -> tuple[str | None, dict[str, Any] | None]:
        node = plan.gltf.nodes[node_index]
        extensions = node.extensions or {}
        if not isinstance(extensions, dict):
            raise ImportFailure(f"{plan.relative_path}: node '{node.name or node_index}' extensions must be an object")
        extension = extensions.get("KHR_lights_punctual")
        if extension is None:
            return None, None
        if not isinstance(extension, dict):
            raise ImportFailure(
                f"{plan.relative_path}: node '{node.name or node_index}' KHR_lights_punctual must be an object"
            )
        light_index = extension.get("light")
        if (isinstance(light_index, bool) or not isinstance(light_index, int) or
                light_index < 0 or light_index >= len(lights)):
            raise ImportFailure(
                f"{plan.relative_path}: node '{node.name or node_index}' references missing punctual light {light_index}"
            )
        light_type, component = lights[light_index]
        return light_type, dict(component)

    @staticmethod
    def index_selector(name: str | None, index: int, values: list[Any]) -> str:
        return name if name and sum(value.name == name for value in values) == 1 else f"#{index}"

    @staticmethod
    def node_parents(plan: ModelPlan) -> dict[int, int]:
        parents: dict[int, int] = {}
        for parent_index, node in enumerate(plan.gltf.nodes or []):
            for child in node.children or []:
                if child < 0 or child >= len(plan.gltf.nodes or []):
                    raise ImportFailure(f"{plan.relative_path}: node {parent_index} references missing child {child}")
                if child in parents:
                    raise ImportFailure(f"{plan.relative_path}: node {child} has multiple parents")
                parents[child] = parent_index
        return parents

    @staticmethod
    def validate_skin(plan: ModelPlan, skin_index: int, node_index: int, parents: dict[int, int]) -> None:
        skin = plan.gltf.skins[skin_index]
        joints = list(skin.joints or [])
        if not joints:
            raise ImportFailure(f"{plan.relative_path}: skin {skin_index} used by node {node_index} has no joints")
        if len(joints) != len(set(joints)):
            raise ImportFailure(f"{plan.relative_path}: skin {skin_index} contains duplicate joints")
        for joint in joints:
            if joint < 0 or joint >= len(plan.gltf.nodes or []):
                raise ImportFailure(f"{plan.relative_path}: skin {skin_index} references missing joint {joint}")
        if skin.inverseBindMatrices is not None:
            if skin.inverseBindMatrices < 0 or skin.inverseBindMatrices >= len(plan.gltf.accessors or []):
                raise ImportFailure(f"{plan.relative_path}: skin {skin_index} has a missing inverse bind accessor")
            accessor = plan.gltf.accessors[skin.inverseBindMatrices]
            if accessor.type != "MAT4" or accessor.componentType != 5126 or accessor.count != len(joints):
                raise ImportFailure(
                    f"{plan.relative_path}: skin {skin_index} inverse bind matrices must be FLOAT MAT4 per joint"
                )

    @staticmethod
    def validate_animations(plan: ModelPlan) -> tuple[list[str], list[str]]:
        animations = list(plan.gltf.animations or [])
        selectors: list[str] = []
        names: list[str] = []
        used_names: set[str] = set()
        for animation_index, animation in enumerate(animations):
            selectors.append(PackImporter.index_selector(animation.name, animation_index, animations))
            base_name = animation.name.strip() if animation.name and animation.name.strip() else f"animation-{animation_index}"
            name = base_name
            suffix = 2
            while name in used_names:
                name = f"{base_name}-{suffix}"
                suffix += 1
            used_names.add(name)
            names.append(name)
            for channel_index, channel in enumerate(animation.channels or []):
                if channel.sampler < 0 or channel.sampler >= len(animation.samplers or []):
                    raise ImportFailure(
                        f"{plan.relative_path}: animation {animation_index} channel {channel_index} references missing sampler"
                    )
                sampler = animation.samplers[channel.sampler]
                interpolation = (sampler.interpolation or "LINEAR").upper()
                if interpolation not in {"LINEAR", "STEP"}:
                    raise ImportFailure(
                        f"{plan.relative_path}: animation {animation_index} uses unsupported {interpolation} interpolation; "
                        "only LINEAR and STEP are supported"
                    )
                target = channel.target
                path = target.path if target is not None else None
                if path not in {"translation", "rotation", "scale", "weights"}:
                    raise ImportFailure(
                        f"{plan.relative_path}: animation {animation_index} has unsupported target path '{path}'"
                    )
                if target.node is None or target.node < 0 or target.node >= len(plan.gltf.nodes or []):
                    raise ImportFailure(
                        f"{plan.relative_path}: animation {animation_index} channel {channel_index} targets a missing node"
                    )
                if (sampler.input is None or sampler.output is None or sampler.input < 0 or sampler.output < 0
                        or sampler.input >= len(plan.gltf.accessors or [])
                        or sampler.output >= len(plan.gltf.accessors or [])):
                    raise ImportFailure(
                        f"{plan.relative_path}: animation {animation_index} channel {channel_index} has missing accessors"
                    )
                input_accessor = plan.gltf.accessors[sampler.input]
                output_accessor = plan.gltf.accessors[sampler.output]
                if input_accessor.componentType != 5126 or input_accessor.type != "SCALAR":
                    raise ImportFailure(
                        f"{plan.relative_path}: animation {animation_index} input must be a FLOAT SCALAR accessor"
                    )
                if path == "weights":
                    node = plan.gltf.nodes[target.node]
                    if node.mesh is None or node.mesh < 0 or node.mesh >= len(plan.gltf.meshes or []):
                        raise ImportFailure(
                            f"{plan.relative_path}: animation {animation_index} weights channel targets a node without a mesh"
                        )
                    target_count = morph_target_count(plan, node.mesh)
                    if (target_count == 0 or output_accessor.componentType != 5126 or
                            output_accessor.type != "SCALAR" or
                            output_accessor.count != input_accessor.count * target_count):
                        raise ImportFailure(
                            f"{plan.relative_path}: animation {animation_index} weights output has invalid morph data"
                        )
                    values = float_accessor_values(plan, sampler.output)
                    if any(not math.isfinite(value) or value != 0.0 for value in values):
                        raise ImportFailure(
                            f"{plan.relative_path}: animation {animation_index} activates morph targets; "
                            "runtime morph deformation is not supported"
                        )
                    continue
                expected_type = "VEC4" if path == "rotation" else "VEC3"
                if (output_accessor.componentType != 5126 or output_accessor.type != expected_type
                        or output_accessor.count != input_accessor.count):
                    raise ImportFailure(
                        f"{plan.relative_path}: animation {animation_index} {path} output must be FLOAT "
                        f"{expected_type} with one value per input time"
                    )
        return selectors, names

    @staticmethod
    def root_nodes(gltf: Any) -> list[int]:
        if gltf.scenes:
            scene_index = gltf.scene if gltf.scene is not None else 0
            if isinstance(scene_index, bool) or not isinstance(scene_index, int) or scene_index < 0 or scene_index >= len(gltf.scenes):
                raise ImportFailure(f"glTF references missing scene {scene_index}")
            roots = list(gltf.scenes[scene_index].nodes or [])
            if any(isinstance(index, bool) or not isinstance(index, int) or index < 0 or index >= len(gltf.nodes or [])
                   for index in roots):
                raise ImportFailure(f"glTF scene {scene_index} references a missing root node")
            return roots
        children = {child for node in gltf.nodes or [] for child in (node.children or [])}
        return [index for index in range(len(gltf.nodes or [])) if index not in children]

    def include_mesh_bounds(self, plan: ModelPlan, mesh: Any, matrix: list[float]) -> None:
        for primitive in mesh.primitives:
            position_index = getattr(primitive.attributes, "POSITION", None)
            if position_index is None:
                continue
            accessor = plan.gltf.accessors[position_index]
            if not accessor.min or not accessor.max:
                self.warn(f"{plan.relative_path}: POSITION bounds are missing; grid estimate may be inaccurate")
                continue
            for x in (accessor.min[0], accessor.max[0]):
                for y in (accessor.min[1], accessor.max[1]):
                    for z in (accessor.min[2], accessor.max[2]):
                        point = transform_point(matrix, [x, y, z])
                        plan.minimum = [min(plan.minimum[i], point[i]) for i in range(3)]
                        plan.maximum = [max(plan.maximum[i], point[i]) for i in range(3)]

    def plan_materials(self, plan: ModelPlan, file_slug: str) -> dict[int, str]:
        outputs: dict[int, str] = {}
        used_indices = {primitive.material for mesh in (plan.gltf.meshes or []) for primitive in mesh.primitives}
        if None in used_indices:
            outputs[-1] = self.create_material(plan, file_slug, -1, None)
        for index in sorted(value for value in used_indices if value is not None):
            if index >= len(plan.gltf.materials or []):
                raise ImportFailure(f"{plan.relative_path}: primitive references missing material {index}")
            outputs[index] = self.create_material(plan, file_slug, index, plan.gltf.materials[index])
        return outputs

    def create_material(self, plan: ModelPlan, file_slug: str, index: int, material: Any) -> str:
        pbr = material.pbrMetallicRoughness if material and material.pbrMetallicRoughness else None
        emissive = list((material.emissiveFactor if material else None) or [0.0, 0.0, 0.0])
        extensions = material.extensions if material and material.extensions else {}
        spec_gloss_enabled = "KHR_materials_pbrSpecularGlossiness" in extensions
        spec_gloss = self.material_extension(
            plan, index, extensions, "KHR_materials_pbrSpecularGlossiness"
        )
        transmission = self.material_extension(plan, index, extensions, "KHR_materials_transmission")
        volume = self.material_extension(plan, index, extensions, "KHR_materials_volume")
        ior_extension = self.material_extension(plan, index, extensions, "KHR_materials_ior")
        emissive_strength = self.material_extension(plan, index, extensions, "KHR_materials_emissive_strength")
        specular_extension = self.material_extension(plan, index, extensions, "KHR_materials_specular")
        clearcoat = self.material_extension(plan, index, extensions, "KHR_materials_clearcoat")
        material_label = f"material '{material.name}'" if material and material.name else f"material {index}"
        strength = self.extension_number(
            plan, index, "KHR_materials_emissive_strength", emissive_strength, "emissiveStrength", 1.0, 0.0
        )
        # Haki's emissive is unbounded HDR, so the strength folds straight into the factor.
        emissive = [float(component) * strength for component in emissive]
        if spec_gloss_enabled and any(
            name in extensions for name in ("KHR_materials_transmission", "KHR_materials_volume", "KHR_materials_ior")
        ):
            raise ImportFailure(
                f"{plan.relative_path}: material {index} KHR_materials_pbrSpecularGlossiness cannot be combined "
                "with transmission, volume, or IOR"
            )
        if spec_gloss_enabled:
            base_factor = self.extension_vec4(
                plan, index, "KHR_materials_pbrSpecularGlossiness", spec_gloss, "diffuseFactor",
                [1.0, 1.0, 1.0, 1.0]
            )
            specular_factor = self.extension_vec3(
                plan, index, "KHR_materials_pbrSpecularGlossiness", spec_gloss, "specularFactor",
                [1.0, 1.0, 1.0]
            )
            glossiness_factor = self.extension_number(
                plan, index, "KHR_materials_pbrSpecularGlossiness", spec_gloss, "glossinessFactor",
                1.0, 0.0, 1.0
            )
            base_color_texture = self.extension_texture(
                plan, index, "KHR_materials_pbrSpecularGlossiness", spec_gloss, "diffuseTexture"
            )
            metallic_roughness_texture = self.extension_texture(
                plan, index, "KHR_materials_pbrSpecularGlossiness", spec_gloss,
                "specularGlossinessTexture"
            )
        else:
            base_factor = list((pbr.baseColorFactor if pbr else None) or [1.0, 1.0, 1.0, 1.0])
            specular_factor = [1.0, 1.0, 1.0]
            glossiness_factor = 1.0
            base_color_texture = pbr.baseColorTexture if pbr else None
            metallic_roughness_texture = pbr.metallicRoughnessTexture if pbr else None
        metallic = float(pbr.metallicFactor if pbr and pbr.metallicFactor is not None else 1.0)
        roughness = float(pbr.roughnessFactor if pbr and pbr.roughnessFactor is not None else 1.0)
        if "KHR_materials_clearcoat" in extensions:
            coat = self.extension_number(plan, index, "KHR_materials_clearcoat", clearcoat, "clearcoatFactor",
                                         0.0, 0.0, 1.0)
            coat_roughness = self.extension_number(plan, index, "KHR_materials_clearcoat", clearcoat,
                                                   "clearcoatRoughnessFactor", 0.0, 0.0, 1.0)
            if coat > 0.5:
                # Keep some of the coat's gloss since the separate layer is not rendered.
                roughness = min(roughness, roughness + (coat_roughness - roughness) * coat * 0.5)
            if coat > 0.0:
                self.note(f"{plan.relative_path}: {material_label} clearcoat layer (factor {coat:g}) "
                          "is approximated by the base layer")
        if "KHR_materials_sheen" in extensions:
            self.note(f"{plan.relative_path}: {material_label} sheen is not rendered (fabric highlight omitted)")
        transmission_factor = self.extension_number(
            plan, index, "KHR_materials_transmission", transmission, "transmissionFactor", 0.0, 0.0, 1.0
        )
        thickness_factor = self.extension_number(
            plan, index, "KHR_materials_volume", volume, "thicknessFactor", 0.0, 0.0
        )
        attenuation_distance = self.extension_number(
            plan, index, "KHR_materials_volume", volume, "attenuationDistance", 0.0, 0.0, strict_minimum=True
        )
        ior = self.extension_number(plan, index, "KHR_materials_ior", ior_extension, "ior", 1.5, 0.0)
        if 0.0 < ior < 1.0:
            raise ImportFailure(f"{plan.relative_path}: material {index} KHR_materials_ior.ior must be 0 or at least 1")
        attenuation_color = self.extension_vec3(
            plan, index, "KHR_materials_volume", volume, "attenuationColor", [1.0, 1.0, 1.0]
        )
        transmission_texture = self.extension_texture(
            plan, index, "KHR_materials_transmission", transmission, "transmissionTexture"
        )
        thickness_texture = self.extension_texture(
            plan, index, "KHR_materials_volume", volume, "thicknessTexture"
        )
        alpha_mode = (material.alphaMode if material else None) or "OPAQUE"
        alpha_value = {"OPAQUE": 0, "MASK": 1, "BLEND": 2}.get(alpha_mode)
        if alpha_value is None:
            raise ImportFailure(f"{plan.relative_path}: unsupported alpha mode {alpha_mode}")
        parameters: dict[str, Any] = {
            "BaseColorFactor": typed("vec4", base_factor),
            "UseBaseColorTexture": typed("int", 0),
            "PbrWorkflow": typed("int", 1 if spec_gloss_enabled else 0),
            "SpecularFactor": typed("vec3", specular_factor),
            "GlossinessFactor": typed("float", glossiness_factor),
            "MetallicFactor": typed("float", metallic),
            "RoughnessFactor": typed("float", roughness),
            "UseMetallicRoughnessTexture": typed("int", 0),
            "UseNormalTexture": typed("int", 0),
            "NormalScale": typed("float", float(material.normalTexture.scale if material and material.normalTexture and material.normalTexture.scale is not None else 1.0)),
            "EmissiveFactor": typed("vec3", emissive),
            "UseEmissiveTexture": typed("int", 0),
            "UseOcclusionTexture": typed("int", 0),
            "OcclusionStrength": typed("float", float(material.occlusionTexture.strength if material and material.occlusionTexture and material.occlusionTexture.strength is not None else 1.0)),
            "AlphaMode": typed("int", alpha_value),
            "AlphaCutoff": typed("float", float(material.alphaCutoff if material and material.alphaCutoff is not None else 0.5)),
            "TransmissionFactor": typed("float", transmission_factor),
            "UseTransmissionTexture": typed("int", 0),
            "ThicknessFactor": typed("float", thickness_factor),
            "UseThicknessTexture": typed("int", 0),
            "AttenuationColor": typed("vec3", attenuation_color),
            "AttenuationDistance": typed("float", attenuation_distance),
            "Ior": typed("float", ior),
        }
        normal_texture = material.normalTexture if material else None
        if normal_texture and any(
            primitive.material == index
            and (getattr(primitive.attributes, "NORMAL", None) is None
                 or getattr(primitive.attributes, "TEXCOORD_0", None) is None)
            for mesh in (plan.gltf.meshes or [])
            for primitive in mesh.primitives
        ):
            self.warn(
                f"{plan.relative_path}: normal texture on material {index} omitted because a primitive lacks normals or TEXCOORD_0"
            )
            normal_texture = None
        texture_specs = [
            (base_color_texture, "BaseColorTexture", "UseBaseColorTexture", True),
            (metallic_roughness_texture, "MetallicRoughnessTexture", "UseMetallicRoughnessTexture",
             spec_gloss_enabled),
            (normal_texture, "NormalTexture", "UseNormalTexture", False),
            (material.emissiveTexture if material else None, "EmissiveTexture", "UseEmissiveTexture", True),
            (material.occlusionTexture if material else None, "OcclusionTexture", "UseOcclusionTexture", False),
            (transmission_texture, "TransmissionTexture", "UseTransmissionTexture", False),
            (thickness_texture, "ThicknessTexture", "UseThicknessTexture", False),
        ]
        for info, parameter_name, flag_name, srgb in texture_specs:
            texture_index = info.get("index") if isinstance(info, dict) else getattr(info, "index", None)
            if info is None or texture_index is None:
                continue
            if isinstance(texture_index, bool) or not isinstance(texture_index, int) or texture_index < 0:
                raise ImportFailure(f"{plan.relative_path}: {parameter_name} has invalid texture index")
            tex_coord = info.get("texCoord", 0) if isinstance(info, dict) else getattr(info, "texCoord", 0)
            info_extensions = (info.get("extensions") if isinstance(info, dict)
                               else getattr(info, "extensions", None)) or {}
            uv_matrix: list[float] | None = None
            if "KHR_texture_transform" in info_extensions:
                uv_matrix, override = self.texture_transform(plan, parameter_name, info_extensions["KHR_texture_transform"])
                if override is not None:
                    tex_coord = override
            if tex_coord is not None and (
                isinstance(tex_coord, bool) or not isinstance(tex_coord, int) or tex_coord < 0
            ):
                raise ImportFailure(f"{plan.relative_path}: {parameter_name} has invalid texCoord")
            uv_set = self.resolve_uv_set(plan, index, tex_coord or 0)
            if uv_set is None:
                self.warn(f"{plan.relative_path}: {material_label} {parameter_name} uses a distinct TEXCOORD_{tex_coord}; "
                          "only two UV sets are supported, texture omitted")
                continue
            if uv_set == 1 and parameter_name == "NormalTexture" and self.material_lacks_tangents(plan, index):
                self.warn(f"{plan.relative_path}: {material_label} normal map uses a distinct TEXCOORD_1 without "
                          "authored tangents; tangents are generated from TEXCOORD_0, so normal detail may be skewed")
            if texture_index >= len(plan.gltf.textures or []):
                raise ImportFailure(f"{plan.relative_path}: {parameter_name} references missing texture {texture_index}")
            texture = plan.gltf.textures[texture_index]
            if texture.source is None:
                self.warn(f"{plan.relative_path}: texture {texture_index} has no standard image source")
                continue
            if texture.source >= len(plan.gltf.images or []):
                raise ImportFailure(f"{plan.relative_path}: texture {texture_index} references missing image {texture.source}")
            texture_id = self.create_texture(plan, file_slug, texture.source, parameter_name, srgb)
            parameters[parameter_name] = typed("texture2D", texture_id)
            parameters[flag_name] = typed("int", 1)
            if uv_set == 1:
                parameters[f"{parameter_name}TexCoord"] = typed("int", 1)
            if uv_matrix is not None:
                parameters[f"{parameter_name}UvTransform"] = typed("mat3", uv_matrix)
        if "KHR_materials_specular" in extensions and not spec_gloss_enabled:
            specular_strength = self.extension_number(plan, index, "KHR_materials_specular", specular_extension,
                                                      "specularFactor", 1.0, 0.0, 1.0)
            specular_color = specular_extension.get("specularColorFactor", [1.0, 1.0, 1.0])
            if (not isinstance(specular_color, list) or len(specular_color) != 3 or
                    any(isinstance(value, bool) or not isinstance(value, (int, float)) or
                        not math.isfinite(value) or value < 0.0 for value in specular_color)):
                raise ImportFailure(f"{plan.relative_path}: material {index} KHR_materials_specular."
                                    "specularColorFactor must be three non-negative numbers")
            parameters["DielectricSpecular"] = typed(
                "vec3", [float(value) * specular_strength for value in specular_color])
            if "specularTexture" in specular_extension or "specularColorTexture" in specular_extension:
                self.note(f"{plan.relative_path}: {material_label} specular textures are approximated by their factors")
        if "KHR_materials_unlit" in extensions:
            # Unlit: no lighting response at all, so show the base colour as pure emission.
            parameters["EmissiveFactor"] = typed("vec3", [float(value) for value in base_factor[:3]])
            parameters["BaseColorFactor"] = typed("vec4", [0.0, 0.0, 0.0, float(base_factor[3])])
            if "BaseColorTexture" in parameters:
                parameters["EmissiveTexture"] = parameters["BaseColorTexture"]
                parameters["UseEmissiveTexture"] = typed("int", 1)
                for suffix in ("TexCoord", "UvTransform"):
                    if f"BaseColorTexture{suffix}" in parameters:
                        parameters[f"EmissiveTexture{suffix}"] = parameters[f"BaseColorTexture{suffix}"]
            parameters["MetallicFactor"] = typed("float", 0.0)
            parameters["RoughnessFactor"] = typed("float", 1.0)
            parameters["DielectricSpecular"] = typed("vec3", [0.0, 0.0, 0.0])
            for name in ("UseMetallicRoughnessTexture", "UseNormalTexture", "UseOcclusionTexture"):
                parameters[name] = typed("int", 0)
            self.note(f"{plan.relative_path}: {material_label} is unlit; rendered as emissive")
        if extensions:
            handled = EXACT_MATERIAL_EXTENSIONS | APPROXIMATED_MATERIAL_EXTENSIONS
            unsupported = sorted(set(extensions) - handled)
            if unsupported:
                material_name = material.name or f"material {index}"
                self.warn(
                    f"{plan.relative_path}: material '{material_name}' extensions ignored: {', '.join(unsupported)}"
                )
        document = {
            "type": "haki.material",
            "version": 1,
            "shader": PBR_SHADER_ID,
            "state": {
                "pass": "transmission" if transmission_factor > 0.0 or parameters["UseTransmissionTexture"]["value"] else "default",
                "blend": "alpha" if alpha_mode == "BLEND" else "opaque",
                "cull": "none" if material and material.doubleSided else "back",
                "depthTest": True,
                "depthWrite": alpha_mode != "BLEND",
                "wireframe": False,
            },
            "parameters": parameters,
        }
        digest = short_hash(json_bytes(document))
        if digest in self.material_outputs:
            return self.material_outputs[digest].logical_id
        name = slug(material.name if material and material.name else f"material-{index if index >= 0 else 'default'}")
        logical_id = f"{self.pack}.material.{file_slug}.{name}-{digest}"
        filename = f"{file_slug}-{name}-{digest}.material.json"
        self.material_outputs[digest] = MaterialOutput(logical_id, filename, document)
        self.catalog_entries.append({
            "id": logical_id,
            "type": "material",
            "source": ((Path("Materials") if self.modern else Path("Imported") / self.pack / "Materials") / filename).as_posix(),
        })
        return logical_id

    def texture_transform(self, plan: ModelPlan, parameter_name: str,
                          transform: Any) -> tuple[list[float], int | None]:
        """KHR_texture_transform -> column-major mat3 (translation * rotation * scale) and texCoord override."""
        if not isinstance(transform, dict):
            raise ImportFailure(f"{plan.relative_path}: {parameter_name} KHR_texture_transform must be an object")

        def pair(name: str, default: list[float]) -> list[float]:
            value = transform.get(name, default)
            if (not isinstance(value, list) or len(value) != 2 or
                    any(isinstance(v, bool) or not isinstance(v, (int, float)) or not math.isfinite(v) for v in value)):
                raise ImportFailure(f"{plan.relative_path}: {parameter_name} KHR_texture_transform.{name} "
                                    "must be two finite numbers")
            return [float(v) for v in value]

        offset = pair("offset", [0.0, 0.0])
        scale = pair("scale", [1.0, 1.0])
        rotation = finite_number(transform.get("rotation", 0.0),
                                 f"{plan.relative_path}: {parameter_name} KHR_texture_transform.rotation")
        override = transform.get("texCoord")
        if override is not None and (isinstance(override, bool) or not isinstance(override, int) or override < 0):
            raise ImportFailure(f"{plan.relative_path}: {parameter_name} KHR_texture_transform.texCoord is invalid")
        cosine, sine = math.cos(rotation), math.sin(rotation)
        # Exactly the spec's sample: translation * rotation * scale, where (column-major)
        # rotation = mat3(cos, sin, 0, -sin, cos, 0, 0, 0, 1).
        matrix = [
            cosine * scale[0], sine * scale[0], 0.0,
            -sine * scale[1], cosine * scale[1], 0.0,
            offset[0], offset[1], 1.0,
        ]
        return matrix, override

    def texcoord_values(self, plan: ModelPlan, accessor_index: int | None) -> list[float] | None:
        """Decoded UVs for comparison (FLOAT or normalized UNSIGNED_BYTE/SHORT); None if not comparable."""
        if accessor_index is None:
            return None
        key = id(plan.gltf) * 1_000_003 + accessor_index
        if key in self.texcoord_cache:
            return self.texcoord_cache[key]
        values: list[float] | None = None
        accessors = plan.gltf.accessors or []
        if 0 <= accessor_index < len(accessors):
            accessor = accessors[accessor_index]
            sparse = accessor.sparse is not None and accessor.sparse.count
            if accessor.type == "VEC2" and accessor.bufferView is not None and not sparse:
                if accessor.componentType == 5126:
                    values = float_accessor_values(plan, accessor_index)
                elif accessor.componentType in (5121, 5123) and accessor.normalized:
                    view = plan.gltf.bufferViews[accessor.bufferView]
                    data = buffer_bytes(plan.gltf, plan.path, view.buffer)
                    size, code, maximum = (1, "B", 255.0) if accessor.componentType == 5121 else (2, "H", 65535.0)
                    stride = view.byteStride or size * 2
                    offset = (view.byteOffset or 0) + (accessor.byteOffset or 0)
                    values = []
                    for element in range(accessor.count):
                        pair_values = struct.unpack_from(f"<2{code}", data, offset + element * stride)
                        values.extend(value / maximum for value in pair_values)
        self.texcoord_cache[key] = values
        return values

    def resolve_uv_set(self, plan: ModelPlan, material_index: int, tex_coord: int) -> int | None:
        """Map a glTF texCoord onto the engine's two UV sets.

        Exporters (notably Blender via Sketchfab) often duplicate UV maps, so a texture on TEXCOORD_n
        whose data matches TEXCOORD_0 (or TEXCOORD_1) for every primitive using the material is remapped
        losslessly. A genuinely distinct set above 1 cannot be represented and returns None.
        """
        if tex_coord == 0:
            return 0
        primitives = [primitive for mesh in plan.gltf.meshes or [] for primitive in mesh.primitives
                      if primitive.material == material_index]

        def same_as(target: int) -> bool:
            for primitive in primitives:
                source = getattr(primitive.attributes, f"TEXCOORD_{tex_coord}", None)
                reference = getattr(primitive.attributes, f"TEXCOORD_{target}", None)
                if source is None or source == reference:
                    continue
                left, right = self.texcoord_values(plan, source), self.texcoord_values(plan, reference)
                if left is None or right is None or len(left) != len(right):
                    return False
                if any(abs(a - b) > 1e-6 for a, b in zip(left, right)):
                    return False
            return True

        if same_as(0):
            return 0
        if tex_coord == 1 or same_as(1):
            return 1
        return None

    @staticmethod
    def material_lacks_tangents(plan: ModelPlan, material_index: int) -> bool:
        return any(getattr(primitive.attributes, "TANGENT", None) is None
                   for mesh in plan.gltf.meshes or [] for primitive in mesh.primitives
                   if primitive.material == material_index)

    @staticmethod
    def material_extension(
        plan: ModelPlan, index: int, extensions: dict[str, Any], name: str
    ) -> dict[str, Any]:
        value = extensions.get(name, {})
        if not isinstance(value, dict):
            raise ImportFailure(f"{plan.relative_path}: material {index} {name} must be an object")
        return value

    @staticmethod
    def extension_number(
        plan: ModelPlan,
        index: int,
        extension_name: str,
        extension: dict[str, Any],
        property_name: str,
        default: float,
        minimum: float,
        maximum: float | None = None,
        strict_minimum: bool = False,
    ) -> float:
        if property_name not in extension:
            return default
        value = extension.get(property_name, default)
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
            raise ImportFailure(
                f"{plan.relative_path}: material {index} {extension_name}.{property_name} must be a finite number"
            )
        if (value <= minimum if strict_minimum else value < minimum) or (maximum is not None and value > maximum):
            bounds = f"greater than {minimum}" if strict_minimum else f"at least {minimum}"
            if maximum is not None:
                bounds = f"between {minimum} and {maximum}"
            raise ImportFailure(
                f"{plan.relative_path}: material {index} {extension_name}.{property_name} must be {bounds}"
            )
        return float(value)

    @staticmethod
    def extension_vec3(
        plan: ModelPlan,
        index: int,
        extension_name: str,
        extension: dict[str, Any],
        property_name: str,
        default: list[float],
    ) -> list[float]:
        value = extension.get(property_name, default)
        if (
            not isinstance(value, (list, tuple))
            or len(value) != 3
            or any(isinstance(component, bool) or not isinstance(component, (int, float))
                   or not math.isfinite(component) or component < 0.0 or component > 1.0
                   for component in value)
        ):
            raise ImportFailure(
                f"{plan.relative_path}: material {index} {extension_name}.{property_name} must be three numbers between 0 and 1"
            )
        return [float(component) for component in value]

    @staticmethod
    def extension_vec4(
        plan: ModelPlan,
        index: int,
        extension_name: str,
        extension: dict[str, Any],
        property_name: str,
        default: list[float],
    ) -> list[float]:
        value = extension.get(property_name, default)
        if (
            not isinstance(value, (list, tuple))
            or len(value) != 4
            or any(isinstance(component, bool) or not isinstance(component, (int, float))
                   or not math.isfinite(component) or component < 0.0 or component > 1.0
                   for component in value)
        ):
            raise ImportFailure(
                f"{plan.relative_path}: material {index} {extension_name}.{property_name} must be four numbers between 0 and 1"
            )
        return [float(component) for component in value]

    @staticmethod
    def extension_texture(
        plan: ModelPlan,
        index: int,
        extension_name: str,
        extension: dict[str, Any],
        property_name: str,
    ) -> dict[str, Any] | None:
        value = extension.get(property_name)
        if value is not None and not isinstance(value, dict):
            raise ImportFailure(
                f"{plan.relative_path}: material {index} {extension_name}.{property_name} must be an object"
            )
        if isinstance(value, dict) and "index" not in value:
            raise ImportFailure(
                f"{plan.relative_path}: material {index} {extension_name}.{property_name} must have an index"
            )
        return value

    def create_texture(self, plan: ModelPlan, file_slug: str, image_index: int, usage: str, srgb: bool) -> str:
        image_key = (plan.path.resolve(), image_index)
        normalized = self.normalized_images.get(image_key)
        if normalized is None:
            source_data, _ = image_bytes(plan.gltf, plan.path, image_index)
            data = normalize_image(source_data)
            normalized = (data, hashlib.sha256(data).hexdigest())
            self.normalized_images[image_key] = normalized
        data, digest = normalized
        extension = ".png"
        key = (digest, srgb)
        if key in self.texture_outputs:
            return self.texture_outputs[key].logical_id
        image = plan.gltf.images[image_index]
        name = slug(image.name or f"image-{image_index}")
        color_tag = "srgb" if srgb else "linear"
        filename = f"{file_slug}-{name}-{digest[:10]}{extension}"
        logical_id = f"{self.pack}.texture.{file_slug}.{name}-{color_tag}-{digest[:10]}"
        self.texture_outputs[key] = TextureOutput(logical_id, filename, data, srgb)
        self.catalog_entries.append({
            "id": logical_id,
            "type": "texture",
            "source": ((Path("Textures") if self.modern else Path("Imported") / self.pack / "Textures") / filename).as_posix(),
            "srgb": srgb,
            "flipY": False,
        })
        return logical_id

    def place_models(self, models: list[ModelPlan]) -> None:
        if not models:
            return
        max_width = max((model.maximum[0] - model.minimum[0]) if math.isfinite(model.minimum[0]) else 2.0 for model in models)
        max_depth = max((model.maximum[2] - model.minimum[2]) if math.isfinite(model.minimum[2]) else 2.0 for model in models)
        spacing = self.args.grid_spacing or max(max_width, max_depth, 2.0) * 1.25
        columns = max(1, math.ceil(math.sqrt(len(models))))
        for index, model in enumerate(models):
            offset = [(index % columns) * spacing, 0.0, (index // columns) * spacing]
            translation = trs_matrix(offset, [0.0, 0.0, 0.0, 1.0], [1.0, 1.0, 1.0])
            for entity in model.entities:
                entity.matrix = multiply_matrix(translation, entity.matrix)

    def build_scene(self, models: list[ModelPlan]) -> dict[str, Any]:
        entities: list[dict[str, Any]] = []
        all_positions: list[list[float]] = []
        entity_ids: set[str] = {"import-camera", "import-sun"}
        imported_camera = False
        imported_light = False
        for model in models:
            for entity in model.entities:
                if entity.entity_id in entity_ids:
                    raise ImportFailure(f"Importer generated a duplicate entity ID: {entity.entity_id}")
                entity_ids.add(entity.entity_id)
                translation, rotation, scale = decompose_matrix(entity.matrix)
                all_positions.append(translation)
                components: dict[str, Any] = {
                    "transform": {"translation": translation, "rotation": rotation, "scale": scale},
                }
                components.update(entity.authored_components)
                if entity.skeleton_id:
                    components["skinnedMeshRenderer"] = {
                        "mesh": entity.mesh_id,
                        "skeleton": entity.skeleton_id,
                        "materials": entity.materials,
                    }
                    if entity.clips:
                        first_clip = next((name for name in entity.clips if name.casefold() == "idle"),
                                          next(iter(entity.clips)))
                        components["animator"] = {
                            "clips": entity.clips,
                            "currentClip": first_clip,
                            "playbackSpeed": 1.0,
                            "playing": True,
                            "loop": True,
                        }
                elif entity.mesh_id:
                    components["meshRenderer"] = {"mesh": entity.mesh_id, "materials": entity.materials}
                if entity.camera is not None:
                    camera = dict(entity.camera)
                    camera["primary"] = not imported_camera
                    imported_camera = True
                    components["camera"] = camera
                if entity.light is not None and entity.light_type is not None:
                    components[f"{entity.light_type}Light"] = entity.light
                    imported_light = True
                entities.append({
                    "id": entity.entity_id,
                    "name": entity.name,
                    "components": components,
                })
        by_id = {e["id"]: e for e in entities}
        for entity in entities:
            components = entity["components"]
            current = entity
            chain = set()
            while "parent" in current["components"]:
                if current["id"] in chain:
                    raise ImportFailure(f"{entity['name']}: runtime parent cycle")
                chain.add(current["id"])
                target = current["components"]["parent"]["entity"]
                if target not in by_id:
                    raise ImportFailure(f"{entity['name']}: missing runtime parent {target}")
                current = by_id[target]
            if "locomotion" in components:
                if "characterController" not in current["components"]:
                    raise ImportFailure(f"{entity['name']}: locomotion requires a character controller ancestor")
                available = components.get("animator", {}).get("clips", {})
                locomotion = components["locomotion"]
                if "blend" in locomotion:
                    wanted = [sample["clip"] for sample in locomotion["blend"]]
                else:
                    wanted = [locomotion[name] for name in ("idleClip", "moveClip")]
                for clip in wanted:
                    if clip not in available:
                        raise ImportFailure(f"{entity['name']}: missing animation '{clip}'; available: {list(available)}")
        center = [sum(point[i] for point in all_positions) / len(all_positions) if all_positions else 0.0 for i in range(3)]
        extent = max([math.dist(point, center) for point in all_positions] + [5.0])
        camera_position = [center[0], center[1] + extent * 0.7, center[2] + extent * 1.8]
        camera_rotation = look_rotation(camera_position, center)
        insert_at = 0
        if not imported_camera:
            entities.insert(insert_at, {
                "id": "import-camera",
                "name": "Import Camera",
                "components": {
                    "transform": {"translation": camera_position, "rotation": camera_rotation, "scale": [1.0, 1.0, 1.0]},
                    "camera": {"fovYDegrees": 60.0, "near": 0.1, "far": max(1000.0, extent * 10.0), "primary": True},
                },
            })
            insert_at += 1
        if not imported_light:
            entities.insert(insert_at, {
                "id": "import-sun",
                "name": "Import Sun",
                "components": {
                    "transform": {"rotation": quaternion_from_euler(-45.0, -30.0)},
                    "directionalLight": {"color": [1.0, 0.95, 0.85], "intensity": 3.0},
                },
            })
        scene = {"type": "haki.scene", "version": 1, "id": self.scene_id, "entities": entities, "prefabs": []}
        if self.modern:
            scene["assetCatalog"] = "Catalog.json"
        scene_environment = None
        has_environment = False
        scene_scripts = []
        for model in models:
            if model.gltf.scenes:
                authored_scene = model.gltf.scenes[model.gltf.scene if model.gltf.scene is not None else 0]
                extras = authored_scene.extras or {}
                if isinstance(extras, dict) and "haki" in extras:
                    try:
                        meta = validate_scene_metadata(extras["haki"])
                    except MetadataError as error:
                        raise ImportFailure(f"{model.relative_path}: {error}") from error
                    if "environment" in meta:
                        has_environment = True
                        scene_environment = meta["environment"]
                    if meta.get("script"):
                        scene_scripts.append(meta["script"])
        if len(scene_scripts) > 1:
            raise ImportFailure("Only one scene script may be authored per imported arena")
        if scene_scripts:
            scene["script"] = scene_scripts[0]
        # Validate sources before replacing a previously successful import.
        script_paths = scene_scripts + [e["components"]["script"]["path"] for e in entities
                                         if "script" in e["components"]]
        script_root = (self.scene_folder / "Scripts" if self.modern else self.project_root / "Assets/Scripts").resolve()
        for script_path in script_paths:
            base = self.assets_root / "Game/Shared/Scripts" if self.modern and script_path.startswith("shared/") else script_root
            resolved = (base / (script_path[7:] if self.modern and script_path.startswith("shared/") else script_path)).resolve()
            if not resolved.is_relative_to(base.resolve()) or not resolved.is_file():
                raise ImportFailure(f"Script does not exist inside Assets/Scripts: {script_path}")
        existing_scene_path = self.final_root / f"{slug(self.scene_id)}.scene.json" if self.modern else self.final_root / "Scenes" / f"{slug(self.scene_id)}.scene.json"
        if existing_scene_path.exists():
            existing_environment = load_json(existing_scene_path).get("environment")
            if isinstance(existing_environment, dict):
                scene["environment"] = existing_environment
        if has_environment:
            scene.pop("environment",None)
            if scene_environment is not None: scene["environment"] = scene_environment
        return scene

    def merge_catalog_preview(self) -> tuple[dict[str, Any], dict[str, Any]]:
        catalog_path = self.final_root / "Catalog.json" if self.modern else self.assets_root / "AssetCatalog.json"
        catalog = load_json(catalog_path) if catalog_path.exists() else {"type":CATALOG_TYPE,"version":1,"assets":[]}
        if self.modern:
            catalog["imports"] = ["../../../Engine/Catalog.json", "../../Shared/Catalog.json"]
        if catalog.get("type") != CATALOG_TYPE or catalog.get("version") != 1 or not isinstance(catalog.get("assets"), list):
            raise ImportFailure(f"Unsupported asset catalog: {catalog_path}")
        old_ids: set[str] = set()
        receipt_path = self.final_root / "ImportReceipt.json"
        if receipt_path.exists():
            receipt = load_json(receipt_path)
            if receipt.get("pack") != self.pack:
                raise ImportFailure(f"Existing import directory is owned by another pack: {self.final_root}")
            old_ids = set(receipt.get("assetIds", []))
        elif self.final_root.exists() and not self.modern and not self.args.force:
            raise ImportFailure(f"Import directory already exists without a receipt; use --force: {self.final_root}")
        preserved = [entry for entry in catalog["assets"] if entry.get("id") not in old_ids]
        generated = ([] if self.modern else [{"id": PBR_SHADER_ID, "type": "shader", "source": "Shaders/GltfPBR"}]) + self.catalog_entries
        preserved_by_id = {entry.get("id"): entry for entry in preserved}
        final_generated: list[dict[str, Any]] = []
        generated_ids: set[str] = set()
        for entry in generated:
            if entry["id"] in generated_ids:
                raise ImportFailure(f"Importer generated a duplicate asset ID: {entry['id']}")
            generated_ids.add(entry["id"])
            existing = preserved_by_id.get(entry["id"])
            if existing:
                if entry["id"] == PBR_SHADER_ID and existing == entry:
                    continue
                raise ImportFailure(f"Generated asset ID conflicts with an unrelated catalog entry: {entry['id']}")
            final_generated.append(entry)
        final_generated.sort(key=lambda entry: entry["id"])
        catalog["assets"] = preserved + final_generated
        receipt = {"type": "haki.import-receipt", "version": 1, "pack": self.pack,
                   "sceneId": self.scene_id,
                   "assetIds": [entry["id"] for entry in final_generated if entry["id"] != PBR_SHADER_ID],
                   "warnings": list(self.warnings)}
        return catalog, receipt

    def merge_manifest_preview(self) -> dict[str, Any]:
        path = self.assets_root / "Game/Project.json" if self.modern else self.assets_root / "Scenes/SceneManifest.json"
        manifest = load_json(path)
        if manifest.get("type") != MANIFEST_TYPE or manifest.get("version") != 1 or not isinstance(manifest.get("scenes"), list):
            raise ImportFailure(f"Unsupported scene manifest: {path}")
        source = (Path("..") / "Imported" / self.pack / "Scenes" / f"{slug(self.scene_id)}.scene.json").as_posix()
        if self.modern:
            source = (Path("Scenes") / slug(self.scene_id) / f"{slug(self.scene_id)}.scene.json").as_posix()
        existing = next((scene for scene in manifest["scenes"] if scene.get("id") == self.scene_id), None)
        if existing and existing.get("source") != source and not self.args.force:
            raise ImportFailure(f"Scene ID already exists with another source: {self.scene_id}")
        manifest["scenes"] = [scene for scene in manifest["scenes"] if scene.get("id") != self.scene_id]
        manifest["scenes"].append({"id": self.scene_id, "source": source})
        if self.args.set_entry:
            manifest["entry"] = self.scene_id
        return manifest

    def write_stage(self, source_root: Path, models: list[ModelPlan], scene: dict[str, Any], receipt: dict[str, Any]) -> None:
        if self.stage_root.exists():
            shutil.rmtree(self.stage_root)
        if self.modern and self.final_root.exists():
            shutil.copytree(self.final_root,self.stage_root)
            old_receipt = load_json(self.final_root/"ImportReceipt.json") if (self.final_root/"ImportReceipt.json").exists() else {}
            for name in old_receipt.get("generatedFiles",[]):
                owned = self.stage_root/name
                if not owned.resolve().is_relative_to(self.stage_root.resolve()):
                    raise ImportFailure(f"Receipt path escapes scene: {name}")
                if owned.is_file(): owned.unlink()
        for directory in (("Models", "Textures", "Materials") if self.modern else ("Source", "Textures", "Materials", "Scenes")):
            (self.stage_root / directory).mkdir(parents=True, exist_ok=True)
        copied: set[Path] = set()
        for model in models:
            self.copy_source_file(model.path, source_root, copied)
            for buffer in model.gltf.buffers or []:
                if buffer.uri and not buffer.uri.startswith("data:"):
                    self.copy_source_file(external_uri_path(model.path, buffer.uri), source_root, copied)
            for image in model.gltf.images or []:
                if image.uri and not image.uri.startswith("data:"):
                    self.copy_source_file(external_uri_path(model.path, image.uri), source_root, copied)
        for candidate in sorted(source_root.iterdir()):
            # Sketchfab downloads ship attribution terms; keep them with the imported source.
            if candidate.is_file() and candidate.name.lower() in {"license.txt", "license.md", "license"}:
                self.copy_source_file(candidate, source_root, copied)
        for output in self.texture_outputs.values():
            (self.stage_root / "Textures" / output.filename).write_bytes(output.data)
        for output in self.material_outputs.values():
            atomic_json_write(self.stage_root / "Materials" / output.filename, output.document)
        if self.modern:
            atomic_json_write(self.stage_root/f"{slug(self.scene_id)}.scene.json",scene)
            atomic_json_write(self.stage_root/"Catalog.json",self.merge_catalog_preview()[0])
            self.generated_files.update([f"{slug(self.scene_id)}.scene.json","Catalog.json","ImportReceipt.json"])
            self.generated_files.update("Textures/"+o.filename for o in self.texture_outputs.values())
            self.generated_files.update("Materials/"+o.filename for o in self.material_outputs.values())
            receipt["generatedFiles"] = sorted(self.generated_files)
        else:
            atomic_json_write(self.stage_root / "Scenes" / f"{slug(self.scene_id)}.scene.json", scene)
        atomic_json_write(self.stage_root / "ImportReceipt.json", receipt)

    def copy_source_file(self, path: Path, source_root: Path, copied: set[Path]) -> None:
        path = path.resolve()
        if path in copied:
            return
        try:
            relative = path.relative_to(source_root.resolve())
        except ValueError as error:
            raise ImportFailure(f"External dependency escapes source package: {path}") from error
        destination = self.stage_root / ("Models" if self.modern else "Source") / relative
        if self.modern: self.generated_files.add((Path("Models")/relative).as_posix())
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, destination)
        copied.add(path)


def typed(type_name: str, value: Any) -> dict[str, Any]:
    return {"type": type_name, "value": value}


def look_rotation(position: list[float], target: list[float]) -> list[float]:
    forward = [target[i] - position[i] for i in range(3)]
    length = math.sqrt(sum(value * value for value in forward)) or 1.0
    forward = [value / length for value in forward]
    yaw = math.degrees(math.atan2(-forward[0], -forward[2]))
    pitch = math.degrees(math.asin(max(-1.0, min(1.0, forward[1]))))
    return quaternion_from_euler(pitch, yaw)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path,
                        help="glTF/GLB file, a directory of them, or a .zip (e.g. a Sketchfab download)")
    parser.add_argument("--pack", required=True, help="logical namespace and output directory")
    parser.add_argument("--scene", help="generated scene ID")
    parser.add_argument("--project-root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--grid-spacing", type=float, help="fixed distance between imported file roots")
    parser.add_argument("--set-entry", action="store_true", help="make the generated scene the startup scene")
    parser.add_argument("--dry-run", action="store_true", help="validate and report without writing files")
    parser.add_argument("--force", action="store_true", help="replace files already owned by this pack")
    parser.add_argument("--strict", action="store_true",
                        help="exit with status 2 if any warning remains (approximation notes are allowed)")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    try:
        return PackImporter(parse_args(argv or sys.argv[1:])).run()
    except ImportFailure as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
