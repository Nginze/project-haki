# 9. Lua gameplay

[Guide index](README.md) · Previous: [Animation](08-animation.md) · Next: [Input, UI, and tools](10-input-ui-and-tools.md)

Lua is where you can change game rules quickly. C++ provides controlled access to engine features; Lua uses that access to build behaviors.

Each scene has its own `ScriptSystem` and Lua state. Entity scripts run as separate instances inside that shared scene state. They are not separate Lua virtual machines for every entity.

## How a C++ binding works

A **binding** connects a Lua name to a C++ function. For example:

```text
Lua: entity:set_move_velocity(value)
    → C++ binding validates the entity and component
    → writes CharacterControllerComponent.DesiredVelocity
    → the later physics step consumes that request
```

The Lua call is not a second physics engine. It is a friendlier way to reach the existing engine system. A missing component can therefore produce an error even when the Lua method name is valid.

The API is registered in `ScriptBindings.cpp` using sol2. Its main groups are `vec3`, `quat`, entity methods, `scene`, `physics`, `input`, `draw`, `time`, and `log`.

## Anatomy of a script

A behavior file returns a table. The runtime loads that table, creates an instance table for each attached entity, injects `self.entity`, copies property defaults, and applies authored overrides.

This movement example belongs on an entity with a character controller:

```lua
local movement = require("shared.lib.movement")

---@class LearningPlayer : HakiEntityScript
---@field speed number
local Player = { properties = { speed = 5.0 } }

function Player:on_update(dt)
    local direction = movement.camera_relative(
        input.move(), scene.primary_camera()
    )

    self.entity:set_move_velocity(direction * self.speed)
    if direction:length() > 0.001 then
        self.entity:set_rotation(quat.look_rotation(direction))
    end
end

return Player
```

There is no `dt` multiplication on the requested velocity. The value is already metres per second; physics applies the time step. For manually integrating a position, `position + velocity * dt` would need the multiplication.

In Lua, `Player:on_update(dt)` is shorthand for a function that receives `self` first. Likewise `entity:position()` passes the entity as the method's first argument. This is why replacing `:` with `.` arbitrarily breaks calls.

## Properties become instance fields

`properties = { speed = 5 }` defines a default. A scene's script component can override it for one entity. During execution, read `self.speed`.

The supported authored property values are booleans, numbers, strings, and vec3 values. The runtime makes appropriate copies of property values rather than sharing every mutable vector accidentally.

State such as cooldown timers also belongs on `self`:

```lua
function Player:on_start()
    self.cooldown = 0
end

function Player:on_update(dt)
    self.cooldown = math.max(0, self.cooldown - dt)
end
```

These are separate illustrative callbacks, not additions that should overwrite the movement callback without combining their contents.

Avoid using one mutable module-level variable as if it were private state for every attached entity. A loaded class/module can be shared by several script instances.

## Entity scripts and scene scripts

An **entity script** belongs to one entity. Movement, a crate's health, or a hazard's local behavior fits there.

A **scene script** belongs to the arena as a whole. Match rules, coordinating a camera, or a shared round timer fits there. It does not receive `self.entity`; it finds entities through `scene` queries.

Both are authored through the Blender add-on and exported as paths. Disabling a script in Blender only changes runtime behavior after the relevant export/import updates the scene component.

## Callback timing

| Callback | When it runs | Arguments after `self` |
| --- | --- | --- |
| `on_start` | Once when a new instance starts on a simulation update | None |
| `on_update` | Each positive simulation tick | Fixed `dt` |
| `on_contact_begin/end` | After physics, for solid contact events | Other entity; scene script gets both |
| `on_trigger_enter/exit` | After physics, for sensor contact events | Other entity; scene script gets both |
| `on_draw` | Once per rendered scene frame after the instance has started | Render-frame `dt`, or zero when paused |
| `on_destroy` | When an entity's running script component is removed/destroyed | None |

The scene script starts before entity scripts and its update runs before entity updates. Do not rely on a meaningful order between unrelated entity scripts. Coordinate explicitly when one behavior must wait for another's result.

`ScriptSystem::Shutdown()` clears scene state rather than acting as a guaranteed per-script save callback. Do not use `on_destroy` as the only way to persist important game data when the entire scene or program exits.

Drawing belongs in `on_draw`, which receives a temporary canvas context. Calling `draw.*` from `on_update` raises an error.

