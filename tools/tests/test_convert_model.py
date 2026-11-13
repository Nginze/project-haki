import contextlib
import io
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

from PIL import Image
from pygltflib import GLTF2

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))

import convert_model as converter
import import_gltf_pack as importer


OBJ = """\
mtllib triangle.mtl
o Triangle
v 0 0 0
v 1 0 0
v 0 1 0
vt 0 0
vt 1 0
vt 0 1
vn 0 0 1
usemtl Painted
f 1/1/1 2/2/1 3/3/1
"""

MTL = """\
newmtl Painted
Kd 0.8 0.4 0.2
Ns 32
map_Kd albedo.png
"""

FBX = """\
; FBX 7.4.0 project file
FBXHeaderExtension:  {
    FBXHeaderVersion: 1003
    FBXVersion: 7400
    Creator: "Haki test fixture"
}
GlobalSettings:  {
    Version: 1000
    Properties70:  {
        P: "UpAxis", "int", "Integer", "",1
        P: "UpAxisSign", "int", "Integer", "",1
        P: "FrontAxis", "int", "Integer", "",2
        P: "FrontAxisSign", "int", "Integer", "",-1
        P: "CoordAxis", "int", "Integer", "",0
        P: "CoordAxisSign", "int", "Integer", "",1
        P: "UnitScaleFactor", "double", "Number", "",1
    }
}
Objects:  {
    Geometry: 1, "Geometry::Triangle", "Mesh" {
        Vertices: *9 { a: 0,0,0,1,0,0,0,1,0 }
        PolygonVertexIndex: *3 { a: 0,1,-3 }
        LayerElementNormal: 0 {
            Version: 102
            Name: ""
            MappingInformationType: "ByPolygonVertex"
            ReferenceInformationType: "Direct"
            Normals: *9 { a: 0,0,1,0,0,1,0,0,1 }
        }
        LayerElementUV: 0 {
            Version: 101
            Name: "UVSet0"
            MappingInformationType: "ByPolygonVertex"
            ReferenceInformationType: "Direct"
            UV: *6 { a: 0,0,1,0,0,1 }
        }
        LayerElementMaterial: 0 {
            Version: 101
            Name: ""
            MappingInformationType: "AllSame"
            ReferenceInformationType: "IndexToDirect"
            Materials: *1 { a: 0 }
        }
        Layer: 0 {
            Version: 100
            LayerElement:  { Type: "LayerElementNormal" TypedIndex: 0 }
            LayerElement:  { Type: "LayerElementUV" TypedIndex: 0 }
            LayerElement:  { Type: "LayerElementMaterial" TypedIndex: 0 }
        }
    }
    Model: 2, "Model::Triangle", "Mesh" {
        Version: 232
        Properties70:  {
            P: "Lcl Translation", "Lcl Translation", "", "A",0,0,0
            P: "Lcl Rotation", "Lcl Rotation", "", "A",0,0,0
            P: "Lcl Scaling", "Lcl Scaling", "", "A",1,1,1
        }
        Shading: T
        Culling: "CullingOff"
    }
    Material: 3, "Material::TriangleMaterial", "" {
        Version: 102
        ShadingModel: "lambert"
        MultiLayer: 0
        Properties70:  {
            P: "DiffuseColor", "Color", "", "A",0.8,0.2,0.1
            P: "DiffuseFactor", "Number", "", "A",1
        }
    }
}
Connections:  {
    C: "OO",1,2
    C: "OO",3,2
    C: "OO",2,0
}
"""


def write_obj(root: Path) -> Path:
    model = root / "triangle.obj"
    model.write_text(OBJ, encoding="utf-8")
    (root / "triangle.mtl").write_text(MTL, encoding="utf-8")
    Image.new("RGBA", (2, 2), (200, 100, 50, 255)).save(root / "albedo.png")
    return model


def accessor_values(gltf: GLTF2, accessor_index: int) -> tuple[float | int, ...]:
    accessor = gltf.accessors[accessor_index]
    view = gltf.bufferViews[accessor.bufferView]
    components = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}[accessor.type]
    formats = {5126: "f", 5125: "I", 5123: "H"}
    count = accessor.count * components
    offset = (view.byteOffset or 0) + (accessor.byteOffset or 0)
    return struct.unpack_from(f"<{count}{formats[accessor.componentType]}", gltf.binary_blob(), offset)


