# 7. Physics

[Guide index](README.md) · Previous: [The renderer](06-renderer.md) · Next: [Animation](08-animation.md)

Jolt provides collision detection and movement simulation. Haki connects Jolt objects to entities, transforms, Lua callbacks, and Blender-authored data.

The engine uses one `PhysicsSystem` per scene. `PhysicsCore` supplies shared Jolt setup, temporary working memory, and a worker-thread pool.

## Mesh, collider, and body are separate

A rendered mesh determines what you see. A **collider** is a simpler mathematical shape used for contact tests. A **rigid body** describes how that shape moves and responds.

A fence can look curved and detailed while using one or two box colliders. For a small top-down arena, simple collision is often easier to control and cheaper than testing every visual triangle.

Current collider shapes are box, sphere, and upright capsule. This integration does not currently expose general triangle-mesh or convex-hull authoring as another collider type.

## Body motion types

| Motion type | Who moves it? | Suitable examples |
| --- | --- | --- |
| Static | It stays where it was placed unless explicitly repositioned | Floor, walls, rock cover |
| Dynamic | Physics integrates velocity, gravity, and collision response | A falling prop or knocked object |
| Kinematic | Gameplay supplies a target transform; physics follows it | A moving platform or scripted obstacle |

**Mass** affects a dynamic body's response to impulses. **Friction** resists sliding at contact. **Restitution** controls bounce. **Damping** gradually reduces motion. Gravity factor scales gravity for that body.

A mass of zero in the Haki component means use the shape-based default mass calculation, not “an infinitely light dynamic object.”

A **sensor** detects overlap without acting as a solid obstacle. Bush zones, pickups, and danger zones can use sensors. Sensor behavior and collision layer filtering are separate choices that need to agree.

The JSON parser derives a layer when one is not supplied: sensors use Trigger, ordinary static bodies use Static, and other ordinary bodies use Dynamic. Creating components directly in C++ means you must also choose consistent values there.

## Shape sizes

Box `halfExtents = [1, 0.5, 2]` means a full size of 2×1×4 before transform scale.

A capsule's `halfHeight` is half its straight middle section, excluding the rounded ends:

```text
total capsule height = 2 × halfHeight + 2 × radius
```

For radius 0.35 and half-height 0.55, total height is 1.8.

Ordinary collider offsets are local to the entity. Haki applies transform scale to collider dimensions and offsets when creating the shape. A sphere uses the largest scale axis for its radius; a capsule uses the largest X/Z scale for radius and Y scale for height.

The character capsule has a separate rule: its configured dimensions are used directly and its shape is shifted upward so the entity origin is at the feet. Keep the controller transform scale at one and resize the visual separately.

The authoring pipeline rejects mirrored or sheared physics transforms. Shear means a transform has skewed the axes rather than simply rotating and scaling them. Apply or fix these transforms in Blender instead of relying on the runtime's defensive dimension clamping.

## Who controls the transform

This is the most important rule when debugging motion:

| Object | Before the physics step | After the physics step |
| --- | --- | --- |
| Static body | Usually unchanged | Remains at its physics placement |
| Kinematic body | Haki reads the entity's target transform | Jolt has moved it toward that target during the step |
| Dynamic body | Jolt has its current position and velocity | Haki copies active-body position/rotation back to the entity |
| Character controller | Gameplay supplies desired horizontal velocity | Collision-corrected position, velocity, and grounded state are copied back |

Changing a dynamic body's transform field alone is not a general teleport operation. Physics can overwrite it. C++ has `PhysicsSystem::Teleport()` to synchronize an explicit transform change with Jolt. The current Lua `set_position()` writes the transform: character controllers detect it as a teleport, but ordinary rigid bodies do not gain that same behavior automatically.

Static transforms are not continuously uploaded as ordinary movement. Changing collider dimensions after body creation also does not automatically rebuild its Jolt shape. Use the supported live editing methods or reload/recreate the body when changing creation-time properties.

## The character controller

The player uses Jolt `CharacterVirtual`, rather than an ordinary dynamic rigid body. Gameplay requests movement; the controller resolves that movement against surfaces, stairs, slopes, and gravity while remaining upright.

Its minimum component combination is:

```text
Transform + CharacterController
```

Add `Script` for Lua movement, or `PlayerInput` for the built-in movement path. Do not also add `RigidBody` and `Collider` to the same controller entity. The controller already owns its capsule and the integration rejects that mixed setup.

The controller has an internal kinematic body so queries and sensors can detect it. The integration keeps that body awake so entering a sensor still produces useful events.

Each update:

