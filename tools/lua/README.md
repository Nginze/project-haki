# Haki Lua editor support

`library/haki.lua` describes the C++ API in `src/Haki/Scripting/ScriptBindings.cpp` using LuaCATS.
It is editor metadata, not an implementation: **never require/dofile it in a gameplay script**.
The root `.luarc.json` configures Lua 5.4, this library and `the scene Scripts folder` module resolution.
Definitions provide signatures, return types, optional values, vector/quaternion operators,
entity methods, input, physics, scene, logging, time, and script callback types. Tests detect
missing definitions when bound member names change; update signatures here when C++ signatures change.

## VS Code

Open the **engine-sandbox folder**, not only a loose Lua file or the Assets folder. Install the
recommended **Lua** extension by **Lua Language Server** (`sumneko.lua`). Existing C++ settings are
unchanged. Reload the window or restart the Lua language server after configuration changes.

## Vim / Neovim

Use Lua Language Server (`lua_ls`) through your editor's LSP client. This machine already has
LuaLS under `~/.local/share/nvim/mason/bin/lua-language-server`. Make the project folder the LSP
workspace root and allow it to read `.luarc.json`. Restart the server (`:LspRestart` with nvim-lspconfig)
or reopen the editor. A syntax highlighter alone does not provide completion. An independent luacheck
linter does not consume LuaLS definitions; check the diagnostic's source if undefined-global warnings persist.
No global user/editor configuration is changed by these project files.

## Type an entity behaviour

```lua
---@class MyPlayer : HakiEntityScript
---@field speed number
local Player = { properties = { speed = 5 } }

---@param dt number
function Player:on_update(dt)
    self.entity:set_move_velocity(vec3(0, 0, -self.speed))
end

return Player
```

`HakiEntityScript` supplies the injected `self.entity` type. Declare injected property fields on your
class too: LuaLS cannot infer that the runtime copies a `properties` table onto each instance.
Use `HakiSceneScript` for scene behaviours; they do **not** receive `self.entity`.
Entity contact callbacks receive `other`; scene contact callbacks receive both entities.
Declare arguments on your implementations (e.g. `---@param other HakiEntity`) for clear hover/completion.
If another entity's script class is known, narrow `entity:script()` with a `---@cast` annotation.
The existing player, hazard, scene rules and movement helper demonstrate these patterns.

Try completion after `scene.`, `physics.`, `vec3.`, or `self.entity:` inside an annotated behaviour.
Definitions describe the actual runtime, including radians for rotation and gamepads indexed from zero.
They do not disable genuine undefined-global or type diagnostics.

For a command-line check with LuaLS installed:

```sh
lua-language-server --check=. --configpath=.luarc.json --logpath=/tmp/haki-luals-log --metapath=/tmp/haki-luals-meta
```

## Arena camera thresholds

`sandbox_rules.lua` preserves the Blender framing while the player is stationary or inside the
screen's safe region. `edge_threshold` is the fraction of the distance from center to edge that
starts a pan (default 0.65); lower values pan earlier. `center_threshold` stops following once the
moving player returns near the center (default 0.15). `pan_sharpness` controls the active pan speed.
Stopping horizontal movement freezes the camera immediately. No projection or FOV override is applied.
`scene.viewport_aspect()` reports the current window width/height, so these screen thresholds adapt
to viewport resizing. Headless scenes default to 16:9 and can set their size through ScriptSystem.

Scene modules resolve locally; `shared.lib.name` resolves from Game/Shared/Scripts/lib/name.lua.
