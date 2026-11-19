# Haki developer handbook

This is an onboarding guide to **the engine in this repository**. It explains what the code does, why the main parts exist, and where to look when you want to change something.

You do not need to understand the whole renderer before making a game. Read the first three chapters, then follow the chapters that match your work. New terms are explained where they first matter. The [glossary](glossary.md) is there when you need a reminder.

These pages describe the code reviewed on **17 November 2026**. Source links are the reference when later changes make a detail outdated. Examples marked as teaching examples are separate from the playable scenes.

The game's visual requirements live in [artdirection.md](../artdirection.md), with shared colors in [palette.json](../palette.json). This handbook explains the implementation; it does not replace that visual specification.

## Reading order

| Chapter | What you will understand |
| --- | --- |
| [1. The big picture](01-big-picture.md) | What belongs to the engine, the game, Blender, and the import tools |
| [2. Startup and the frame loop](02-startup-and-frame-loop.md) | How the program starts, runs, and shuts down; why simulation and drawing use different clocks |
| [3. Entities, transforms, and cameras](03-entities-transforms-cameras.md) | How objects are represented, how parenting works, and how a camera sees the world |
| [4. Files and serialization](04-files-and-serialization.md) | How JSON becomes a running scene, how validation works, and what gets saved |
| [5. Assets and Blender](05-assets-and-blender.md) | How models, materials, catalogs, handles, and Blender exports fit together |
| [6. The renderer](06-renderer.md) | How triangles become pixels, and what each rendering pass adds |
| [7. Physics](07-physics.md) | How Jolt bodies, colliders, controllers, queries, and contacts work |
| [8. Animation](08-animation.md) | How clips move a mesh, how locomotion blends, and how to play an action |
| [9. Lua gameplay](09-lua-gameplay.md) | Script ownership, callbacks, bindings, properties, modules, and reloads |
| [10. Input, UI, and development tools](10-input-ui-and-tools.md) | Keyboard and controller input, canvas drawing, editor panels, and telemetry |
| [11. Working on the engine](11-working-on-the-engine.md) | Build commands, debugging routes, tests, and how to add a feature across the pipeline |
| [Glossary](glossary.md) | Short definitions of terms used in the guide |

## Choose a shorter route

**I want to build the game:** 1 → 3 → 5 → 7 → 9 → 8 → 10. Keep chapter 11 nearby.

**I want to learn the engine internals:** read in order. Follow one or two source links per chapter; you do not need to read every linked file at once.

**I want to learn graphics:** 3 → 6 → 8. Chapter 6 builds from meshes and coordinate spaces up to lighting, shadows, and post-processing.

**I want to understand saving and loading:** 4 → 5 → 2. These separate data on disk, loaded resources, and live scene state.

## Find an answer quickly

| Question | Start here |
| --- | --- |
| What happens after I press Run? | [Startup](02-startup-and-frame-loop.md) |
| Why are there several JSON files? | [File responsibilities](04-files-and-serialization.md#the-files-have-different-jobs) |
| Why does changing Blender not change the engine yet? | [Export and import](05-assets-and-blender.md#from-blender-to-a-running-scene) |
| Why does the player need both a controller and a visual child? | [Player structure](03-entities-transforms-cameras.md#a-player-is-usually-more-than-one-entity) |
| Who decides where a moving object is? | [Physics ownership](07-physics.md#who-controls-the-transform) |
| How do I play an attack animation? | [Animation actions](08-animation.md#playing-an-action-from-lua) |
| What does a renderer setting mean? | [Rendering passes](06-renderer.md#the-rendering-passes-in-order) |
| Why is something wrong only in the engine? | [Debugging routes](11-working-on-the-engine.md#debug-by-following-the-data) |

## How to use the source links

Each chapter ends with a short source reading route. Start at the public header to see the system's job, then read the named function in the `.cpp` file. Source links use file paths rather than line numbers so normal code edits do not immediately break them.

The diagrams use Mermaid, which many Markdown readers can display. Every diagram also has an explanation in normal text, so the guide remains usable in a plain text editor.

## What this guide does and does not claim

The engine already supports imported scenes, rendering, Jolt physics, character movement, skeletal animation, Lua scripts, and development tools. The intended game is a PC arena showdown game with Blender as its level editor.

The current source does **not** contain a complete multiplayer stack, world streaming system, audio system, or RmlUI integration. Ten-player online matches, full Haki combat, and a finished game UI remain game and engine work ahead. An architecture explanation should not make those sound finished.

When updating a system, update its chapter and any examples that cross into another system. For a new component, use the [feature checklist](11-working-on-the-engine.md#adding-a-feature-across-the-whole-pipeline).