class ConverterTests(unittest.TestCase):
    def test_obj_to_self_contained_glb(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = write_obj(root)
            output = root / "triangle.glb"

            self.assertEqual(converter.main([str(source), str(output)]), 0)
            gltf = GLTF2().load_binary(str(output))

            self.assertEqual(len(gltf.buffers), 1)
            self.assertIsNone(gltf.buffers[0].uri)
            self.assertEqual(len(gltf.meshes), 1)
            primitive = gltf.meshes[0].primitives[0]
            self.assertEqual(accessor_values(gltf, primitive.indices), (0, 1, 2))
            self.assertEqual(gltf.accessors[primitive.attributes.POSITION].min, [0.0, 0.0, 0.0])
            self.assertEqual(gltf.accessors[primitive.attributes.POSITION].max, [1.0, 1.0, 0.0])
            self.assertIsNotNone(primitive.attributes.NORMAL)
            self.assertIsNotNone(primitive.attributes.TANGENT)
            self.assertIsNotNone(primitive.attributes.TEXCOORD_0)
            self.assertEqual(accessor_values(gltf, primitive.attributes.TEXCOORD_0),
                             (0.0, 1.0, 1.0, 1.0, 0.0, 0.0))
            self.assertEqual(len(gltf.images), 1)
            self.assertIsNotNone(gltf.images[0].bufferView)
            self.assertIsNone(gltf.images[0].uri)
            self.assertEqual(gltf.materials[primitive.material].pbrMetallicRoughness.metallicFactor, 0.0)
            for actual, expected in zip(
                gltf.materials[primitive.material].pbrMetallicRoughness.baseColorFactor[:3],
                [0.8, 0.4, 0.2],
            ):
                self.assertAlmostEqual(actual, expected)

    def test_ascii_fbx_to_glb(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "triangle.fbx"
            output = root / "triangle.glb"
            source.write_text(FBX, encoding="utf-8")

            self.assertEqual(converter.main([str(source), str(output)]), 0)
            gltf = GLTF2().load_binary(str(output))
            self.assertEqual(len(gltf.meshes), 1)
            self.assertEqual(gltf.accessors[gltf.meshes[0].primitives[0].attributes.POSITION].count, 3)
            self.assertTrue(any(node.mesh == 0 for node in gltf.nodes))

    def test_directory_conversion_and_force(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source"
            nested = source / "nested"
            nested.mkdir(parents=True)
            write_obj(nested)
            output = root / "converted"

            self.assertEqual(converter.main([str(source), str(output)]), 0)
            self.assertTrue((output / "nested/triangle.glb").is_file())
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(converter.main([str(source), str(output)]), 1)
            self.assertEqual(converter.main([str(source), str(output), "--force"]), 0)

    def test_converted_glb_imports_as_pack(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = write_obj(root)
            output = root / "triangle.glb"
            self.assertEqual(converter.main([str(source), str(output)]), 0)

            project = root / "project"
            assets = project / "Assets"
            scenes = assets / "Scenes"
            scenes.mkdir(parents=True)
            (assets / "AssetCatalog.json").write_text(
                json.dumps({"type": "haki.asset-catalog", "version": 1, "assets": []}), encoding="utf-8"
            )
            (scenes / "SceneManifest.json").write_text(
                json.dumps({
                    "type": "haki.scene-manifest",
                    "version": 1,
                    "assetCatalog": "../AssetCatalog.json",
                    "entry": "empty",
                    "scenes": [],
                }),
                encoding="utf-8",
            )
            self.assertEqual(importer.main([
                str(output), "--pack", "converted", "--project-root", str(project)
            ]), 0)
            catalog = json.loads((assets / "AssetCatalog.json").read_text(encoding="utf-8"))
            self.assertTrue(any(entry["type"] == "mesh" for entry in catalog["assets"]))
            self.assertTrue(any(entry["type"] == "material" for entry in catalog["assets"]))
            self.assertTrue(any(entry["type"] == "texture" for entry in catalog["assets"]))

    def test_gltf_input_is_rejected_with_actionable_message(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "already.glb"
            source.write_bytes(b"not needed")
            errors = io.StringIO()
            with contextlib.redirect_stderr(errors):
                self.assertEqual(converter.main([str(source), str(root / "out.glb")]), 1)
            self.assertIn("already canonical", errors.getvalue())

    def test_embedded_texture_is_rejected(self):
        assimp = converter.load_assimp()
        model_converter = converter.ModelConverter(Path("fixture.fbx"), assimp)
        with self.assertRaisesRegex(converter.ConversionFailure, "embedded texture"):
            model_converter.first_texture(
                {assimp.TextureType_DIFFUSE: ["*0"]}, assimp.TextureType_DIFFUSE, 0
            )

    def test_skinned_mesh_and_animation_are_exported(self):
        identity = [[1.0, 0.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0],
                    [0.0, 0.0, 1.0, 0.0], [0.0, 0.0, 0.0, 1.0]]
        bone = SimpleNamespace(
            name="RootJoint",
            node_name="RootJoint",
            armature_name="Model",
            offset_matrix=identity,
            weight_vertex_ids=[0, 1, 2],
            weights=[1.0, 1.0, 1.0],
        )
        mesh = SimpleNamespace(
            name="Skinned", num_vertices=3, vertices=[0, 0, 0, 1, 0, 0, 0, 1, 0],
            normals=None, texcoords=[], colors=[], tangents=None, bitangents=None,
            indices=[0, 1, 2], num_faces=1, material_index=0, num_bones=1, bones=[bone],
        )
        joint = SimpleNamespace(name="RootJoint", transformation=identity, mesh_indices=[], children=[])
        root = SimpleNamespace(name="Model", transformation=identity, mesh_indices=[0], children=[joint])
        animation = SimpleNamespace(
            name="Idle", ticks_per_second=24.0, duration=24.0,
            channels=[SimpleNamespace(
                node_name="RootJoint",
                position_key_times=[0.0, 24.0],
                position_key_values=[0, 0, 0, 0, 1, 0],
                rotation_key_times=[], rotation_key_values=[],
                scaling_key_times=[], scaling_key_values=[],
            )],
        )
        imported = SimpleNamespace(
            meshes=[mesh], materials=[{}], root_node=root, animations=[animation]
        )

        class FakeAssimp:
            Process_Triangulate = 1
            Process_JoinIdenticalVertices = 2
            Process_GenSmoothNormals = 4
            Process_CalcTangentSpace = 8
            Process_FlipUVs = 16
            Process_ImproveCacheLocality = 32
            Process_SortByPType = 64
            Process_ValidateDataStructure = 128
            Process_LimitBoneWeights = 256
            Process_PopulateArmatureData = 512
            TextureType_DIFFUSE = 1
            TextureType_OPACITY = 2
            TextureType_NORMALS = 3
            TextureType_HEIGHT = 4
            TextureType_EMISSIVE = 5
            TextureType_LIGHTMAP = 6
            TextureType_SPECULAR = 7
            TextureType_SHININESS = 8
            TextureType_REFLECTION = 9
            TextureType_DISPLACEMENT = 10
            flags = 0

            @classmethod
            def import_file(cls, _source, flags):
                cls.flags = flags
                return imported

        gltf = converter.ModelConverter(Path("fixture.fbx"), FakeAssimp()).convert()
        primitive = gltf.meshes[0].primitives[0]
        self.assertIsNotNone(primitive.attributes.JOINTS_0)
        self.assertIsNotNone(primitive.attributes.WEIGHTS_0)
        self.assertEqual(accessor_values(gltf, primitive.attributes.JOINTS_0), (0,) * 12)
        self.assertEqual(accessor_values(gltf, primitive.attributes.WEIGHTS_0),
                         (1.0, 0.0, 0.0, 0.0) * 3)
        self.assertEqual(gltf.nodes[0].skin, 0)
        self.assertEqual(gltf.skins[0].joints, [1])
        self.assertEqual(gltf.skins[0].skeleton, 0)
        self.assertEqual(accessor_values(gltf, gltf.skins[0].inverseBindMatrices),
                         tuple(value for row in identity for value in row))
        self.assertEqual(gltf.animations[0].name, "Idle")
        self.assertEqual(gltf.animations[0].channels[0].target.path, "translation")
        times = accessor_values(gltf, gltf.animations[0].samplers[0].input)
        self.assertEqual(times, (0.0, 1.0))
        self.assertTrue(FakeAssimp.flags & FakeAssimp.Process_LimitBoneWeights)
        self.assertTrue(FakeAssimp.flags & FakeAssimp.Process_PopulateArmatureData)

    def test_animation_rejects_invalid_ticks_and_times(self):
        model_converter = converter.ModelConverter(Path("fixture.fbx"), object())
        channel = SimpleNamespace(
            node_name="Joint",
            position_keys=[SimpleNamespace(time=1.0, value=[0, 0, 0]),
                           SimpleNamespace(time=1.0, value=[0, 0, 0])],
            rotation_keys=[], scaling_keys=[],
        )
        animation = SimpleNamespace(name="Bad", ticks_per_second=0.0, duration=1.0, channels=[channel])
        with self.assertRaisesRegex(converter.ConversionFailure, "ticks per second"):
            model_converter.convert_animation(animation, 0, {"Joint": 0})
        animation.ticks_per_second = 1.0
        with self.assertRaisesRegex(converter.ConversionFailure, "finite and increasing"):
            model_converter.convert_animation(animation, 0, {"Joint": 0})


if __name__ == "__main__":
    unittest.main()
