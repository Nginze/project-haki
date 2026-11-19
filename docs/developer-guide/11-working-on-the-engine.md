# 11. Working on the engine

[Guide index](README.md) · Previous: [Input, UI, and tools](10-input-ui-and-tools.md) · Reference: [Glossary](glossary.md)

Start with one behavior you can observe. Follow its data from the authored file to the component, system, and output. This is more effective than trying to understand every engine file before changing anything.

## Build and run

Run these commands from the repository root:

```sh
cmake -S . -B build
cmake --build build -j2
./build/Engine
```

The project uses C++20 and CMake. Configuration fetches several pinned library versions, so the first setup needs access to those dependency sources. A Linux development environment also needs the compiler and the desktop/OpenGL development libraries required by the configured GLFW build. The exact package names depend on the distribution.

The `HakiEngine` target is the engine library. `Engine` is the runnable program. `HakiAssets` prepares the runtime asset copy. Tests are configured through CTest when `BUILD_TESTING` is enabled.

Keep the working-directory distinction in mind: running from the root reads source `Assets`; running from `build` reads packaged `build/Assets`.

### Python tools

If the project environment does not exist yet:

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r tools/requirements.txt
```

Use that interpreter for the importer and Python tests. Blender has its own interpreter, so installing project dependencies does not change Blender's bundled Python automatically.

The add-on installation and setup are documented in [tools/blender/README.md](../../tools/blender/README.md). Point the add-on at the project root, project Python, and built engine executable as required by that workflow.

### Optional development features

The defaults enable ImGui and the free-look camera. You can configure a build with them disabled:

```sh
cmake -S . -B build \
  -DHAKI_ENABLE_IMGUI=OFF \
  -DHAKI_ENABLE_DEBUG_CAMERA=OFF
