# Arena 02

**Visual target:** [docs/references/kit-v2/concept-gameplay-primary.jpg](../../../../docs/references/kit-v2/concept-gameplay-primary.jpg). The rules are in [docs/artdirection.md](../../../../docs/artdirection.md). This README describes the scene and its workflow; it is not a visual approval.

**How it's built:** `arena-02.blend` is rebuilt by the recipe `tools/blender/arenas/arena_02.py` from the canyon kit v2. Change the recipe and rebuild rather than hand-editing instances.

**Generated files:** the Haki add-on's **Export Arena** produces the scene JSON, `Catalog.json`, materials, textures and `Models/arena.glb`. `Source/ground_color.png` is the unique baked ground map the recipe paints.

## Rebuild and export
Open `arena-02.blend`, then in Blender (for example through Blender MCP):

```python
import runpy; ns = runpy.run_path("tools/blender/arenas/arena_02.py")
ns["build"]()          # rebuild everything, then validate
ns["render_checks"]()  # optional gameplay-camera renders into build/arena-02-previews/
ns["export"]()         # Export Arena with import, then apply the map's environment intensity
```

Use `export()` rather than the sidebar button: the add-on always writes environment intensity 1.0, and `export()` resets it to `ENVIRONMENT_INTENSITY`.

## Layout
- **Playable area:** 24×24 m. It is authored as the north half in `NORTH` (one character per cell) and completed by 180° rotation, so both spawn sides get the same cover. The build fails if the walkable cells disconnect.
  - **Walls:** each connected wall is one continuous clay shell with one box collider per cell. There is a Z wall in the centre, L pockets holding a Haki cache and a barrel, a long wall with a return, and short straights.
  - **Water:** plus pools and stepped inlets built from the kit's water modules. Every playable water cell has a blocker.
  - **Grass:** golden patches as stepped shapes, using unrotated grass modules. Every cell has its own 1×1×0.9 m nonblocking sensor (`GrassZone_*`).
  - **Floor details:** rock-tile clusters, cacti and bones.
- **Boundary:** hidden box colliders at the playable edge, with a visible reason on each side:
  - **North-west:** a lake fed by the inlet.
  - **North and east:** fenced mesa plateaus (0.6 m).
  - **West:** a stream behind a fence.
  - **South-east:** a lake below a low foreground mesa.
- **Outskirts:** mesa tops carry clusters of broad-leaf plants with shrubs, rocks and cacti, set back from the rims. Low sand areas get sparse rocks, shrubs and bones.
- **Ground:** flat sand at z = 0 with one baked map: broad tonal variation, small grains, and contact darkening at the feet of walls, props and cliffs. Water land fills and mesa tops reuse the same map, so the floor continues seamlessly.
- **Batching:** water, grass visuals, fences and outskirts dressing are merged into 8 m chunks (fewer draw calls in the forward and shadow passes). Gameplay props stay separate objects.
- **`Player`:** a character controller running `Scripts/player.lua`, with the Elastic Brawler
  (`output/characters/elastic-brawler/model/elastic-brawler-polished.blend`) attached as its
  visual. The character's `Idle`/`Walk`/`Run` clips play through the engine's speed blend at
  the measured stride speeds (1.26 and 4.11 m/s). It runs at 5 m/s; hold **Shift** to walk.
  Its textures are scaled to 1024 px in this scene. After re-animating the character, run
  `ns["update_player"]()` then `ns["export"]()` instead of a full rebuild.

## Camera and coverage
- **Benchmark camera:** perspective, 57° pitch, 25° vertical FOV, 25.4 m from the focus (art direction section 1).
- **Terrain:** sized by `coverage()` to cover every camera footprint for any focus in the playable square at 16:9, 16:10 and 21:9, plus 4 m. `Scripts/arena_rules.lua` clamps the camera focus to the same rectangle. If you change the camera or the terrain, update both.

## Lighting
- **Sun:** one warm afternoon sun at 50° elevation from the west-north-west, so shadows fall right and slightly down the screen, as in the concept. Intensity 2.6.
- **Environment:** the default environment at 0.45.
- **Project rendering values** (`Assets/Game/Project.json`):
  - Neutral tonemapper at exposure 1.0, so authored colours read as painted.
  - Warm-neutral ambient.
  - 1024 cascaded shadows over 38 m.
  - SSAO off.
