"""Run inside Blender: verify the canyon kit v2 water modules survive building and export.

For water_bank, water_outer and water_inner, and for every generated variant used by a stepped
inlet, it checks the exported GLB:
  * the water surface sits at WATER_TOP and faces up;
  * the land fill (where a module has one) sits at 0 and faces up;
  * the shore lip faces toward the water and up, never into the ground;
  * the module stays inside its 1 x 1 m cell.
"""
import json
from pathlib import Path
import runpy
import struct
import tempfile

import bpy
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[2]
kit = runpy.run_path(str(ROOT / "tools/blender/kits/canyon_kit.py"))
WATER_TOP = kit["WATER_TOP"]


def read_glb(path):
    data = path.read_bytes()
    offset = 12
    chunks = {}
    while offset < len(data):
        length, kind = struct.unpack_from("<II", data, offset)
        chunks[kind] = data[offset + 8:offset + 8 + length]
        offset += 8 + length
    document = json.loads(chunks[0x4E4F534A])
    binary = chunks[0x004E4942]

    def accessor(index):
        value = document["accessors"][index]
        view = document["bufferViews"][value["bufferView"]]
        kind = {5121: "B", 5123: "H", 5125: "I", 5126: "f"}[value["componentType"]]
        width = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[value["type"]]
        fmt = "<" + kind * width
        size = struct.calcsize(fmt)
        start = view.get("byteOffset", 0) + value.get("byteOffset", 0)
        return [struct.unpack_from(fmt, binary, start + i * view.get("byteStride", size)) for i in range(value["count"])]

    return document, accessor


def check(name, obj, directory):
    for other in list(bpy.context.selected_objects):
        other.select_set(False)
    saved = obj.location.copy()
    obj.location = (0, 0, 0)
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    path = Path(directory) / (name + ".glb")
    try:
        bpy.ops.export_scene.gltf(filepath=str(path), use_selection=True, export_yup=True, export_apply=True,
                                  export_cameras=False, export_lights=False)
    finally:
        obj.location = saved
    document, accessor = read_glb(path)
    counts = {"water": 0, "land": 0, "lip": 0}
    for mesh in document["meshes"]:
        for primitive in mesh["primitives"]:
            material = document["materials"][primitive["material"]]["name"]
            positions = accessor(primitive["attributes"]["POSITION"])
            stored = accessor(primitive["attributes"]["NORMAL"])
            indices = [v[0] for v in accessor(primitive["indices"])]
            for x, y, z in positions:  # glTF is Y-up: (x, z, -y) in Blender terms
                assert -0.5001 <= x <= 0.5001 and -0.5001 <= z <= 0.5001, f"{name}: vertex outside its cell"
            for i in range(0, len(indices), 3):
                a, b, c = (Vector(positions[j]) for j in indices[i:i + 3])
                normal = (b - a).cross(c - a)
                if normal.length < 2e-6:  # slivers where the lip profile meets a cell corner
                    continue
                normal.normalize()
                heights = [p.y for p in (a, b, c)]
                if material.endswith("water"):
                    assert all(abs(h - WATER_TOP) < 1e-5 for h in heights), f"{name}: water off its level"
                    assert normal.y > 0.99, f"{name}: water faces down"
                    assert all(stored[j][1] > 0.99 for j in indices[i:i + 3]), f"{name}: exported water normal not up"
                    counts["water"] += 1
                elif material.endswith("sand"):
                    assert all(abs(h) < 1e-5 for h in heights), f"{name}: land fill off the land level"
                    assert normal.y > 0.99, f"{name}: land fill faces down"
                    counts["land"] += 1
                elif material.endswith("lip"):
                    assert normal.y > 0.2, f"{name}: lip top faces down {a} {b} {c}"
                    counts["lip"] += 1
    assert counts["water"] > 0, f"{name}: no water surface"
    return counts


variants = {"water_bank": "N", "water_outer": "NE"}
inlet = {(0, 0), (1, 0), (2, 0), (0, 1), (1, 1), (0, 2)}  # a stepped inlet: banks, outer and inner corners
# a channel along a stepped edge and a plus pool: one-wide runs, ends and mixed corners
inlet |= {(5, y) for y in range(-3, 4)} | {(6, 2), (6, 3), (4, -2)} | {(9, 0), (8, 0), (10, 0), (9, 1), (9, -1)}
library = kit["kit_collection"]()
visibility = (library.hide_viewport, library.hide_render)
library.hide_viewport = library.hide_render = False
try:
  with tempfile.TemporaryDirectory(prefix="canyon-water-check-") as temporary:
    names = ["water_bank", "water_outer", "water_inner"]
    for builder in names:
        kit[builder]()
    for cell in sorted(inlet):
        names.append(kit["water_piece"](inlet, cell))
    for name in dict.fromkeys(names):
        counts = check(name, bpy.data.objects[name], temporary)
        print(name, counts, "passed")
finally:
    library.hide_viewport, library.hide_render = visibility

print("CANYON_WATER_OK")
