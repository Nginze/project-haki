# 2. Startup and the frame loop

[Guide index](README.md) · Previous: [The big picture](01-big-picture.md) · Next: [Entities, transforms, and cameras](03-entities-transforms-cameras.md)

A game program repeats two jobs: update the world and draw a picture of it. Haki updates the world at a fixed rate and draws as often as the outer loop allows.

## What happens at startup

`main()` installs a console logger, constructs `Game`, and calls `Run()`. The default project path is `Assets/Game/Project.json`.

Project settings load before the window is created. That lets saved dimensions, title, and window mode take effect from the start.

`Game::InitializeSystems()` then prepares the engine in this order:

1. Confirm that the window and OpenGL context are usable.
2. Start the asset manager and register its loaders.
3. Start the shared Jolt support through `PhysicsCore`.
4. Connect input to the window.
5. Create renderer resources.
6. Load fonts and prepare telemetry.
7. Start the optional ImGui development interface.
8. Read the project scene manifest, register scene factories, and request the entry scene.

A **context** here is the connection between this thread and the OpenGL state for the window. GPU objects such as textures cannot be created before this is ready.

Registering a scene stores a way to create it later. The entry scene becomes active when the scene manager processes its request during an update.

## A frame and a tick are different

A **rendered frame** is one picture presented to the window. A **simulation tick** is one step of gameplay and physics.

Haki's fixed tick is `1 / 60` second, about 16.67 milliseconds. The drawing rate can be higher or lower.

At 120 rendered frames per second, some frames have no simulation step. At 30 rendered frames per second, a frame will commonly perform two simulation steps. Those are examples of normal timing, not promises about exact scheduling.

This helps physics because it sees a regular time step. It also makes gameplay easier to reason about: movement speed is measured in units per second instead of units per picture.

```text
distance = speed × seconds
5 metres/second × 1/60 second = about 0.083 metres per tick
```

## The accumulator

The main loop keeps an amount of unprocessed simulation time called an **accumulator**.

```text
measure time since the last frame
add a capped amount of that time to the accumulator

while at least one fixed step remains, up to 8 steps:
    update the scene by 1/60 second
    subtract 1/60 second

draw once
```

The code caps a single frame's added simulation time at 0.25 seconds. It also limits catch-up work to 8 ticks per frame, then discards excess whole steps. Otherwise one slow frame could cause so much catch-up work that the engine never recovers.

That means a very large stall can make simulation time fall behind real time. This is a deliberate limit on work, not a network rollback system. The loop currently does not interpolate between two physics snapshots when drawing.

## What happens in one outer frame

The simplified order in `Game::Run()` is:

1. Measure elapsed time and poll window events.
2. Update input once.
3. Build the editor UI, handle its requests, and check project setting saves.
4. Decide whether gameplay or the debug camera owns input.
5. Run zero or more fixed simulation ticks.
6. Update the optional free-look camera for this rendered frame.
7. Poll shader and script files for changes.
8. Queue telemetry, render the scene, and draw ImGui on top.
9. Swap the window buffers to present the picture.

**Buffer swapping** makes the completed image visible while the next image is drawn elsewhere. It avoids showing a half-drawn frame.

## The order inside one simulation tick

The main loop first runs `PlayerControllerSystem::Update()`. This can fill desired movement from built-in player input. `SceneManager::Update()` then advances scene transitions and calls the scene's `OnUpdate()` and `UpdateRuntime()`.

Within `Scene::UpdateRuntime()` the order is:

| Stage | What it does | Why the order matters |
| --- | --- | --- |
| Prepare scene systems | Lazily create physics and Lua support when needed | Components need runtime systems before they can act |
| Lua update | Start new scripts and run gameplay callbacks | Scripts can request movement before physics |
| Physics | Create pending bodies, move controllers, step Jolt, copy results back | Collision decides the movement that really happened |
| Contact callbacks | Deliver collision and trigger events to scripts | Events now describe the completed physics step |
| Transform hierarchy | Move attached children with their parents | A visual follows the controller's corrected position |
| Animation | Evaluate clips and locomotion | Locomotion can use actual speed after collision |

A camera moved from Lua's `on_update` sees the player's state before that tick's physics step. If you investigate subtle camera lag, this order is one place to look. A future dedicated post-physics gameplay phase would be a new feature.

The base C++ `Scene::OnUpdate()` is empty. Engine systems run through `UpdateRuntime()`, so a scene subclass does not have to call those systems itself.

## Pausing

When simulation is paused, the runtime step uses a delta time of zero unless a single step was requested. Scripts do not advance, contacts are not dispatched again, and physics does not simulate.

Physics can still create newly added bodies. Transform and animation evaluation still run with zero time so the scene can remain visible and editable. Drawing also continues. Lua `on_draw` receives zero time while paused.

## Input timing matters

Input is sampled once per outer frame. Several catch-up ticks can therefore see the same sampled state. A call such as `input.key_pressed()` is a frame edge, not a separately consumed event for each simulation tick.

For held movement this is normally fine. For actions such as firing once per press, use an explicit action/cooldown policy and be aware of catch-up ticks. Do not assume the engine already has a queued input command for every simulation tick.

## Shutdown and lifetime

The active scene shuts down before shared physics support is destroyed. Renderer and asset GPU resources are released while the window's OpenGL context still exists. Input disconnects before the window disappears.

This is why member order and shutdown order matter. Calling `glDeleteTextures` after destroying the OpenGL context would be a lifetime error even if the texture's C++ destructor itself looked correct.

## Try this

Trace `Game::Run()` → `SceneManager::Update()` → `Scene::UpdateRuntime()`. Find the line that calls Lua and the line that calls physics. Ask: if Lua requests a velocity, which later stage decides whether a wall stops it?

## Source reading route

1. [Game.cpp](../../src/Haki/Core/Game.cpp): `InitializeSystems`, `Run`, and `ShutdownSystems`.
2. [Scene.cpp](../../src/Haki/Scene/Scene.cpp): `UpdateRuntime` and `OnRender`.
3. [SceneManager.cpp](../../src/Haki/Scene/SceneManager.cpp): `Update`.
4. [PhysicsCore.cpp](../../src/Haki/Physics/PhysicsCore.cpp): shared Jolt startup and worker threads.
