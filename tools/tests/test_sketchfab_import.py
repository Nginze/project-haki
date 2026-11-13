"""Sketchfab-style import coverage: zip input, UV-set resolution, and material extensions."""

import base64
import io
import json
import math
import struct
import sys
import tempfile
import unittest
import zipfile
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))

import import_gltf_pack as importer


def png_data_uri() -> str:
    image = importer.Image.new("RGBA", (2, 2), (200, 120, 40, 255))
    output = io.BytesIO()
    image.save(output, format="PNG")
    return "data:image/png;base64," + base64.b64encode(output.getvalue()).decode()


def build_gltf(material: dict, uv_sets: list[list[float]], tangents: bool = False,
               extensions_used: list[str] | None = None, root_extensions: dict | None = None) -> dict:
    """One triangle with the given UV sets (each 6 floats), one embedded texture and one material."""
    positions = [-0.5, -0.5, 0.0, 0.5, -0.5, 0.0, 0.0, 0.5, 0.0]
    normals = [0.0, 0.0, 1.0] * 3
    blob = bytearray()
    views, accessors = [], []

    def add(values: list[float], kind: str, count: int, **extra) -> int:
        offset = len(blob)
        blob.extend(struct.pack(f"<{len(values)}f", *values))
        views.append({"buffer": 0, "byteOffset": offset, "byteLength": len(values) * 4})
        accessors.append({"bufferView": len(views) - 1, "componentType": 5126, "count": count, "type": kind, **extra})
        return len(accessors) - 1

    attributes = {
        "POSITION": add(positions, "VEC3", 3, min=[-0.5, -0.5, 0.0], max=[0.5, 0.5, 0.0]),
        "NORMAL": add(normals, "VEC3", 3),
    }
    for index, uvs in enumerate(uv_sets):
        attributes[f"TEXCOORD_{index}"] = add(uvs, "VEC2", 3)
    if tangents:
        attributes["TANGENT"] = add([1.0, 0.0, 0.0, 1.0] * 3, "VEC4", 3)
    document = {
        "asset": {"version": "2.0"},
        "buffers": [{"byteLength": len(blob),
                     "uri": "data:application/octet-stream;base64," + base64.b64encode(bytes(blob)).decode()}],
        "bufferViews": views,
        "accessors": accessors,
        "images": [{"uri": png_data_uri()}],
        "textures": [{"source": 0}],
        "materials": [material],
        "meshes": [{"name": "Tri", "primitives": [{"attributes": attributes, "material": 0}]}],
        "nodes": [{"name": "Tri", "mesh": 0}],
        "scenes": [{"nodes": [0]}],
        "scene": 0,
    }
    if extensions_used:
        document["extensionsUsed"] = extensions_used
    if root_extensions:
        document["extensions"] = root_extensions
    return document


UV0 = [0.0, 0.0, 1.0, 0.0, 0.5, 1.0]
UV_OTHER = [0.25, 0.25, 0.75, 0.25, 0.5, 0.75]


