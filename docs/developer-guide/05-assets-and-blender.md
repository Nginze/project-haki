# 5. Assets and the Blender pipeline

[Guide index](README.md) · Previous: [Files and serialization](04-files-and-serialization.md) · Next: [The renderer](06-renderer.md)

An **asset** is a reusable resource: a mesh, texture, material, shader, skeleton, or animation clip. An entity is one use of those resources in a scene.

Twenty crates can share one mesh and one material while keeping twenty different positions and physics bodies. Sharing resources saves memory. It does not automatically mean those crates are drawn in one GPU call.

## Folder ownership

```text
Assets/
  Engine/
    Catalog.json
    Geometry/  Materials/  Shaders/  Fonts/  Skyboxes/
  Game/
    Project.json
    Shared/
      Catalog.json
      Scripts/
      Kits/
    Scenes/
      arena-02/
        arena-02.blend
        arena-02.scene.json
        Catalog.json
        ImportReceipt.json
        Models/  Materials/  Textures/  Scripts/
```

The engine folder holds general defaults. Shared holds game resources deliberately reused across scenes. A scene folder holds that scene's sources and runtime package. Empty folders are unnecessary.

The [asset ownership document](../../Assets/README.md) gives the shorter operating rules. Old experiments in `Archive/` are outside runtime discovery and packaging.

## From Blender to a running scene

1. **Author in Blender.** Place kit meshes, lights, camera, and controller. Use the Haki add-on for physics and script properties.
2. **Validate.** Catch duplicate IDs, unsupported physics transforms, bad paths, and incompatible components.
3. **Export a GLB.** Blender writes models and Haki metadata into the export.
4. **Run the Python importer.** It creates or updates the scene JSON, local catalog, imported resources, and receipt.
5. **Run the engine against those files.** The serializer creates entities; loaders prepare assets in memory; scene systems create physics and scripting state.

A `.blend` change alone does not update a running scene. A raw GLB export alone does not necessarily update its scene JSON either. The complete export/import step connects those changes.

### Why GLB?

glTF describes models and their related data. A `.gltf` file commonly refers to separate buffers or images. `.glb` is the binary container version and can pack that data together. It is a transport format between Blender and the importer/runtime, not the whole Haki project format.

For external formats such as FBX or OBJ, [convert_model.py](../../tools/convert_model.py) is a separate preparation tool that uses Assimp-supported input to produce a canonical GLB. Runtime loaders still consume the resulting supported data; this is not native runtime loading of every source format. For scenes authored with Haki metadata, use the Blender add-on's export route.

### How Haki metadata travels

Blender knows about meshes and cameras, but it does not know what a Haki `CharacterControllerComponent` is. The add-on stores Haki-specific descriptions in exported custom data, under `extras.haki`.

This includes stable entity IDs and authored components such as colliders, scripts, and runtime parent relationships. Scene metadata carries scene script and environment choices.

The exporter temporarily writes this metadata, exports visible objects with cameras and lights, and restores previous custom properties afterward. Collider previews are development drawings, rather than extra visible collision meshes that should appear in the game.

The exporter enables Y-up conversion and uses raw lighting conversion so an authored sun strength such as 1 does not become an unexpectedly large imported value.

## Why flatten the Blender object tree?

Blender parenting can serve organization, modeling, animation, and gameplay attachment. Preserving all of it blindly would make engine behavior depend on many relationships that have no gameplay meaning.

The importer resolves ordinary object placement into world transforms. Explicit runtime attachments remain as parent components. Skeletons retain the pose hierarchy required by animation.

So a flat list of scene entities does not mean that bones were flattened or that attached visuals cannot follow a player. It means those relationships are represented deliberately.

## Catalogs are a resource address book

An entry connects a readable ID to a source and type. This is an actual engine asset entry:

```json
{
  "id": "engine.mesh.unit-cube",
  "type": "mesh",
  "source": "Geometry/UnitCube.gltf",
  "mesh": "UnitCube"
}
```

The `mesh` selector identifies which mesh inside the file to use. One model file can contain multiple meshes, skins, and animations, so a source path alone is not always enough.

A catalog can import other catalogs. Scene catalogs import engine and shared resources explicitly. Source paths are relative to the **catalog that declares the entry**, even when another catalog imports it.

The catalog rejects duplicate IDs, hash collisions, and import cycles. It does not scan every folder and guess what belongs to the scene.

## Three stages of an asset reference

| Stage | Representation | Meaning |
| --- | --- | --- |
| Authored files | String such as `engine.mesh.unit-cube` | Stable logical name |
| Catalog/runtime lookup | 32-bit `AssetID` derived from the string | Compact lookup key |
| Loaded component | Typed handle with slot index and generation | Access to a loaded resource |