## Finding and talking to entities

```lua
local player = scene.find("player")
if player and player:valid() then
    log.info(player:name(), player:position())
end
```

This example assumes the authored stable ID is literally `player`. Blender-authored IDs are often UUIDs. `scene.find_by_name("Player")` is convenient for a prototype, but repeated names can make the result ambiguous.

Other helpers include `scene.primary_camera()`, `scene.characters()`, and `scene.entities_with_script(path)`. `entity:script()` returns its running instance table if it has one, allowing game behaviors to call known methods on each other.

Always consider lifetime. A cached entity may have been destroyed. Lua entity methods validate access, but your game should still check optional results and `valid()` where appropriate.

Lua destruction requests are queued until the current callback batch ends. This avoids destroying registry entries in the middle of the loop that is visiting them. Do not assume `destroy()` removes the entity in the middle of the very next Lua expression.

## Script paths and modules

Scene-local script paths start at the scene's `Scripts/` folder:

```text
player.lua
lib/combat.lua
```

Reusable modules live under `Assets/Game/Shared/Scripts`:

```lua
local movement = require("shared.lib.movement")
```

The custom `require` resolves the module, runs it once, and caches its returned value. It supports local modules too, such as `require("lib.combat")`. Module names use dots; script component paths use relative file paths ending in `.lua`.

The Lua state opens basic, math, string, table, coroutine, and UTF-8 libraries. It does not expose normal `io`, `os`, `debug`, or `package` access, and general file-loading helpers are removed. Modules are resolved through Haki's allowed roots. This is controlled gameplay scripting, not a general-purpose shell environment.

## Errors and reloads

A callback error is logged with the script path, entity ID when available, callback name, and Lua stack trace. The failing instance is disabled until a successful script reload. This avoids printing the same failure every frame.

The game polls for script changes at roughly half-second intervals when reload is enabled. A successful reload replaces class functions while preserving the instance table. Existing health values, timers, and other fields can remain.

That also means reload is **not the same as restarting the scene**. Changing a property default or adding initialization in `on_start` does not necessarily reset existing instances or rerun their startup. Reload the scene when you need fresh state.

Editing an already loaded module causes the system to clear module caches and reload script classes, because those scripts may have captured the module in a local variable. This is broader than reloading only one behavior file.

Shader hot reload is separate and follows shader source files through the asset manager. Meshes, colliders, and imported scene structure do not all gain automatic live reload because scripts do.

## Why the IDE understands the bindings

The real implementation is C++. Lua Language Server cannot discover that API just by reading your gameplay files.

`tools/lua/library/haki.lua` supplies type and method descriptions. `.luarc.json` tells the editor to include them and use Lua 5.4. These descriptions provide completion and help catch incorrect arguments; they do not implement the engine API.

Never `require` the definition file in gameplay. Open the repository root in your editor and use LuaLS. A syntax highlighter alone cannot provide the same completion. See the [Lua setup guide](../../tools/lua/README.md).

When adding a C++ binding, update the definitions and meaningful tests in the same change. Otherwise the runtime and IDE tell different stories.

## Current API boundaries

The runtime currently exposes entity lookup, movement, selected body operations, camera settings, animation, queries, simple drawing, input, logging, and time. It does not expose every C++ function automatically.

For example, a C++ scene-manager feature is not automatically a Lua `scene` method. General entity spawning, a full UI system, mouse-to-ground aiming, and network commands need their own supported APIs where the current bindings do not provide them.

## Try this

Read a simple arena player script. For each engine method, find the matching binding and identify the component it reads or writes. This makes missing-component errors much easier to understand.

## Source reading route

1. [ScriptSystem.h](../../src/Haki/Scripting/ScriptSystem.h): lifecycle and reload contract.
2. [ScriptSystem.cpp](../../src/Haki/Scripting/ScriptSystem.cpp): instance creation, callbacks, modules, and reloads.
3. [ScriptBindings.cpp](../../src/Haki/Scripting/ScriptBindings.cpp): the actual exposed API.
4. [ScriptPaths.h](../../src/Haki/Scripting/ScriptPaths.h): local/shared path resolution.
5. [haki.lua](../../tools/lua/library/haki.lua): searchable signatures and callback types.
6. [Game/Shared/Scripts](../../Assets/Game/Shared/Scripts): reusable gameplay helpers.