class SketchfabImportTests(unittest.TestCase):
    def plan(self, document: dict) -> tuple[importer.PackImporter, dict]:
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        root = Path(directory.name)
        source = root / "scene.gltf"
        source.write_text(json.dumps(document), encoding="utf-8")
        pack = importer.PackImporter(importer.parse_args([str(source), "--pack", "sketch", "--project-root", str(root)]))
        pack.plan_model(source, Path("scene.gltf"))
        material = next(iter(pack.material_outputs.values())).document["parameters"]
        return pack, material

    def test_duplicated_uv_sets_remap_to_uv0_without_warnings(self):
        # Mirrors the vintage living room export: textures on TEXCOORD_1/2 whose data equals TEXCOORD_0.
        material = {"name": "Wood",
                    "pbrMetallicRoughness": {"baseColorTexture": {"index": 0},
                                             "metallicRoughnessTexture": {"index": 0, "texCoord": 1}},
                    "normalTexture": {"index": 0, "texCoord": 2}}
        pack, parameters = self.plan(build_gltf(material, [UV0, list(UV0), list(UV0)]))
        self.assertEqual(pack.warnings, [])
        self.assertEqual(parameters["UseMetallicRoughnessTexture"]["value"], 1)
        self.assertEqual(parameters["UseNormalTexture"]["value"], 1)
        self.assertNotIn("MetallicRoughnessTextureTexCoord", parameters)
        self.assertNotIn("NormalTextureTexCoord", parameters)

    def test_distinct_second_uv_set_selects_engine_set_one(self):
        material = {"name": "Baked", "occlusionTexture": {"index": 0, "texCoord": 1}}
        pack, parameters = self.plan(build_gltf(material, [UV0, UV_OTHER]))
        self.assertEqual(pack.warnings, [])
        self.assertEqual(parameters["OcclusionTextureTexCoord"], {"type": "int", "value": 1})

    def test_uv_set_two_matching_set_one_uses_set_one(self):
        material = {"name": "Detail", "occlusionTexture": {"index": 0, "texCoord": 2}}
        pack, parameters = self.plan(build_gltf(material, [UV0, UV_OTHER, list(UV_OTHER)]))
        self.assertEqual(pack.warnings, [])
        self.assertEqual(parameters["OcclusionTextureTexCoord"]["value"], 1)

    def test_genuinely_distinct_third_uv_set_still_warns(self):
        material = {"name": "Many", "occlusionTexture": {"index": 0, "texCoord": 2}}
        pack, parameters = self.plan(build_gltf(material, [UV0, UV_OTHER, [0.9, 0.1, 0.1, 0.9, 0.3, 0.3]]))
        self.assertEqual(parameters["UseOcclusionTexture"]["value"], 0)
        self.assertEqual(len(pack.warnings), 1)
        self.assertIn("TEXCOORD_2", pack.warnings[0])

    def test_normal_map_on_distinct_uv1_warns_only_without_tangents(self):
        material = {"name": "Bumpy", "normalTexture": {"index": 0, "texCoord": 1}}
        pack, _ = self.plan(build_gltf(material, [UV0, UV_OTHER]))
        self.assertTrue(any("tangents" in warning for warning in pack.warnings))
        pack, parameters = self.plan(build_gltf(material, [UV0, UV_OTHER], tangents=True))
        self.assertEqual(pack.warnings, [])
        self.assertEqual(parameters["NormalTextureTexCoord"]["value"], 1)

    def test_emissive_strength_scales_emissive_factor(self):
        material = {"name": "Lamp", "emissiveFactor": [1.0, 0.5, 0.25],
                    "extensions": {"KHR_materials_emissive_strength": {"emissiveStrength": 10.0}}}
        pack, parameters = self.plan(build_gltf(material, [UV0], extensions_used=["KHR_materials_emissive_strength"]))
        self.assertEqual(pack.warnings, [])
        self.assertEqual(parameters["EmissiveFactor"]["value"], [10.0, 5.0, 2.5])

    def test_texture_transform_emits_column_major_matrix(self):
        transform = {"offset": [0.5, 0.25], "scale": [2.0, 3.0], "rotation": math.pi / 2}
        material = {"name": "Tiles", "pbrMetallicRoughness": {
            "baseColorTexture": {"index": 0, "extensions": {"KHR_texture_transform": transform}}}}
        pack, parameters = self.plan(build_gltf(material, [UV0], extensions_used=["KHR_texture_transform"]))
        self.assertEqual(pack.warnings, [])
        matrix = parameters["BaseColorTextureUvTransform"]
        self.assertEqual(matrix["type"], "mat3")
        # KHR_texture_transform README: translation * rotation * scale with
        # rotation = mat3(cos, sin, 0, -sin, cos, 0, 0, 0, 1) (column-major).
        expected = [0.0, 2.0, 0.0, -3.0, 0.0, 0.0, 0.5, 0.25, 1.0]
        for actual, wanted in zip(matrix["value"], expected):
            self.assertAlmostEqual(actual, wanted)

    def test_texture_transform_texcoord_override(self):
        transform = {"texCoord": 1}
        material = {"name": "Override", "occlusionTexture": {
            "index": 0, "texCoord": 0, "extensions": {"KHR_texture_transform": transform}}}
        _, parameters = self.plan(build_gltf(material, [UV0, UV_OTHER]))
        self.assertEqual(parameters["OcclusionTextureTexCoord"]["value"], 1)

    def test_specular_extension_becomes_dielectric_specular(self):
        material = {"name": "Plastic", "extensions": {"KHR_materials_specular": {
            "specularFactor": 0.5, "specularColorFactor": [1.0, 0.8, 0.6]}}}
        pack, parameters = self.plan(build_gltf(material, [UV0], extensions_used=["KHR_materials_specular"]))
        self.assertEqual(pack.warnings, [])
        self.assertEqual(parameters["DielectricSpecular"]["value"], [0.5, 0.4, 0.3])

    def test_unlit_material_renders_as_emissive(self):
        material = {"name": "Sign", "pbrMetallicRoughness": {
            "baseColorFactor": [0.2, 0.4, 0.6, 0.8], "baseColorTexture": {"index": 0}},
            "extensions": {"KHR_materials_unlit": {}}}
        pack, parameters = self.plan(build_gltf(material, [UV0], extensions_used=["KHR_materials_unlit"]))
        self.assertEqual(pack.warnings, [])
        self.assertEqual(parameters["EmissiveFactor"]["value"], [0.2, 0.4, 0.6])
        self.assertEqual(parameters["BaseColorFactor"]["value"], [0.0, 0.0, 0.0, 0.8])
        self.assertEqual(parameters["EmissiveTexture"], parameters["BaseColorTexture"])
        self.assertEqual(parameters["DielectricSpecular"]["value"], [0.0, 0.0, 0.0])
        self.assertTrue(any("unlit" in note for note in pack.notes))

    def test_clearcoat_sheen_and_variants_are_notes_not_warnings(self):
        material = {"name": "Car", "pbrMetallicRoughness": {"roughnessFactor": 0.8},
                    "extensions": {"KHR_materials_clearcoat": {"clearcoatFactor": 1.0, "clearcoatRoughnessFactor": 0.0},
                                   "KHR_materials_sheen": {"sheenColorFactor": [1, 1, 1]}}}
        pack, parameters = self.plan(build_gltf(
            material, [UV0], extensions_used=["KHR_materials_clearcoat", "KHR_materials_sheen", "KHR_materials_variants"],
            root_extensions={"KHR_materials_variants": {"variants": [{"name": "Red"}, {"name": "Blue"}]}}))
        self.assertEqual(pack.warnings, [])
        self.assertLess(parameters["RoughnessFactor"]["value"], 0.8)
        joined = " ".join(pack.notes)
        self.assertIn("clearcoat", joined)
        self.assertIn("sheen", joined)
        self.assertIn("Red, Blue", joined)

    def test_unknown_document_extension_warns(self):
        pack, _ = self.plan(build_gltf({"name": "Plain"}, [UV0], extensions_used=["KHR_mesh_quantization"]))
        self.assertEqual(len(pack.warnings), 1)
        self.assertIn("KHR_mesh_quantization", pack.warnings[0])