The string is hashed using the catalog's `MakeRuntimeId()` function. Hashing converts text into a number. Since different strings could theoretically produce the same number, the catalog explicitly checks for collisions.

### Why a handle has a generation

Imagine mesh A occupied slot 4. It was unloaded, and mesh B later reused slot 4. An old reference containing only “4” could accidentally access B.

A generation number changes when a slot receives a new resource. The resource manager checks both the slot and generation. An old handle then returns no resource rather than pointing at the replacement.

`handle.IsValid()` only checks that the handle has a non-invalid index. `AssetManager::GetMesh(handle)` and the resource manager perform the stronger live-resource check. Do not treat `IsValid()` alone as proof the resource is still loaded.

## What the loaders do

`AssetManager` maintains managers and loader registries for each asset type. A load request supplies an ID and source. The manager returns an existing resource when that ID is already loaded consistently, or finds a compatible loader and stores its result.

| Loader | Main work |
| --- | --- |
| glTF mesh | Read vertex/index data and prepare a GPU mesh |
| glTF skeleton | Build pose nodes, joint order, and bind information |
| glTF animation | Read timed translation/rotation/scale tracks |
| Material file | Resolve shader and texture references; create material parameters |
| Shader file | Read and compile vertex/fragment shader sources |
| Texture file | Decode an image and create its GPU texture |

The glTF loaders share a `GltfDocumentCache`, so reading a mesh, skeleton, and clip from the same model does not require parsing the file independently for every request.

Loading here is synchronous. Several loaders create GPU objects and therefore need the right OpenGL context. An atomic generation counter in the resource manager does not make the whole asset manager thread-safe.

## Resource lifetime is separate from scene lifetime

Scenes store handles. The asset manager owns the actual resources globally. Unloading a scene destroys its entities and scene systems, but does not automatically evict every asset those entities referenced.

This supports reuse across scene changes. It also means there is no complete automatic memory-budgeted streaming/unloading policy yet. Adding one would require deciding when resources are no longer needed and how dependencies are kept alive.

## Authored and generated files

Treat `.blend` and Lua files as authored sources. Imported scene JSON, imported materials/textures, and catalog entries are generated by the pipeline.

The importer stages changes and records generated files in `ImportReceipt.json`. On reimport, it uses that ownership record to replace its own output and preserve unrelated authored files. It refuses unsafe output collisions.

If you hand-edit a generated material, expect a later import to replace it. For a lasting appearance change, change the authored material or establish a separately owned material with its own ID and pipeline reference.

The importer also preserves `window`, `rendering`, and `editor` values in `Project.json`. Exporting an arena should not reset your project graphics preferences.

## Running the importer manually

From the repository root, with the project's Python environment installed:

```sh
.venv/bin/python tools/import_gltf_pack.py \
  Assets/Game/Scenes/arena-02/Models/arena.glb \
  --pack arena-02 --scene arena-02 --dry-run
```

`--dry-run` validates and reports without writing the import result. Remove it to import. Add `--set-entry` only when you want that scene to become the startup scene. The Blender **Export Arena** action performs the pipeline when **Import into Haki** is enabled.

Blender's Python and the project's Python are different installations. `pygltflib` belongs in the project interpreter used by the importer. In a Flatpak Blender setup, the add-on's process helper also handles launching host tools. Read an import error before reinstalling the add-on: the missing dependency may be in the other interpreter.

## Source assets versus packaged assets

The build's `HakiAssets` target packages runtime assets into `build/Assets`. It excludes Blender sources, backups, import receipts, archives, and other authoring-only files. Registered scenes, Shared, and Engine determine what is packaged.

Paths such as `Assets/Game/Project.json` are relative to the process's working directory:

```sh
./build/Engine
```

Run from the repository root, this uses source `Assets`. Running `./Engine` from inside `build` uses `build/Assets`. A stale packaged copy can explain why one launch shows old content while another does not. Blender's export-and-play action deliberately launches from the project root.

## Try this

Pick one mesh renderer in your scene JSON. Follow its mesh ID to the scene catalog, then follow that entry's source and selector. You have traced an entity all the way to the model data it draws.

## Source reading route

1. [Blender editor.py](../../tools/blender/haki_editor/editor.py): metadata, validation, export, and play actions.
2. [Blender metadata.py](../../tools/blender/haki_editor/metadata.py): exported component rules.
3. [import_gltf_pack.py](../../tools/import_gltf_pack.py): scene package generation.
4. [AssetCatalog.cpp](../../src/Haki/AssetManager/AssetCatalog.cpp): IDs, imports, and paths.
5. [AssetManager.cpp](../../src/Haki/AssetManager/AssetManager.cpp) and [ResourceManager.h](../../src/Haki/AssetManager/ResourceManager.h): load requests, sharing, and handles.
6. [package_assets.py](../../tools/package_assets.py): runtime packaging rules.
