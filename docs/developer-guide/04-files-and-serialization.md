# 4. Files, serialization, and scene management

[Guide index](README.md) · Previous: [Entities and cameras](03-entities-transforms-cameras.md) · Next: [Assets and Blender](05-assets-and-blender.md)

**Serialization** means turning structured program data into a form that can be stored or sent. **Deserialization** means reading that form and rebuilding useful program data from it.

For Haki, JSON describes a scene. Loading creates C++ components, resolves assets, and builds a live `Scene`. It does not copy a block of raw C++ memory from disk.

## Why the file cannot just contain a C++ object

A live component may contain an asset handle or a reference to a Jolt body. Those values are meaningful only in the running program that created them. A memory address from yesterday's run is useless today.

The stored version uses stable information instead: entity IDs, asset IDs, numbers, and script paths. Loading turns those stable descriptions into new runtime objects.

For example:

```text
On disk: mesh = "engine.mesh.unit-cube"
Load: find catalog entry → load resource if needed
In memory: MeshHandle { Index, Generation }
```

## The files have different jobs

| File | Question it answers |
| --- | --- |
| `Game/Project.json` | Which scenes exist, which starts first, and what are the project window/render settings? |
| Scene `.scene.json` | Which entities and components make up this scene? |
| `Catalog.json` | Where is each named asset, and what type is it? |
| `.material.json` | Which shader, parameters, textures, and draw state does a material use? |
| `.prefab.json` | Which reusable entity description should be expanded into a scene? |
| `ImportReceipt.json` | Which output files belong to this import operation? |
| `.blend` | Editable Blender source |
| `.glb` / `.gltf` | Exported meshes, materials, skeletons, clips, and related model data |
| `.lua` | Gameplay behavior |

A **schema** is the set of fields and rules a file format accepts. Haki checks headers such as `"type": "haki.scene"` and `"version": 1`. The type prevents reading one kind of JSON as another. The version gives future changes a way to distinguish formats; it does not automatically migrate every old format.

## Follow a scene load

`SceneManager::Initialize()` calls `SceneSerializer::LoadManifest()`. Each manifest entry becomes a registered scene factory: a function that can load that scene on request.

When a factory calls `Deserialize()`, the main steps are:

1. Read the scene JSON.
2. Load its explicitly named catalog, if present, including imported catalogs.
3. Check the type, version, field names, and values.
4. Parse entity descriptions and expand prefab instances.
5. Check relationships: duplicate IDs, missing parents, parent cycles, component rules, and exactly one primary camera.
6. Check that script files exist under the allowed script roots.
7. Create a new `Scene`, resolve referenced assets, and attach runtime components.
8. Evaluate the new scene once with zero elapsed time, then return it or a set of errors.

The temporary description structs in `SceneDescription.h` are useful here. They hold asset names while validation is underway. The final components hold handles after assets have been resolved.

The zero-time update prepares physics objects and initial transforms/poses without advancing the simulation. Lua startup callbacks wait until the first positive tick.

The serializer restores the previous shared catalog if the load fails. The partially built scene is not installed as the active scene. Asset loading is separately cached, however: a failed scene load is not a promise to undo every resource that was loaded during the attempt.

## A small scene you can read

This is a complete **teaching scene document**, assuming it sits beside a valid scene `Catalog.json`. It has an invisible floor, an invisible controller, and a camera. It does not add a character mesh or script.

```json
{
  "type": "haki.scene",
  "version": 1,
  "id": "learning-scene",
  "assetCatalog": "Catalog.json",
  "environment": null,
  "entities": [
    {
      "id": "camera",
      "components": {
        "transform": {
          "translation": [0, 10, 7],
          "rotation": [-0.46175, 0, 0, 0.88701]
        },
        "camera": {
          "primary": true,
          "projection": "orthographic",
          "orthoSize": 6
        }
      }
    },
    {
      "id": "floor",
      "components": {
        "transform": { "translation": [0, -0.5, 0] },
        "rigidBody": { "motion": "static" },
        "collider": { "shape": "box", "halfExtents": [10, 0.5, 10] }
      }
    },
    {
      "id": "player",
      "components": {
        "transform": { "translation": [0, 0.1, 0] },
        "characterController": { "radius": 0.35, "halfHeight": 0.55 },
        "playerInput": { "moveSpeed": 5 }
      }
    }
  ]
}
```

The camera is pitched down by about 55 degrees. The floor and player have no mesh renderers, so use collider debug drawing to see them. The loader supplies defaults for omitted optional fields. A missing required field still produces an error.

## Errors are addresses into the data

A serialization error contains a source file, a JSON location, and a message. For example:

```text
arena-02.scene.json/entities/player/components/script/path:
Script '.../Scripts/player.lua' does not exist
```

