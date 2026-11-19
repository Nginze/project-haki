# 3. Entities, transforms, and cameras

[Guide index](README.md) · Previous: [Startup and the frame loop](02-startup-and-frame-loop.md) · Next: [Files and serialization](04-files-and-serialization.md)

The scene's EnTT registry stores its live objects. Systems ask that registry for entities with the components they need.

## Entity, component, and system

An entity is a runtime identifier. A component is data attached to it. A system is code that uses some combination of components.

This small C++ example shows the pattern. It is a teaching example, not a replacement for scene loading:

```cpp
auto entity = registry.create();
registry.emplace<Haki::TransformComponent>(entity);
registry.emplace<Haki::NameComponent>(entity, "Test object");

auto view = registry.view<Haki::TransformComponent>();
for (auto e : view)
{
    auto &transform = view.get<Haki::TransformComponent>(e);
    transform.Translation.x += 1.0f;
}
```

A **view** means “give me the entities with these components.” A view of transform plus mesh renderer leaves out cameras and invisible triggers automatically.

## Three kinds of identity

| Identity | Example | Purpose |
| --- | --- | --- |
| Authored entity ID | A UUID stored by the add-on, or `player` in a small test | Stable references across exported scene files |
| Runtime entity handle | `entt::entity` | Access to one live entity in one registry |
| Name | `Player`, `Fence`, `Camera` | Human-readable display and convenient lookups |

`EntityIdComponent` stores the authored ID. `NameComponent` stores the display name. Names can repeat, so use stable IDs when a relationship must be unambiguous.

Do not write a runtime entity handle into a scene file and expect it to identify the same object next run. Reloading a scene creates a new registry and new runtime objects.

## Components you will meet often

| Component | Data it carries |
| --- | --- |
| Transform | Position, rotation, scale, and computed world matrix |
| ParentTransform | Stable parent ID and transform relative to that parent |
| Camera | Projection settings and whether it is the primary camera |
| MeshRenderer | Mesh handle and material handles |
| SkinnedMeshRenderer | Animated mesh, skeleton, materials, and final bone matrices |
| Animator | Named clips, playback state, and blending state |
| LocomotionAnimation | Movement-speed samples and smoothing settings |
| RigidBody + Collider | Simulation behavior and collision shape |
| CharacterController | Movement request, capsule settings, and grounded/movement results |
| PlayerInput | Built-in movement speed |
| Script | Lua path and authored property values |
| Directional/Point/SpotLight | Light color, strength, and type-specific settings |

Not every entity needs every component. An invisible collider is useful precisely because it has physics without a rendered mesh.

## Position, rotation, and scale

A **transform** describes where something is, how it is turned, and how large it is.

The engine uses Y as up. The ground is normally the XZ plane. A camera's forward direction is local negative Z. The player-facing math also treats negative Z as forward.

The Blender export converts Blender's Z-up coordinates to the engine/glTF Y-up convention. In the usual conversion, `(x, y, z)` becomes `(x, z, -y)`. Do not manually apply that conversion again to an already exported scene.

Use metres consistently for scene dimensions. Jolt's gravity is `(0, -9.81, 0)`, and the default controller is about 1.8 units tall. A metre-based scale makes movement and camera framing easier to tune together.

### What a matrix does

A matrix is a compact way to combine several transform operations. The engine builds an object's world matrix as:

```text
World = Translation × Rotation × Scale
```

With the math convention used here, a vertex gets scaled first, then rotated, then translated. The result puts the mesh's own coordinates into world coordinates.

### What a quaternion does

A quaternion stores a rotation without relying on three independently edited Euler angles. You can understand it as a rotation value; you do not need to memorize its four-number math to use it.

Scene JSON stores it as `[x, y, z, w]`. GLM's quaternion constructor takes `(w, x, y, z)`. The parser deliberately changes the order. The identity rotation is `[0, 0, 0, 1]` in JSON.

Lua rotation helpers use radians. A half-turn is `math.pi`; a quarter-turn is `math.pi / 2`. Camera FOV settings and physics slope settings are named in degrees instead.

## A player is usually more than one entity

```text
Player controller entity
  Transform: feet position and gameplay facing
  CharacterController: capsule and movement
  Script: player behavior

  Character visual entity
    ParentTransform: attached to Player
    Local transform: model fit and forward correction
    SkinnedMeshRenderer + Animator + LocomotionAnimation
```

The controller should remain upright and have predictable dimensions. The visual may need a local rotation or scale adjustment because of the downloaded model's original authoring conventions.

Keeping that correction on the visual avoids rotating movement axes or lying about capsule size. An armature is the animation skeleton; it does not automatically become the gameplay controller.

The add-on offers attachment controls for this relationship. The runtime uses explicit parent metadata, rather than preserving every organizational Blender parent as a gameplay relationship.

## How runtime parenting works

For an attached child:

```text
child world matrix = parent world matrix × child local matrix
```

`TransformHierarchySystem` finds parents using stable IDs and evaluates parents before children. It updates each child's world matrix and world transform values. Its `ParentTransformComponent.Local` remains the relative transform used to rebuild that result.

This means changing a child's world position alone is not a lasting local edit: the next hierarchy update derives it again from the parent and local transform.

Physics bodies and character controllers are required to be world roots in the authored runtime hierarchy. Attach visuals to the controller; do not make the controller a child of an animated mesh. This avoids two systems competing to decide its world position.

The imported scene can be mostly flat while a skeleton still keeps its own bone hierarchy. These are separate hierarchies with different jobs.

## What the camera actually does

The renderer uses three spaces:

```text
mesh coordinates → world coordinates → camera coordinates → projected screen position
```

The **view matrix** is the inverse of the camera's world transform. It describes the world as seen from that camera. The **projection matrix** decides how that view becomes a 2D image.

| Projection | Result | Main size control |
| --- | --- | --- |
| Perspective | Farther objects appear smaller | Vertical FOV and camera position |
| Orthographic | Distance does not change apparent object size | `OrthographicSize` |

`OrthographicSize` is **half the visible height**, not the whole width or height. If it is 6, the camera view spans 12 units vertically in camera space. At 16:9, the width is about 21.33 units. A steep camera pitch changes how this rectangle meets the ground.

For a Brawl Stars-like composition, pitch, projection, view size, object dimensions, and player size all contribute. Moving an orthographic camera farther away does not zoom out the way a perspective camera does.

Exactly one camera must be marked primary in a serialized scene. The primary camera is the gameplay view. The development free-look camera and projection override are separate views and can make the runtime appear different without changing the authored camera component.

## Try this

In an exported scene, compare the player's transform with its visual's `parent.local`. Identify which rotation is gameplay facing and which rotation corrects the model. Then inspect the primary camera's projection before changing camera distance to solve a scale problem.

## Source reading route

1. [Components.h](../../src/Haki/Scene/Components.h): component definitions and defaults.
2. [TransformHierarchySystem.cpp](../../src/Haki/Systems/TransformHierarchySystem.cpp): parent resolution.
3. [SceneCamera.h](../../src/Haki/Scene/SceneCamera.h): view and projection construction.
4. [PlayerControllerSystem.cpp](../../src/Haki/Systems/PlayerControllerSystem.cpp): ground movement axes and facing.
5. [CameraSystem.cpp](../../src/Haki/Systems/CameraSystem.cpp): development free-look camera.
