# Current asset layout

Current projects use `Assets/Engine` and `Assets/Game/Project.json`. Imports target
`Assets/Game/Scenes/<scene>/`, with an adjacent catalog and receipt. Sources are stored in Models;
materials and textures have one directory each. Reimport replaces only receipt-owned generated files.
The old global catalog/Imported layout is supported only when processing an existing legacy project.
See ../Assets/README.md and blender/README.md for the current workflow.

# Haki Model Tools

The model tools convert source models to canonical GLB and import `.gltf` and `.glb` packages into Haki catalog, material, and scene files. The engine only loads glTF at runtime.

## Setup

```bash
python3.12 -m venv .venv
.venv/bin/python -m pip install -r tools/requirements.txt
```

Python 3.12 or newer is required by `assimp-py`. Its Python wheel bundles Assimp, so Blender and a system Assimp installation are not required.

## Convert

Convert an OBJ/MTL, FBX, or another supported model to a self-contained GLB:

```bash
.venv/bin/python tools/convert_model.py ~/Downloads/chair.obj converted/chair.glb
```

Directories are converted recursively while preserving their relative layout:

```bash
.venv/bin/python tools/convert_model.py ~/Downloads/furniture converted/furniture
```

Use `--dry-run` to load and validate without writing files. Existing output requires `--force`.

The converter exports triangle geometry, positions, generated normals, UV0, tangents, vertex color set 0, hierarchy, transforms, skins, and node TRS animation. Skins contain four-component `JOINTS_0`/`WEIGHTS_0` vertex attributes and inverse bind matrices, and skinned nodes reference their generated skin. Assimp's `LimitBoneWeights` and `PopulateArmatureData` processing are enabled. External textures are normalized to PNG and embedded in the GLB. OBJ and FBX are covered by integration tests; the command also accepts the other model extensions listed by `tools/convert_model.py`.

Bone names must identify unique hierarchy nodes, vertices must have one to four valid influences, and animation names, ticks-per-second, and key times are validated. Embedded FBX textures remain unsupported. Additional UV/color sets and material channels that cannot be represented safely in core glTF PBR are omitted with diagnostics; cameras and lights are ignored. Convert glTF/GLB packages directly with the importer instead of passing them through Assimp.

## Import

```bash
.venv/bin/python tools/import_gltf_pack.py converted/furniture \
    --pack furniture \
    --scene furniture-showcase \
    --set-entry
```

Use `--dry-run` to validate and preview the import without changing files. Re-running the same pack updates only assets recorded in its `ImportReceipt.json` and preserves unrelated catalog entries.

### Sketchfab downloads

Pass the downloaded `.zip` directly; it is extracted to a temporary folder (entries that would escape it,
symlinks, and archives over 4 GB are rejected) and any `license.txt` is kept in the pack's `Source/` folder:

```bash
.venv/bin/python tools/import_gltf_pack.py ~/Downloads/vintage_living_room.zip \
    --pack vintage_living_room --scene vintage_living_room --set-entry --strict
```

Every glTF feature has one of three outcomes:

- **Imported exactly** (silent): core PBR, specular-glossiness, transmission/volume/IOR,
  `KHR_materials_emissive_strength` (folded into the HDR emissive factor), `KHR_texture_transform`, and
  textures on any UV set the engine can represent. The engine renders two UV sets; a texture on
  `TEXCOORD_n` whose data duplicates `TEXCOORD_0` or `TEXCOORD_1` (common in Blender/Sketchfab exports)
  is remapped losslessly.
- **Approximated** (`note:` lines, stdout): `KHR_materials_specular` (factors tint the dielectric
  reflectance; textures ignored), `KHR_materials_unlit` (rendered as emissive), `KHR_materials_clearcoat`
  (base layer only, slightly glossier), `KHR_materials_sheen` (dropped), `KHR_materials_variants`
  (default material), and morph targets whose weights are all zero.
- **Unsupported** (`warning:` lines, stderr): anything that would visibly differ, such as a third distinct
  UV set or an unknown extension. Compressed meshes and `CUBICSPLINE` animation fail the import.

`--strict` exits with status 2 when any warning remains, so a model that will not match its preview is
caught immediately. Notes never fail `--strict`.

The importer supports static and skinned meshes, skeletons, node TRS animation with `LINEAR` or `STEP`
interpolation, core metallic-roughness PBR, `KHR_materials_pbrSpecularGlossiness`, transmission,
volume, and IOR materials. It retains the node-selected baked mesh catalog entry for each skinned
node and additionally emits one `skeleton` entry per skin/node pair and one `animationClip` entry
per compatible animation/skeleton pair. Generated scenes use `skinnedMeshRenderer` and `animator`
components for animated entities. Morph targets are ignored only when every default and animated weight
is provably zero; active morph deformation remains unsupported. `CUBICSPLINE`, compressed geometry,
remote resources, and non-default texture-coordinate sets are rejected or reported explicitly.

Skeleton catalog entries use `source`, `skin`, and `node`; animation clip entries use `source`,
`animation`, and the logical `skeleton` ID. A skinned scene renderer contains `mesh`, `skeleton`, and
`materials`. Its animator contains a name-to-asset `clips` object plus `currentClip`, `playbackSpeed`,
`playing`, and `loop`. Index fallbacks in selectors and logical IDs make these records stable on reimport.

Perspective cameras and `KHR_lights_punctual` directional, point, and spot lights become scene entities,
using their flattened world transforms. The first imported perspective camera is primary. The importer
adds its fallback camera or sun only when no supported camera or light, respectively, was authored.
Orthographic cameras are reported and skipped. Infinite camera far planes use 1000 units; unbounded
point/spot light ranges use 10 units, with diagnostics. glTF intensities are copied numerically; Haki's
finite-cutoff lighting is not a photometric match to Blender's physical lighting. For Haki-targeted Blender
exports, RAW lighting values can be authored to match engine intensity settings.

An `Idle` clip is preferred as the initial animation when available. Other clips can be selected using
the generated animator's `currentClip` name. Multiple source files are still placed on a showcase grid;
export one assembled GLB when cross-file scene placement must be retained.
