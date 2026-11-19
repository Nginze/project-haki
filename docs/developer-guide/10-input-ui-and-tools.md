# 10. Input, UI, and development tools

[Guide index](README.md) · Previous: [Lua gameplay](09-lua-gameplay.md) · Next: [Working on the engine](11-working-on-the-engine.md)

There are three separate layers here: input tells the game what the player requested; the canvas draws game overlays; ImGui provides development controls.

## Window and input

`Window` creates a GLFW window and OpenGL context, loads OpenGL functions through GLAD, polls desktop events, and presents frames. It keeps framebuffer dimensions updated when the display changes.

`InputManager` connects devices to gainput and exposes queries for keyboard, mouse, and gamepads. It also supports C++ action mappings so an action can be associated with a device control.

The main loop polls events and updates input once per rendered frame before simulation. Input has both held state and transition state:

| Query | Meaning |
| --- | --- |
| Down | The button is held now |
| Pressed | It changed from up to down in the current input sample |
| Released | It changed from down to up in the current input sample |

Held input fits movement. A press edge fits starting an action. Remember that several simulation ticks may share one sampled frame's input, as explained in [chapter 2](02-startup-and-frame-loop.md#input-timing-matters).

Lua examples use lowercase names:

```lua
local attacking = input.mouse_down("left")
local dodgePressed = input.key_pressed("space")
local superPressed = input.key_pressed("r")
```

These queries do not implement attacks, dodges, or supers. Gameplay must connect the intent to cooldowns, movement, damage, animation, and effects.

The underlying C++ input API includes mouse position/delta/scroll and gamepad support. The Lua bindings expose a selected subset. A working C++ mouse query does not imply a Lua ground-aim API already exists.

## Camera-relative movement

For the top-angled arena camera, pressing W should usually mean moving toward the top of the screen along the ground.

The movement helper takes the camera's forward vector, removes its vertical part, normalizes it, and builds a perpendicular right vector. Input X/Y values are converted into a world XZ direction from those two vectors.

The built-in controller input combines WASD and the first gamepad's left stick. It limits the resulting input length so diagonal movement does not become faster just because two buttons are held. Its gamepad index is zero-based.

The C++ `PlayerControllerSystem` runs before Lua. If a character uses both built-in `PlayerInput` and a Lua movement script, Lua can overwrite the desired velocity. Pick one clear owner for gameplay movement rather than tuning two competing speed values.

## Who owns input while editing?

When ImGui owns keyboard or mouse input, the game gates the corresponding gameplay controls. Free-look also stops ordinary player control so WASD can move the development camera.

The debug camera owns a separate pose copied from the gameplay view when activated. It does not move the scene's primary camera component. Its projection override can also show a different view without rewriting the exported camera.

When comparing Blender and runtime framing, choose the gameplay camera and scene projection first. Otherwise you may be comparing an authored camera with a temporary development view.

## The canvas

The engine's `Canvas` queues rectangles, sprites, and text for one frame. The renderer draws them over the finished scene and clears them afterward. This is called **immediate-mode drawing**: code describes what to draw again each frame, rather than creating a permanent button object once.

Coordinates start at the output viewport's top-left. X increases right; Y increases down. Units are output pixels. Higher layers draw on top, and later calls within the same layer draw on top of earlier ones.

The canvas groups neighboring compatible quads into texture batches to reduce draw calls. A **quad** is a four-corner shape drawn as triangles. Text becomes a sequence of glyph quads.

## Screen HUD versus world labels

A screen HUD stays in a fixed screen position. A world label follows a 3D point, but is drawn at a stable pixel size facing the camera. The latter is often called a billboard.

These are callbacks you could place in a player script:

```lua
function Player:on_draw(dt)
    draw.screen_text(20, 20, "Prototype", {
        size = 20,
        color = {1, 1, 1, 1}
    })

    draw.label(self.entity:position() + vec3(0, 2, 0), "Player", {
        size = 18,
        color = {1, 1, 1, 1},
        depth_test = true
    })
end
```

`draw.label` projects the anchor point through the current camera. With depth testing enabled, scene geometry covering that anchor can hide the label. It is an anchor visibility test, not a fully simulated 3D text mesh.