The slash-separated location tells you where the bad value lives. Some errors use an entity ID in that location; others use an array index. Read it as a route to the problem, rather than assuming every message is a directly executable JSON query.

Strict field checks also catch misspellings such as `halfExtent` instead of `halfExtents`. If you add a new component in C++ but forget its parser, the file should fail validation instead of silently pretending the feature loaded.

## Prefabs: reusable descriptions

A prefab is a file containing entity descriptions that can be reused. A scene instance names a source file, an instance name, an optional transform, and optional overrides.

This is the shape of an instance inside a scene's `prefabs` array:

```json
{
  "source": "PhysicsBody.prefab.json",
  "instance": "ball",
  "transform": { "translation": [0, 4, 0] },
  "overrides": {
    "root": { "rigidBody": { "restitution": 0.5 } }
  }
}
```

The loader expands it into normal scene entities. Names are given an instance prefix so repeated prefabs do not all use the same entity IDs. Nested prefab loads are checked for cycles.

This is **load-time reuse**. It is not a full live prefab editing system, and placing a Blender collection instance does not automatically create a Haki JSON prefab instance. See the existing [physics scene fixture](../../src/Tests/Serialization/Fixtures/Physics.scene.json) and [prefab fixture](../../src/Tests/Serialization/Fixtures/PhysicsBody.prefab.json) for a working pair.

## The scene manager's job

The serializer knows how to build a scene. The manager decides **which scene is active and when its lifetime starts or ends**.

It keeps registered factories, one active scene, one pending request, and transition state. A new pending request can replace the earlier pending request; this is not an unlimited queue.

From C++ code that already has access to the manager, a request looks like this:

```cpp
auto result = manager.RequestScene("arena-02");
if (result == Haki::SceneRequestResult::UnknownScene)
{
    // Check the registered IDs in Project.json.
}
```

An accepted request means the manager scheduled it. It does not mean disk loading has already succeeded. `GetLastActivationStatus()` and `GetLastSerializationErrors()` describe the later activation result. Requesting the already active scene normally returns `AlreadyActive`; deliberate reloads use the `reloadIfActive` option.

The normal scene lifetime is:

```text
factory creates Scene
    → OnLoad
    → OnEnter(payload)
    → repeated OnUpdate / UpdateRuntime / OnRender
    → OnExit
    → OnUnload
    → destruction
```

For a scene switch, the manager creates the next scene and calls its `OnLoad()` before removing the old scene. If the factory returns no scene, including when deserialization fails, the old scene remains active. `OnLoad()` itself has no success/failure return value. Exceptions from custom callbacks are not caught here, so the protected factory-failure path is not a general exception-recovery system.

The optional payload uses `std::any`, a C++ container for a value whose type is checked later. It could carry scene-entry information. It is not automatically serialized to JSON or available as a Lua scene-switch API.

### Transitions

The manager supports instant transitions and timed covering/revealing phases. A fade specification stores durations and a color. Currently `SceneManager::Render()` only calls the active scene's render function: **the transition state alone does not draw a fade overlay**. A visible transition needs rendering code to use that state.

Loading is synchronous. A fade duration does not turn scene loading into background work, and switching scenes does not provide spatial world streaming.

## What saving means today

The import tool writes scene JSON from Blender exports. The runtime scene serializer is primarily a loader; it does not currently provide a full “save every live entity back to the Blender scene” path.

Project settings have their own save path. `ProjectSettings::Save()` rereads the latest project JSON, updates settings fields, and uses a temporary file before replacement. This preserves scene registrations and unrelated values that an exporter may have changed meanwhile.

Editor autosave waits for about one second without further settings changes. It saves on clean shutdown when enabled, too. This saves window/render/editor settings, not an entire running gameplay session. Inspector changes to an entity do not become authored Blender changes automatically.

## Environment defaults

Omitting `environment` uses the engine's default environment when that texture exists in the composed catalog. `"environment": null` explicitly opts out. An environment object selects a texture, intensity, and rotation.

Project rendering settings can hide the visible skybox while leaving environment lighting enabled. Background visibility and lighting are separate choices.

## Source reading route

1. [SceneDescription.h](../../src/Haki/Scene/SceneDescription.h): the load-time descriptions.
2. [SceneSerializer.cpp](../../src/Haki/Serialization/SceneSerializer.cpp): `LoadManifest`, `Deserialize`, and component parsers.
3. [JsonUtils.h](../../src/Haki/Serialization/JsonUtils.h): common validation and errors.
4. [SceneManager.cpp](../../src/Haki/Scene/SceneManager.cpp): requests, activation, and lifetime callbacks.
5. [ProjectSettings.cpp](../../src/Haki/Core/ProjectSettings.cpp): settings validation and saving.