1. Detect an explicitly changed position, such as a respawn.
2. Read requested XZ movement; the current movement path does not use requested Y velocity as a jump command.
3. Combine grounded motion or airborne vertical velocity with gravity.
4. Run Jolt's extended character update, including configured step support.
5. Copy feet position, actual velocity, ground normal, and grounded state to the components.

Requested speed and actual speed can differ. Pressing into a wall can request 5 m/s while moving almost nowhere. Animation correctly uses the resulting velocity rather than assuming the request succeeded.

## Layers decide which pairs are tested

The current collision rules are:

| Pair | Collides? |
| --- | --- |
| Static ↔ Dynamic | Yes |
| Static ↔ Character | Yes |
| Static ↔ Static | No |
| Static ↔ Trigger | No |
| Dynamic ↔ Dynamic | Yes |
| Dynamic ↔ Character | Yes |
| Dynamic ↔ Trigger | Yes |
| Character ↔ Character | No |
| Character ↔ Trigger | Yes |
| Trigger ↔ Trigger | No |

Characters passing through one another is an explicit game-oriented rule. It does not prevent an attack query from finding a character.

**Broad phase** is the cheap early search that finds possible nearby collision pairs. **Narrow phase** tests their actual shapes. Jolt uses these stages internally; Haki supplies the layers and filters that determine eligible pairs.

## Queries ask a question without creating a new object

A **raycast** asks what a line hits first. An **overlap sphere** asks which shapes intersect a sphere now.

```lua
local hit = physics.raycast(vec3(0, 3, 0), vec3(0, -1, 0), 10)
if hit then
    log.info("Ground distance", hit.distance)
end

local nearby = physics.overlap_sphere(self.entity:position(), 2)
for _, other in ipairs(nearby) do
    if other ~= self.entity and other:is_character() then
        -- This is a possible target; gameplay still decides range, team, and damage.
    end
end
```

These snippets belong inside a script callback. Queries normally exclude the Trigger layer; the optional `includeTriggers` argument includes it. There is no automatic “ignore the caller” rule in the shown Lua query. Filter self and unsuitable targets in gameplay.

A sphere overlap does not answer whether a wall is between two players, nor whether a target lies in front of the attacker. Add line-of-sight and direction checks when the attack needs them.

## Contacts become Lua events after simulation

Jolt can invoke contact listeners on its worker threads. Those listeners queue raw body IDs under a lock. They do not call Lua or modify arbitrary scene components directly.

After the step, Haki resolves IDs to entities and dispatches events on the main scene update path:

```text
Jolt workers detect contact
  → queue raw event
  → physics step completes
  → resolve entities
  → ScriptSystem dispatches callback
```

Solid events call `on_contact_begin/end`; sensor events call `on_trigger_enter/exit`. Entity scripts receive the other entity. Scene scripts receive both entities.

These are contact events, not a full damage system. Do not assume one callback equals one attack or one damage application. Gameplay should track attack windows, targets already hit, or cooldowns as appropriate.

## Limits and threading

The default world reserves capacity for 4096 bodies, 4096 body pairs, and 2048 contact constraints. These are configured limits, not benchmark guarantees. The physics system logs capacity problems rather than silently making them disappear.

Physics runs at the fixed simulation step. Jolt uses workers internally, but Lua and ordinary scene mutation remain on the engine's update path. Moving gameplay onto threads requires more than calling it from a Jolt callback.

Removing a rigid body, collider, or controller component triggers registered cleanup hooks. Destroying the scene removes its world and remaining bodies. These hooks are why removing ECS data must also clean up the matching Jolt object instead of leaving an invisible collider behind.

## Try this

Make one floor, one dynamic crate, one sensor, and one controller in a test scene. First predict which pairs interact from the layer table. Then show collider debug shapes and compare the shape positions with the visible artwork.

## Source reading route

1. [PhysicsTypes.h](../../src/Haki/Physics/PhysicsTypes.h) and [Components.h](../../src/Haki/Scene/Components.h): values and component data.
2. [PhysicsCore.cpp](../../src/Haki/Physics/PhysicsCore.cpp): process-wide Jolt support.
3. [PhysicsSystem.cpp](../../src/Haki/Systems/PhysicsSystem.cpp): `CreatePendingBodies`, `Update`, `CreatePendingCharacters`, `UpdateCharacters`.
4. [PhysicsLayers.cpp](../../src/Haki/Physics/PhysicsLayers.cpp): collision rules.
5. [PhysicsSettings.h](../../src/Haki/Physics/PhysicsSettings.h): capacities and gravity.
