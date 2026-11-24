import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

import sys

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))

import import_gltf_pack as importer


class ImporterTests(unittest.TestCase):
    def test_environment_inheritance_opt_out_and_reimport(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            project = root / "Assets/Game/Project.json"
            project.parent.mkdir(parents=True)
            project.write_text(json.dumps({"type": "haki.scene-manifest", "version": 1,
                                           "entry": "", "scenes": []}))
            fixture = TOOLS.parent / "src/Tests/Serialization/Fixtures/Triangle.gltf"
            source = root / "arena.gltf"
            document = json.loads(fixture.read_text())
            document.update(nodes=[{"mesh": 0}], scenes=[{"nodes": [0]}], scene=0)
            source.write_text(json.dumps(document))
            pack = importer.PackImporter(importer.parse_args([
                str(source), "--pack", "arena", "--scene", "arena", "--project-root", str(root)]))
            plan = pack.plan_model(source, Path("arena.gltf"))
            # No authored setting inherits the runtime's engine environment.
            self.assertNotIn("environment", pack.build_scene([plan]))
            authored = plan.gltf.scenes[plan.gltf.scene or 0]
            authored.extras = {"haki": {"version": 1, "environment": None}}
            disabled = pack.build_scene([plan])
            self.assertIn("environment", disabled)
            self.assertIsNone(disabled["environment"])
            existing = pack.final_root / "arena.scene.json"
            existing.parent.mkdir(parents=True)
            existing.write_text(json.dumps(disabled))
            authored.extras = {}
            # A generic model reimport cannot erase the scene's opt-out.
            self.assertIsNone(pack.build_scene([plan])["environment"])
            custom = {"texture": "texture.custom", "intensity": 1.5, "rotationDegrees": 20}
            authored.extras = {"haki": {"version": 1, "environment": custom}}
            self.assertEqual(pack.build_scene([plan])["environment"], custom)
            existing.write_text(json.dumps({**disabled, "environment": custom}))
            authored.extras = {}
            self.assertEqual(pack.build_scene([plan])["environment"], custom)
            # Blender's explicit opt-out overrides a previously enabled environment.
            authored.extras = {"haki": {"version": 1, "environment": None}}
            self.assertIsNone(pack.build_scene([plan])["environment"])

    def test_orthographic_camera_scene_entity(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            fixture = Path(__file__).resolve().parents[2] / "src/Tests/Serialization/Fixtures/Triangle.gltf"
            document = json.loads(fixture.read_text(encoding="utf-8"))
            document["cameras"] = [{"type": "orthographic", "orthographic": {
                "xmag": 16.0, "ymag": 9.0, "znear": 0.0, "zfar": 120.0}}]
            document["nodes"] = [{"name": "TopDown", "camera": 0, "translation": [0, 30, 0]},
                                 {"name": "Tile", "mesh": 0}]
            document["scenes"] = [{"nodes": [0, 1]}]
            document["scene"] = 0
            source = root / "ortho.gltf"
            source.write_text(json.dumps(document), encoding="utf-8")
            pack = importer.PackImporter(importer.parse_args([
                str(source), "--pack", "ortho", "--project-root", str(root)]))
            plan = pack.plan_model(source, Path("ortho.gltf"))
            entities = pack.build_scene([plan])["entities"]
            camera = next(e["components"]["camera"] for e in entities if "camera" in e["components"])
            self.assertEqual(camera["projection"], "orthographic")
            self.assertEqual(camera["orthoSize"], 9.0)
            self.assertGreater(camera["near"], 0.0)
            self.assertEqual(camera["far"], 120.0)
            self.assertTrue(camera["primary"])
            self.assertNotIn("fovYDegrees", camera)

            document["cameras"][0]["orthographic"]["ymag"] = 0.0
            source.write_text(json.dumps(document), encoding="utf-8")
            bad = importer.PackImporter(importer.parse_args([
                str(source), "--pack", "ortho", "--project-root", str(root)]))
            with self.assertRaises(importer.ImportFailure):
                bad.build_scene([bad.plan_model(source, Path("ortho.gltf"))])

    def test_camera_and_punctual_light_scene_entities(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            fixture = Path(__file__).resolve().parents[2] / "src/Tests/Serialization/Fixtures/Triangle.gltf"
            document = json.loads(fixture.read_text(encoding="utf-8"))
            document["meshes"][0]["name"] = "Tile-1"
            document["meshes"].append(json.loads(json.dumps(document["meshes"][0])))
            document["meshes"][1]["name"] = "Tile_1"
            document["cameras"] = [{"type": "perspective", "perspective": {
                "yfov": 1.0, "znear": 0.2, "zfar": 250.0}}]
            document["extensionsUsed"] = ["KHR_lights_punctual"]
            document["extensions"] = {"KHR_lights_punctual": {"lights": [
                {"type": "point", "color": [1, 0.5, 0.2], "intensity": 3, "range": 6},
                {"type": "directional", "intensity": 2},
                {"type": "spot", "range": 8, "spot": {"innerConeAngle": 0.2, "outerConeAngle": 0.6}}
            ]}}
            document["nodes"] = [
                {"name": "Parent", "translation": [10, 0, 0], "children": [1, 2, 3, 4]},
                {"name": "PlayerCamera", "camera": 0, "translation": [0, 2, 5]},
                {"name": "MeshAndPoint", "mesh": 0,
                 "extensions": {"KHR_lights_punctual": {"light": 0}}},
                {"name": "Sun", "extensions": {"KHR_lights_punctual": {"light": 1}}},
                {"name": "Spot", "extensions": {"KHR_lights_punctual": {"light": 2}}},
            ]
            document["scenes"] = [{"nodes": [0]}]
            document["scene"] = 0
            source = root / "authored.gltf"
            source.write_text(json.dumps(document), encoding="utf-8")
            pack = importer.PackImporter(importer.parse_args([
                str(source), "--pack", "authored", "--project-root", str(root)]))
            plan = pack.plan_model(source, Path("authored.gltf"))
            mesh_ids = [e["id"] for e in pack.catalog_entries if e["type"] == "mesh"]
            self.assertEqual(len(mesh_ids), len(set(mesh_ids)))
            entities = pack.build_scene([plan])["entities"]
            self.assertNotIn("import-camera", {e["id"] for e in entities})
            self.assertNotIn("import-sun", {e["id"] for e in entities})
            camera = next(e["components"] for e in entities if "camera" in e["components"])
            self.assertEqual(camera["transform"]["translation"], [10, 2, 5])
            self.assertTrue(camera["camera"]["primary"])
            self.assertAlmostEqual(camera["camera"]["fovYDegrees"], 57.2957795)
            point = next(e["components"] for e in entities if "pointLight" in e["components"])
            self.assertIn("meshRenderer", point)
            self.assertEqual(point["pointLight"]["range"], 6)
            spot = next(e["components"]["spotLight"] for e in entities if "spotLight" in e["components"])
            self.assertAlmostEqual(spot["outerAngleDegrees"], 34.3774677)
            document["nodes"][2]["extensions"]["KHR_lights_punctual"]["light"] = True
            source.write_text(json.dumps(document), encoding="utf-8")
            with self.assertRaisesRegex(importer.ImportFailure, "missing punctual light"):
                pack.plan_model(source, Path("authored.gltf"))

    def test_slug_and_matrix_round_trip(self):
        self.assertEqual(importer.slug("Fancy Chair 01"), "fancy-chair-01")
        matrix = importer.trs_matrix([1.0, 2.0, 3.0], [0.0, 0.0, 0.0, 1.0], [2.0, 3.0, 4.0])
        translation, rotation, scale = importer.decompose_matrix(matrix)
        self.assertEqual(translation, [1.0, 2.0, 3.0])
        self.assertEqual(rotation, [0.0, 0.0, 0.0, 1.0])
        self.assertEqual(scale, [2.0, 3.0, 4.0])

    def test_percent_encoded_data_uri_preserves_binary_bytes(self):
        self.assertEqual(importer.decode_data_uri("data:application/octet-stream,%00%7f%80%ff"), b"\x00\x7f\x80\xff")

    def test_import_and_idempotent_reimport(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            assets = root / "Assets"
            scenes = assets / "Scenes"
            shaders = assets / "Shaders"
            source = root / "download"
            scenes.mkdir(parents=True)
            shaders.mkdir(parents=True)
            source.mkdir(parents=True)
            (assets / "AssetCatalog.json").write_text(
                json.dumps({"type": "haki.asset-catalog", "version": 1, "assets": []}), encoding="utf-8"
            )
            (scenes / "SceneManifest.json").write_text(
                json.dumps({
                    "type": "haki.scene-manifest",
                    "version": 1,
                    "assetCatalog": "../AssetCatalog.json",
                    "entry": "empty",
                    "scenes": [{"id": "empty", "source": "Empty.scene.json"}],
                }),
                encoding="utf-8",
            )
            fixture = Path(__file__).resolve().parents[2] / "src/Tests/Serialization/Fixtures/Triangle.gltf"
            document = json.loads(fixture.read_text(encoding="utf-8"))
            document["nodes"] = [{"name": "Triangle", "mesh": 0}]
            document["scenes"] = [{"nodes": [0]}]
            document["scene"] = 0
            image = importer.Image.new("RGBA", (2, 2))
            image.putdata([
                (255, 0, 0, 255),
                (0, 255, 0, 255),
                (0, 0, 255, 255),
                (255, 255, 255, 255),
            ])
            texture_path = source / "textures" / "corners.png"
            texture_path.parent.mkdir()
            image.save(texture_path, format="PNG")
            document["images"] = [{
                "name": "Corners",
                "uri": "textures/corners.png",
            }]
            document["textures"] = [{"source": 0}]
            document["materials"] = [{
                "name": "Test Material",
                "pbrMetallicRoughness": {
                    "baseColorTexture": {"index": 0},
                    "metallicRoughnessTexture": {"index": 0},
                },
                "alphaMode": "BLEND",
                "extensions": {
                    "KHR_materials_transmission": {
                        "transmissionFactor": 0.75,
                        "transmissionTexture": {"index": 0},
                    },
                    "KHR_materials_volume": {
                        "thicknessFactor": 0.4,
                        "thicknessTexture": {"index": 0},
                        "attenuationColor": [0.2, 0.4, 0.6],
                        "attenuationDistance": 3.5,
                    },
                    "KHR_materials_ior": {"ior": 1.33},
                },
            }]
            document["meshes"][0]["primitives"][0]["material"] = 0
            model = source / "Triangle.gltf"
            model.write_text(json.dumps(document), encoding="utf-8")

            arguments = [str(model), "--pack", "demo", "--scene", "demo-scene",
                         "--project-root", str(root), "--set-entry"]
            with mock.patch.object(importer, "normalize_image", wraps=importer.normalize_image) as normalize:
                self.assertEqual(importer.main(arguments), 0)
                self.assertEqual(normalize.call_count, 1)
            generated_scene = assets / "Imported/demo/Scenes/demo-scene.scene.json"
            authored_scene = json.loads(generated_scene.read_text(encoding="utf-8"))
            authored_scene["environment"] = {
                "texture": "engine.texture.environment.test",
                "intensity": 1.5,
                "rotationDegrees": 30.0,
            }
            generated_scene.write_text(json.dumps(authored_scene), encoding="utf-8")
            with mock.patch.object(importer, "normalize_image", wraps=importer.normalize_image) as normalize:
                self.assertEqual(importer.main(arguments), 0)
                self.assertEqual(normalize.call_count, 1)

            catalog = json.loads((assets / "AssetCatalog.json").read_text(encoding="utf-8"))
            ids = [entry["id"] for entry in catalog["assets"]]
            self.assertEqual(len(ids), len(set(ids)))
            self.assertIn("engine.shader.gltf-pbr", ids)
            self.assertTrue(any(asset_id.startswith("demo.mesh.") for asset_id in ids))
            self.assertTrue(any(asset_id.startswith("demo.material.") for asset_id in ids))
            self.assertTrue(any(asset_id.startswith("demo.texture.") for asset_id in ids))
            textures = [entry for entry in catalog["assets"] if entry["type"] == "texture"]
            self.assertEqual(len(textures), 2)
            self.assertTrue(all(entry["flipY"] is False for entry in textures))
            self.assertEqual(sorted(entry["srgb"] for entry in textures), [False, True])

            material_path = next((assets / "Imported/demo/Materials").glob("*.material.json"))
            generated_material = json.loads(material_path.read_text(encoding="utf-8"))
            parameters = generated_material["parameters"]
            self.assertEqual(generated_material["state"]["pass"], "transmission")
            self.assertEqual(generated_material["state"]["blend"], "alpha")
            self.assertEqual(parameters["TransmissionFactor"], {"type": "float", "value": 0.75})
            self.assertEqual(parameters["UseTransmissionTexture"], {"type": "int", "value": 1})
            self.assertEqual(parameters["ThicknessFactor"], {"type": "float", "value": 0.4})
            self.assertEqual(parameters["UseThicknessTexture"], {"type": "int", "value": 1})
            self.assertEqual(parameters["AttenuationColor"], {"type": "vec3", "value": [0.2, 0.4, 0.6]})
            self.assertEqual(parameters["AttenuationDistance"], {"type": "float", "value": 3.5})
            self.assertEqual(parameters["Ior"], {"type": "float", "value": 1.33})
            self.assertEqual(parameters["TransmissionTexture"]["value"], parameters["ThicknessTexture"]["value"])

            manifest = json.loads((scenes / "SceneManifest.json").read_text(encoding="utf-8"))
            self.assertEqual(manifest["entry"], "demo-scene")
            scene = json.loads(generated_scene.read_text(encoding="utf-8"))
            self.assertEqual(scene["environment"], authored_scene["environment"])
            renderables = [entity for entity in scene["entities"] if "meshRenderer" in entity["components"]]
            self.assertEqual(len(renderables), 1)
            self.assertEqual(len(renderables[0]["components"]["meshRenderer"]["materials"]), 1)
            self.assertEqual(len(list((assets / "Imported/demo/Textures").glob("*.png"))), 1)
            self.assertTrue((assets / "Imported/demo/Source/textures/corners.png").is_file())
            with importer.Image.open(next((assets / "Imported/demo/Textures").glob("*.png"))) as generated:
                self.assertEqual(list(generated.convert("RGBA").getdata()), list(image.getdata()))
            receipt = json.loads((assets / "Imported/demo/ImportReceipt.json").read_text(encoding="utf-8"))
            self.assertEqual(receipt["warnings"], [])

    def test_material_extension_defaults_and_unsupported_warning(self):
        material = mock.Mock()
        material.pbrMetallicRoughness = None
        material.emissiveFactor = None
        material.alphaMode = None
        material.normalTexture = None
        material.emissiveTexture = None
        material.occlusionTexture = None
        material.alphaCutoff = None
        material.doubleSided = False
        material.name = "Defaults"
        material.extensions = {"VENDOR_fake_extension": {}}
        gltf = mock.Mock(meshes=[], textures=[], images=[])
        plan = importer.ModelPlan(Path("model.gltf"), Path("model.gltf"), gltf)
        arguments = mock.Mock(project_root=Path("."), pack="test", scene=None)
        instance = importer.PackImporter(arguments)

        instance.create_material(plan, "model", 0, material)

        generated = next(iter(instance.material_outputs.values())).document
        self.assertEqual(generated["state"]["pass"], "default")
        self.assertEqual(generated["parameters"]["TransmissionFactor"], {"type": "float", "value": 0.0})
        self.assertEqual(generated["parameters"]["UseTransmissionTexture"], {"type": "int", "value": 0})
        self.assertEqual(generated["parameters"]["ThicknessFactor"], {"type": "float", "value": 0.0})
        self.assertEqual(generated["parameters"]["UseThicknessTexture"], {"type": "int", "value": 0})
        self.assertEqual(generated["parameters"]["AttenuationColor"], {"type": "vec3", "value": [1.0, 1.0, 1.0]})
        self.assertEqual(generated["parameters"]["AttenuationDistance"], {"type": "float", "value": 0.0})
        self.assertEqual(generated["parameters"]["Ior"], {"type": "float", "value": 1.5})
        self.assertEqual(generated["parameters"]["PbrWorkflow"], {"type": "int", "value": 0})
        self.assertEqual(generated["parameters"]["SpecularFactor"],
                         {"type": "vec3", "value": [1.0, 1.0, 1.0]})
        self.assertEqual(generated["parameters"]["GlossinessFactor"], {"type": "float", "value": 1.0})
        self.assertEqual(len(instance.warnings), 1)
        self.assertIn("VENDOR_fake_extension", instance.warnings[0])

    def test_specular_glossiness_overrides_core_and_reuses_texture_slots(self):
        pbr = SimpleNamespace(
            baseColorFactor=[0.9, 0.9, 0.9, 0.9],
            baseColorTexture={"index": 99},
            metallicFactor=0.7,
            metallicRoughnessTexture={"index": 99},
            roughnessFactor=0.6,
        )
        material = SimpleNamespace(
            alphaCutoff=None,
            alphaMode=None,
            doubleSided=False,
            emissiveFactor=None,
            emissiveTexture=None,
            extensions={
                "KHR_materials_pbrSpecularGlossiness": {
                    "diffuseFactor": [0.1, 0.2, 0.3, 0.4],
                    "diffuseTexture": {"index": 0},
                    "specularFactor": [0.5, 0.6, 0.7],
                    "glossinessFactor": 0.8,
                    "specularGlossinessTexture": {"index": 0},
                }
            },
            name="Spec Gloss",
            normalTexture=None,
            occlusionTexture=None,
            pbrMetallicRoughness=pbr,
        )
        gltf = SimpleNamespace(
            images=[SimpleNamespace(name="Packed")], meshes=[], textures=[SimpleNamespace(source=0)]
        )
        plan = importer.ModelPlan(Path("model.gltf"), Path("model.gltf"), gltf)
        instance = importer.PackImporter(mock.Mock(project_root=Path("."), pack="test", scene=None))

        with mock.patch.object(instance, "create_texture", return_value="test.texture") as create_texture:
            instance.create_material(plan, "model", 0, material)

        generated = next(iter(instance.material_outputs.values())).document
        parameters = generated["parameters"]
        self.assertEqual(parameters["PbrWorkflow"], {"type": "int", "value": 1})
        self.assertEqual(parameters["BaseColorFactor"],
                         {"type": "vec4", "value": [0.1, 0.2, 0.3, 0.4]})
        self.assertEqual(parameters["SpecularFactor"],
                         {"type": "vec3", "value": [0.5, 0.6, 0.7]})
        self.assertEqual(parameters["GlossinessFactor"], {"type": "float", "value": 0.8})
        self.assertEqual(parameters["BaseColorTexture"]["value"], "test.texture")
        self.assertEqual(parameters["MetallicRoughnessTexture"]["value"], "test.texture")
        self.assertEqual(parameters["UseBaseColorTexture"]["value"], 1)
        self.assertEqual(parameters["UseMetallicRoughnessTexture"]["value"], 1)
        self.assertTrue(all(call.args[-1] for call in create_texture.call_args_list))
        self.assertEqual(instance.warnings, [])

    def test_specular_glossiness_validates_values_and_incompatible_extensions(self):
        base = {
            "alphaCutoff": None,
            "alphaMode": None,
            "doubleSided": False,
            "emissiveFactor": None,
            "emissiveTexture": None,
            "name": "Invalid Spec Gloss",
            "normalTexture": None,
            "occlusionTexture": None,
            "pbrMetallicRoughness": None,
        }
        gltf = SimpleNamespace(images=[], meshes=[], textures=[])
        plan = importer.ModelPlan(Path("model.gltf"), Path("model.gltf"), gltf)

        invalid_extensions = [
            {"KHR_materials_pbrSpecularGlossiness": {"diffuseFactor": [1.0, 1.0, 1.0]}},
            {"KHR_materials_pbrSpecularGlossiness": {"specularFactor": [1.1, 1.0, 1.0]}},
            {"KHR_materials_pbrSpecularGlossiness": {"glossinessFactor": -0.1}},
            {"KHR_materials_pbrSpecularGlossiness": {"diffuseTexture": {}}},
            {"KHR_materials_pbrSpecularGlossiness": {"specularGlossinessTexture": {"index": "0"}}},
            {"KHR_materials_pbrSpecularGlossiness": {}, "KHR_materials_transmission": {}},
            {"KHR_materials_pbrSpecularGlossiness": {}, "KHR_materials_volume": {}},
            {"KHR_materials_pbrSpecularGlossiness": {}, "KHR_materials_ior": {}},
        ]
        for extensions in invalid_extensions:
            with self.subTest(extensions=extensions):
                material = SimpleNamespace(**base, extensions=extensions)
                instance = importer.PackImporter(mock.Mock(project_root=Path("."), pack="test", scene=None))
                with self.assertRaises(importer.ImportFailure):
                    instance.create_material(plan, "model", 0, material)

    def test_skinned_node_emits_skeleton_clip_and_scene_components(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            assets = root / "Assets"
            (assets / "Scenes").mkdir(parents=True)
            (assets / "AssetCatalog.json").write_text(
                json.dumps({"type": "haki.asset-catalog", "version": 1, "assets": []}), encoding="utf-8"
            )
            (assets / "Scenes/SceneManifest.json").write_text(
                json.dumps({
                    "type": "haki.scene-manifest",
                    "version": 1,
                    "assetCatalog": "../AssetCatalog.json",
                    "entry": "empty",
                    "scenes": [],
                }),
                encoding="utf-8",
            )
            fixture = Path(__file__).resolve().parents[2] / "src/Tests/Serialization/Fixtures/Triangle.gltf"
            document = json.loads(fixture.read_text(encoding="utf-8"))
            document["nodes"] = [
                {"name": "Skinned Triangle", "mesh": 0, "skin": 0, "children": [1]},
                {"name": "Root Joint"},
            ]
            document["skins"] = [{"name": "Test Skin", "joints": [1]}]
            document["meshes"][0]["primitives"][0]["attributes"]["JOINTS_0"] = 0
            document["meshes"][0]["primitives"][0]["attributes"]["WEIGHTS_0"] = 0
            document["scenes"] = [{"nodes": [0]}]
            document["scene"] = 0
            document["animations"] = [{
                "name": "Idle",
                "samplers": [{"input": 2, "output": 3, "interpolation": "STEP"}],
                "channels": [{"sampler": 0, "target": {"node": 1, "path": "rotation"}}],
            }]
            document["accessors"].extend([
                {"bufferView": 0, "componentType": 5126, "count": 1, "type": "SCALAR"},
                {"bufferView": 0, "componentType": 5126, "count": 1, "type": "VEC4"},
            ])
            model = root / "Skinned.gltf"
            model.write_text(json.dumps(document), encoding="utf-8")

            arguments = [str(model), "--pack", "skinned", "--project-root", str(root)]
            self.assertEqual(importer.main(arguments), 0)
            first_catalog = json.loads((assets / "AssetCatalog.json").read_text(encoding="utf-8"))
            self.assertEqual(importer.main(arguments), 0)
            receipt = json.loads((assets / "Imported/skinned/ImportReceipt.json").read_text(encoding="utf-8"))
            self.assertEqual(receipt["warnings"], [])
            scene = json.loads(next((assets / "Imported/skinned/Scenes").glob("*.scene.json")).read_text(encoding="utf-8"))
            renderables = [entity for entity in scene["entities"] if "skinnedMeshRenderer" in entity["components"]]
            self.assertEqual(len(renderables), 1)
            catalog = json.loads((assets / "AssetCatalog.json").read_text(encoding="utf-8"))
            self.assertEqual(catalog, first_catalog)
            renderer = renderables[0]["components"]["skinnedMeshRenderer"]
            skinned_entry = next(entry for entry in catalog["assets"]
                                  if entry["id"] == renderer["mesh"])
            self.assertNotIn("node", skinned_entry)
            baked_entry = next(entry for entry in catalog["assets"]
                               if entry["id"] == f'{renderer["mesh"]}.node-0')
            self.assertEqual(baked_entry["node"], "#0")
            skeleton = next(entry for entry in catalog["assets"] if entry["type"] == "skeleton")
            clip = next(entry for entry in catalog["assets"] if entry["type"] == "animationClip")
            self.assertEqual(skeleton["skin"], "Test Skin")
            self.assertEqual(skeleton["node"], "#0")
            self.assertEqual(renderer["skeleton"], skeleton["id"])
            self.assertEqual(clip["animation"], "Idle")
            self.assertEqual(clip["skeleton"], skeleton["id"])
            self.assertEqual(renderables[0]["components"]["animator"], {
                "clips": {"Idle": clip["id"]},
                "currentClip": "Idle",
                "playbackSpeed": 1.0,
                "playing": True,
                "loop": True,
            })

    def test_animation_rejects_cubic_spline_and_active_morph_weights(self):
        animation = SimpleNamespace(
            name="Bad",
            samplers=[SimpleNamespace(interpolation="CUBICSPLINE")],
            channels=[SimpleNamespace(
                sampler=0,
                target=SimpleNamespace(node=0, path="translation"),
            )],
        )
        gltf = SimpleNamespace(
            animations=[animation], nodes=[SimpleNamespace(children=[])],
            accessors=[SimpleNamespace(componentType=5126, type="SCALAR", count=1)],
        )
        plan = importer.ModelPlan(Path("bad.gltf"), Path("bad.gltf"), gltf)
        with self.assertRaisesRegex(importer.ImportFailure, "CUBICSPLINE"):
            importer.PackImporter.validate_animations(plan)

        animation.samplers[0] = SimpleNamespace(interpolation="LINEAR", input=0, output=1)
        animation.channels[0].target.path = "weights"
        gltf.nodes[0].mesh = 0
        gltf.nodes[0].weights = None
        gltf.meshes = [SimpleNamespace(
            weights=[0.0],
            primitives=[SimpleNamespace(targets=[{"POSITION": 0}])],
        )]
        gltf.accessors.append(SimpleNamespace(componentType=5126, type="SCALAR", count=1))
        with mock.patch.object(importer, "float_accessor_values", return_value=[0.0]):
            selectors, names = importer.PackImporter.validate_animations(plan)
        self.assertEqual(selectors, ["Bad"])
        self.assertEqual(names, ["Bad"])

        with mock.patch.object(importer, "float_accessor_values", return_value=[0.5]):
            with self.assertRaisesRegex(importer.ImportFailure, "activates morph targets"):
                importer.PackImporter.validate_animations(plan)

        gltf.nodes[0].weights = [1.0]
        with self.assertRaisesRegex(importer.ImportFailure, "active morph targets"):
            importer.validate_inert_morph_defaults(plan, 0, 0)


if __name__ == "__main__":
    unittest.main()