The canvas uses display-color values with alpha. It is drawn after scene tone mapping, so changing scene exposure should not act as the main brightness control for the HUD.

## Fonts

`Font` reads a TrueType file and bakes glyphs into an atlas: one texture containing many character images. Drawing text looks up glyph rectangles inside that atlas.

The present font path covers printable ASCII, not a full international text shaping and localization system. Text can be scaled from its bake size, but very large scaling can look soft. This is one reason a complete game UI may eventually need more font and layout support.

## ImGui is the development interface

The runtime overlay provides hierarchy and inspector panes, graphics controls, physics debug visibility, pause/step, reload controls, script errors, and project/window settings.

Useful default shortcuts when the development UI is available:

| Shortcut | Action |
| --- | --- |
| F1 | Toggle hierarchy |
| F2 | Toggle inspector |
| F3 | Toggle telemetry; during playtest, toggle its playtest telemetry |
| F6 or Pause | Pause/resume simulation outside playtest |
| F7 | Request one simulation tick while paused |
| F11 | Enter/leave fullscreen playtest |

Playtest hides development overlays and uses the gameplay camera. Temporary fullscreen playtest is different from the saved fullscreen preference in project settings.

The editor panels overlay the game. They do not currently define a separate game viewport render target that shrinks when a pane opens.

Changing a live entity in the inspector does not automatically write that entity back to its Blender source. Project setting persistence covers a different set of values, as described in [chapter 4](04-files-and-serialization.md#what-saving-means-today).

RmlUI is a possible future game UI choice. It is not integrated in the current runtime source. The existing canvas is useful for labels, health bars, prototype HUDs, and telemetry; it is not a complete menu layout, focus, and widget framework.

## Debug drawing

`DebugDraw` queues lines and shapes such as boxes and capsules. These help answer “what does the physics system think is here?” or “which bounds are used for culling?”

Collider overlays can distinguish solid bodies, sensors, and characters. A visible mesh may be offset from its collider; showing both makes that error much clearer than guessing from collision behavior alone.

These drawings last one frame unless queued again. They are development graphics, not permanent scene meshes or exported kit assets.

## Telemetry: learning where the time goes

Telemetry uses the engine canvas and fonts. It tracks recent average/minimum FPS, frame time, CPU stages, GPU passes, graphics toggles, available GPU memory information, and dimensions.

**Milliseconds are often more useful than FPS.** A 60 FPS frame has about 16.67 ms total. A 120 FPS frame has about 8.33 ms. Saving 2 ms matters even when the FPS number changes differently at different starting rates.

CPU timings measure work on the main processor. GPU timings measure rendering work through queries and can arrive later. They are not independent values you should always add into one total: CPU and GPU work can overlap.

VRAM reporting depends on what the driver exposes; unavailable data should not be read as zero memory use. Several reported values are smoothed or refreshed periodically, so an overlay is a diagnosis aid rather than a per-instruction trace.

## Logging

`Logger` routes messages to the installed logger; startup uses `ConsoleLogger`. Lua's `log.info/warn/error` feeds into the same general reporting path with script context.

Start with the first useful error, not the last visual symptom. A missing material load can cause a skipped draw later. A disabled script instance can make movement stop while rendering and physics remain healthy.

## Try this

Pause, turn on collider drawing, and inspect the empty player controller. Step once with F7. Then compare Window, Game, and Render dimensions while changing render scale. This separates simulation behavior from picture size.

## Source reading route

1. [Window.cpp](../../src/Haki/Platform/Window.cpp): desktop window and context.
2. [InputManager.h](../../src/Haki/Input/InputManager.h): available C++ input features.
3. [Canvas.h](../../src/Haki/Renderer/Canvas.h) and [Font.h](../../src/Haki/Renderer/Font.h): drawing rules.
4. [ImGuiManager.cpp](../../src/Haki/Debug/ImGuiManager.cpp) and [EditorPanels.cpp](../../src/Haki/Debug/EditorPanels.cpp): development UI.
5. [TelemetryOverlay.cpp](../../src/Haki/Debug/TelemetryOverlay.cpp): measurements and displayed values.
6. [Logger.h](../../src/Haki/Logging/Logger.h): log interface.
