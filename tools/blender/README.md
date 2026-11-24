# Haki Blender editor 0.2.1

## Install and open

Build with `python3 tools/blender/build_extension.py`, then install `dist/haki_editor-0.2.1.zip` through
Blender Preferences > Get Extensions > Install from Disk. Replace the previous extension and restart Blender.
Open `Assets/Game/Scenes/arena-01/arena-01.blend`. The old source is retained in Archive and should not be edited.
The 3D viewport's N sidebar has the Haki tab.

## Project settings

- **Haki Project:** repository root, containing tools/import_gltf_pack.py.
- **Project Python:** host `.venv/bin/python` with tools/requirements.txt installed.
- **Engine Executable:** built Engine; needed only for Export and Play.
- **Scene ID:** selects `Assets/Game/Scenes/<id>`.
- **Scene Directory:** optional explicit selection of the same folder; must agree with Scene ID.
- **Pack:** asset ID namespace. Keep it unchanged on reimport.
- **Make Startup Scene:** select this scene in Game/Project.json.
- **Import into Haki:** enable one-click export/import. Disable for export-only mode.

Current projects export to `Models/arena.glb` inside the scene folder. The legacy Export GLB field is only used
for projects with the old global catalog. Save the .blend at the scene root alongside Catalog.json and scene JSON.
The migrated arena already has updated saved settings.

## Physics and characters

Select a mesh, enable Rigid Body + Collider, and choose a shape. Static is appropriate for floors, walls and
stationary destructible cover. Fit to Mesh Bounds copies evaluated bounds once; edit dimensions/offset afterward.
Use Add Collider for invisible Empty collision objects. Box dimensions and capsule heights are full lengths.
Object scale is applied once; scene Unit Scale must be 1. Mirroring, shear and unsupported scaling fail validation.

Add Player creates a feet-origin capsule controller. It needs unit scale and an upright orientation. Keep
Locally Controlled Player enabled for native controls; scripts can instead steer it through the Lua API.
Do not combine its controller with a separate rigid body or collider. Green wireframes are colliders,
magenta are sensors, and cyan are character capsules. Debug drawing does not create exported mesh geometry.

For character visuals, keep the mesh parented to its armature. Set the mesh Type to Attached Visual and Runtime
Parent to the Player marker. Place and orient it relative to the marker in Blender; export captures that offset
and scale. On animated meshes, Drive Idle / Movement selects the exact named clips. Keep clips as active actions
or on NLA tracks. Unanimated accessories can disable animation driving. Bone sockets are separate future work.

## Scripts and environments

Entity Script is independent of physics Type. It supports one Lua file and typed property overrides per entity.
Use a local path such as `player.lua` under the scene Scripts folder, or `shared/foo.lua` under Game/Shared/Scripts.
The file picker also accepts those files. Add Property Override supports Number, Boolean, String and Vector 3;
vector overrides use gameplay Y-up coordinates. Paths must stay inside their respective script roots.

Scene Script selects one scene behaviour. It has Lua-authored defaults; scene property overrides are not
supported by the current runtime. Local modules use `require("lib.name")`; shared modules use
`require("shared.lib.name")`. LuaLS configuration at the repository root includes both locations.

Use Engine Default Skybox is enabled by default and supplies the engine HDR skybox and diffuse/specular IBL.
Scene Skybox Asset takes precedence and must be a texture ID in the scene/engine/shared catalogs.
Disable the checkbox and leave the custom asset blank to opt out. Previously saved checkbox choices are retained.
Export records this opt-out as `"environment": null`; omitting the field inherits the engine environment.
The camera and lights
are exported; raw light strengths preserve Haki's intensity convention. Sun strength 1, exposure 0 imports as 1.
Blender viewport lighting is not an exact match for Haki's rendering and tone mapping.

## Validate, export, play

Validate Arena checks transforms, references, script files and duplicate IDs. Use Repair Missing / Duplicate IDs
after duplicating configured entities. Renaming/moving preserves identity. Make linked physics objects local;
make collections with authored metadata real before export, then repair IDs.

Export Arena stages GLB, validates it with the importer, and generates scene-local materials, textures, catalog,
scene JSON and receipt. Receipts track generated files. Reimport preserves Blender files, Lua scripts and other
unowned files; conflicting authored output paths fail rather than being overwritten. Do not edit generated
materials/scene JSON expecting those edits to survive reimport.

Export and Play launches the configured Engine from the project root. Build C++ beforehand; the button does
not compile. Packaged builds contain Engine, Shared and registered scenes; Blender files and receipts are excluded.

## Flatpak Blender

The host venv may use a different Python version than Blender's Flatpak runtime. The extension detects Flatpak
and uses flatpak-spawn --host for Python and Engine. Enable that explicitly from a host terminal, then restart Blender:

```sh
flatpak override --user --talk-name=org.freedesktop.Flatpak org.blender.Blender
```

This permits Blender to launch host programs. The extension never changes permissions itself. Alternatively,
disable Import into Haki, export only, and invoke the importer manually from a host terminal.

## Checks and migration

```sh
.venv/bin/python -m unittest discover -s tools/tests
blender -b --factory-startup --python-exit-code 1 --python tools/tests/blender_haki_smoke.py
```

`tools/migrate_assets.py` previews the legacy migration; --apply preserves originals under Archive/Experiments.
Run Blender with `--python tools/blender/migrate_project.py` afterward to resave migrated arena settings.
This workspace has already been migrated. The smoke test uses a disposable scene and checks physics,
animations, parent relationships, scripts, metadata conversion, repeat imports and authoring preservation.