cmake --build build -j2
```

These are build choices, not `Project.json` renderer switches. Turning them off removes useful development controls from that build; turn them back on for the normal tooling experience.

## A source map

| Task | Start with |
| --- | --- |
| Startup, update order, shutdown | [Core/Game.cpp](../../src/Haki/Core/Game.cpp) |
| Project setting defaults/validation/save | [Core/ProjectSettings.cpp](../../src/Haki/Core/ProjectSettings.cpp) |
| Scene requests and lifetime | [Scene/SceneManager.cpp](../../src/Haki/Scene/SceneManager.cpp) |
| Scene data and per-tick work | [Scene/Components.h](../../src/Haki/Scene/Components.h), [Scene/Scene.cpp](../../src/Haki/Scene/Scene.cpp) |
| JSON loading rules | [Serialization/SceneSerializer.cpp](../../src/Haki/Serialization/SceneSerializer.cpp) |
| Catalogs, handles, resource lifetime | [AssetManager](../../src/Haki/AssetManager) |
| Model and material decoding | [AssetLoader](../../src/Haki/AssetLoader) |
| Drawing and image effects | [Renderer](../../src/Haki/Renderer), [GL](../../src/Haki/GL), [engine shaders](../../Assets/Engine/Shaders) |
| Body and controller behavior | [Systems/PhysicsSystem.cpp](../../src/Haki/Systems/PhysicsSystem.cpp), [Physics](../../src/Haki/Physics) |
| Animation blending | [Systems/AnimationSystem.cpp](../../src/Haki/Systems/AnimationSystem.cpp) |
| Lua behavior and API | [Scripting](../../src/Haki/Scripting), [Lua definitions](../../tools/lua/library/haki.lua) |
| Blender controls and metadata | [haki_editor](../../tools/blender/haki_editor) |
| Disk import and runtime packaging | [importer](../../tools/import_gltf_pack.py), [packager](../../tools/package_assets.py) |
| Live inspector and measurements | [Debug](../../src/Haki/Debug) |

## Debug by following the data

### Blender looks correct, runtime is wrong

Follow this route:

```text
.blend → exported GLB + metadata → imported scene/catalog → loaded components → system result
```

At each boundary, ask whether the expected value exists there. If the scene JSON still contains a script disabled in Blender, the problem happened before script execution. If JSON has the right camera but the debug view differs, inspect camera selection and projection override.

Confirm which `Assets` folder the process opened before changing the code. A source/build-copy mismatch can imitate an importer or renderer bug.

### The player does not move

1. Read the script log. An errored instance is disabled until reload.
2. Check that the script is attached to the controller entity, not just the armature or visual mesh.
3. Confirm it has `Transform` and `CharacterController`, without an extra rigid body/collider combination.
4. Check pause state and whether free-look or ImGui currently owns input.
5. Compare requested movement with actual velocity. Collision can correctly stop a valid request.

`entity has no characterController` means the API exists but the target entity lacks its required component. Installing editor definitions cannot solve that runtime ownership problem.

### The player faces the wrong direction

Separate three questions: is movement world-relative or camera-relative, is gameplay facing using negative Z forward, and does the visual need a local forward correction?

Fixing a model's local orientation should not require rotating every movement vector or making the physics capsule sideways. Keep model correction on the attached visual.

### The camera is too far away or has the wrong projection

Inspect the primary camera in scene JSON, then the live camera component, then the active development camera/override. For orthographic views, inspect `orthoSize`; distance alone does not zoom the picture.

For a gameplay follow issue, inspect the scene's camera script and the tick order. A script might clamp to arena bounds or wait until the player crosses a screen threshold. These are game rules, not automatic `SceneManager` behavior.

### A material is black or unexpectedly dark

Check the material/texture load errors, source paths, normals, backface winding, UVs, light direction/intensity, and environment settings. Disable one rendering effect at a time to isolate the cause.

The canyon shoreline issue is a useful concrete example. The bank and water slab overlapped; combining and recalculating the open mesh could flip bank-top normals downward. Blender's display hid the problem more easily than the runtime lighting did. Correct material colors alone could not repair those normals.

The fix preserved the intended per-part winding. A Blender smoke check now verifies exported top-triangle winding, upward normals after Y-up conversion, and palette UVs. See [blender_canyon_water_smoke.py](../../tools/tests/blender_canyon_water_smoke.py). It demonstrates why checking the actual exported geometry is more useful than only judging a viewport screenshot.

### An animation looks broken

Check that the mesh, skeleton, and clips agree; named clips exist; interpolation is supported; the visual parent/local transform is correct; and the controller is not being moved by an unintended animated root.

A good capsule position with a displaced mesh points toward the visual/animation path. A good pose that faces sideways points toward local orientation or facing conventions.

### Settings revert

Check the loaded project path and autosave state. The runtime persists project settings to the project it opened. Importers preserve those settings, but a different working directory can make you inspect a different `Project.json`.

Disabling autosave itself must be saved explicitly if you want that preference to survive the next launch. Entity inspector edits and project rendering settings have different persistence rules.

### Performance drops

Compare CPU stages and GPU passes before reducing poly count blindly. Try one controlled change: lower internal render resolution, turn off shadows, or pause simulation. Observe which change affects frame time.

If lowering resolution helps a lot, pixel shading/post-processing may matter. If it barely helps, inspect CPU work, object submissions, simulation, and shadow geometry. These observations suggest where to investigate; they do not replace measurement.

## Adding a feature across the whole pipeline

Suppose you add a gameplay component that must be authored in Blender. A C++ struct alone is only the first piece.

1. **Define its data and owner.** Is it per entity, per scene, or per project? What are its defaults and units?
2. **Implement runtime behavior.** Decide which system reads it, when it runs, and which other system owns any shared transform/state.
3. **Define its stored form.** Add description/parsing/validation in the serializer. Decide which values are authored and which are runtime-only.
4. **Carry it through import.** Update importer metadata handling and exported JSON generation.
5. **Expose authoring controls.** Add Blender properties, validation, and metadata export. Update add-on packaging when distributing it.
6. **Expose gameplay access if needed.** Add C++ bindings, Lua definitions, and useful inspector controls.
7. **Check the complete route.** Test valid data, a useful failure case, export/import, scene load, and visible runtime behavior.
8. **Document the behavior.** Include defaults, units, ownership, and persistence rules.

Do not add every layer when the feature does not need it. A local Lua cooldown may need no new component or Blender control. A renderer-wide preference belongs in renderer/project settings rather than an arbitrary scene entity.

## Tests that already exist

C++ tests cover scene lifetimes/transitions, serialization, assets/handles, physics, controllers, animation, transforms, scripting, camera projection, renderer helpers, project settings, and development UI behavior.

List configured tests without running them:

```sh
ctest --test-dir build -N
```

Run all configured C++ tests, or one relevant group:

```sh
ctest --test-dir build --output-on-failure
ctest --test-dir build -R 'Serialization|SceneManager' --output-on-failure
```

Some graphics/input tests need a usable desktop/OpenGL context. Tests marked with skip return code 77 can skip when that environment is unavailable. A skip is not proof that rendering passed.

Python pipeline tests use `unittest`:

```sh
.venv/bin/python -m unittest discover -s tools/tests -p 'test_*.py'
```

Blender smoke scripts are separate and need Blender's `bpy` environment. They are not ordinary host-Python unit tests. Read their header before running them; an authoring smoke test may create temporary scene data and should be run in an appropriate Blender session or background process.

When changing a system, run the tests that exercise the changed behavior and a relevant end-to-end check. A JSON parser unit test cannot prove that Blender exported a visible collider attachment correctly; a screenshot cannot prove that malformed data is rejected safely.

## A practical learning sequence

1. Follow one crate from Blender to GLB, catalog, loaded mesh, entity, and draw submission.
2. Follow one movement request from Lua through the controller to the attached visual.
3. Trigger a one-shot animation and trace its return to locomotion.
4. Add a small scene-local Lua rule with a property and a visible label.
5. Change one renderer setting and explain the pass/timing change.
6. Add a small engine feature only after you can identify which missing capability requires it.

For the planned online arena game, networking will introduce new questions: which machine owns the authoritative state, how inputs are sent, how clients predict movement, and how corrections are displayed. Fixed-step simulation is useful groundwork, but the present code does not already solve those questions.

Use the current engine to finish a small, measurable local gameplay loop first. The code map and ownership rules in this guide help you place the next feature when that loop exposes a real need.