class ZipAndStrictTests(unittest.TestCase):
    def project(self) -> Path:
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        root = Path(directory.name)
        scenes = root / "Assets" / "Scenes"
        scenes.mkdir(parents=True)
        (root / "Assets" / "AssetCatalog.json").write_text(
            json.dumps({"type": "haki.asset-catalog", "version": 1, "assets": []}), encoding="utf-8")
        (scenes / "SceneManifest.json").write_text(json.dumps({
            "type": "haki.scene-manifest", "version": 1, "assetCatalog": "../AssetCatalog.json",
            "entry": "empty", "scenes": [{"id": "empty", "source": "Empty.scene.json"}]}), encoding="utf-8")
        return root

    def run_importer(self, *arguments: str) -> tuple[int, str, str]:
        stdout, stderr = io.StringIO(), io.StringIO()
        with redirect_stdout(stdout), redirect_stderr(stderr):
            code = importer.main(list(arguments))
        return code, stdout.getvalue(), stderr.getvalue()

    def test_sketchfab_zip_imports_and_keeps_license(self):
        root = self.project()
        archive = root / "model.zip"
        with zipfile.ZipFile(archive, "w") as bundle:
            bundle.writestr("scene.gltf", json.dumps(build_gltf({"name": "Plain"}, [UV0])))
            bundle.writestr("license.txt", "CC-BY-4.0 attribution")
            bundle.writestr("__MACOSX/._scene.gltf", "junk")
        code, out, err = self.run_importer(str(archive), "--pack", "zipped", "--project-root", str(root), "--strict")
        self.assertEqual(code, 0, err)
        imported = root / "Assets" / "Imported" / "zipped" / "Source"
        self.assertTrue((imported / "scene.gltf").is_file())
        self.assertEqual((imported / "license.txt").read_text(), "CC-BY-4.0 attribution")

    def test_zip_rejects_path_traversal(self):
        root = self.project()
        archive = root / "evil.zip"
        with zipfile.ZipFile(archive, "w") as bundle:
            bundle.writestr("../escape.gltf", "{}")
        code, _, err = self.run_importer(str(archive), "--pack", "evil", "--project-root", str(root), "--dry-run")
        self.assertEqual(code, 1)
        self.assertIn("escapes", err)
        self.assertFalse((root / "escape.gltf").exists())

    def test_strict_fails_on_warnings_but_not_on_notes(self):
        root = self.project()
        noisy = root / "noisy.gltf"
        noisy.write_text(json.dumps(build_gltf({"name": "Plain"}, [UV0], extensions_used=["KHR_mesh_quantization"])))
        code, _, err = self.run_importer(str(noisy), "--pack", "noisy", "--project-root", str(root),
                                         "--dry-run", "--strict")
        self.assertEqual(code, 2)
        self.assertIn("--strict", err)

        quiet = root / "quiet.gltf"
        quiet.write_text(json.dumps(build_gltf(
            {"name": "Coat", "extensions": {"KHR_materials_sheen": {}}}, [UV0],
            extensions_used=["KHR_materials_sheen"])))
        code, out, _ = self.run_importer(str(quiet), "--pack", "quiet", "--project-root", str(root),
                                         "--dry-run", "--strict")
        self.assertEqual(code, 0)
        self.assertIn("note:", out)


if __name__ == "__main__":
    unittest.main()
