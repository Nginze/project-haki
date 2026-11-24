# Canyon kit v2

This kit is modelled from the kit sheets in [docs/references/kit-v2](../../../../../docs/references/kit-v2/). Its rules come from [docs/artdirection.md](../../../../../docs/artdirection.md), and its colours come only from [docs/palette.json](../../../../../docs/palette.json) roles.

`tools/blender/kits/canyon_kit.py` generates every piece. `tools/blender/kits/canyon_textures.py` paints every texture with numpy. To change a piece, change those scripts and rebuild; don't edit exported meshes. `preview.png` shows the join test (L/T/plus water, L grass, cover connections and props) through the gameplay camera.

## Conventions
- **Orientation:** metres, Blender Z-up. Front is −Y and right is +X. Origins are at ground level.
- **Land and water levels:** land top is z = 0 and water top is z = −0.06.
- **Placement:** put single-cell pieces at the centre of their cell. Multi-cell pieces (`cover_straight_2`, `cover_L`, `cover_T`, `bank_2x1`) use the centre of their first cell (lowest x, then lowest y) as their origin.
- **Rotation:** rotate placed pieces only in 0, 90, 180 or 270° steps. Shape variation lives in the recipe seeds.
- **Textures:** they tile and are mapped in world metres, with cell edges on tile edges. Neighbouring modules therefore continue the same strata and grain across a join at any rotation.

## Materials and textures (`Textures/`)
| Map | Use |
|---|---|
| `clay_side` (+ `_n`) | Cover sides: coral cap band with a light lip, three wavy brown strata with soft seams, and contact darkening at the base. Covers 1 m along the wall and 1 m up; v = 1 is always the wall top. |
| `clay_top` (+ `_n`) | Cover caps: quiet patches, tiling every 2 m. |
| `cliff_side` (+ `_n`) | Mesa and bank cliffs: the same strata with a sand-coloured cap band. |
| `sand` | Sand tiles and mesa tops in the kit. Arenas use their own baked ground map instead. |
| `wood` (+ `_n`), `wood_end`, `rope` | Crates, barrels, fences and caches. Plank divisions are modelled geometry; the textures carry grain, end grain and the rope twist. |
| `grass_blade` | Golden blade: deep orange roots, golden body, yellow tips. |
| `leaf`, `cactus` | Outskirts plants and cactus. |
| `water` | Shore gradient sampled by u: a light shore band at u = 0, open water at u = 1. |

Small parts use solid-colour materials: lip, iron, bone, petals, Haki violet and ring. The `haki_core` material is emissive, so the orb and crests glow through bloom.

## Pieces (`Models/`)
| Piece | Notes | Triangles |
|---|---|---|
| `cover_unit`, `cover_straight_2`, `cover_L`, `cover_T` | Sheet 01 cover: a soft rounded cap, three stepped strata and pebble bumps. | 512–932 |
| `wall_<kind>` (+ `_low`) | One-cell cover modules (`pillar`, `end`, `straight`, `corner`, `t`, `cross`) that join flush. Pick the module and rotation with `piece_for(cells, cell)`. Arenas build each wall as one shell with `cover_piece(cells)`, so cap textures don't change direction at module joins. | 120–600 |
| `ground_block`, `ground_<kind>`, `bank_2x1` | Raised mesa ground (0.6 m) with cliff strata. The bank has a wavy outline. | 58–1424 |
| `sand_tile_a`, `sand_tile_b`, `rock_tiles` | Floor tiles with stone chips, and a cluster of four flat stone slabs. | 94–132 |
| `water_fill`, `water_bank`, `water_outer`, `water_inner` | Sheet 02 modules: a rounded brown lip, a light shore band and rounded corners. Any pond, channel or inlet assembles from `water_piece(cells, cell)`, which generates the needed variant from the same shore profile. | 2–78 |
| `grass_a`, `grass_b`, `grass_<sides>`, `grass_blade` | Sheet 03 gameplay grass, with 25 broad teardrop blades per cell. Blades lean back so their faces turn toward the gameplay camera. **Place grass unrotated**: `grass_piece(cells, cell)` returns the variant for the cell's open sides (`solo`, `E`, `EN`, … `ENWS`). | ~1400 |
| `cactus_flower`, `outskirts_plant`, `small_shrub`, `rock_group`, `bone_pile` | Outskirts dressing (sheets 03 and 05). | 225–1140 |
| `crate`, `barrel`, `fence_post`, `fence_rope_1m`, `fence_2m` | Sheet 04 props. For fences, place a post on every fence point and a rope from each post toward the next (+X, rotated for other directions). | 212–2012 |
| `haki_cache`, `haki_cache_damaged`, `haki_cache_broken`, `haki_orb` | Sheet 04 caches, all on one footprint. The orb is a 0.28 m sphere hovering 0.2 m above its ground origin. | 730–2100 |

## Rebuilding
1. Open `canyon_kit.blend`, for example through Blender MCP.
2. Run:
   ```python
   import runpy; ns = runpy.run_path("tools/blender/kits/canyon_kit.py")
   ns["build_all"]()          # paints Textures/, rebuilds every piece
   ns["check_dimensions"]()   # compares sizes with the sheets
   ns["render_sheets"]()      # front/right/back/top/3-4 renders into build/kit-v2-sheets/
   ns["build_sample"](); ns["render_sample"]()   # gameplay-camera join test
   ns["export_all"]()         # one GLB per piece, with tangents for the normal maps
   ```
3. Run `tools/tests/blender_canyon_water_smoke.py` inside Blender. It checks the exported water levels, the normals (including stored vertex normals) and the shore lips for every water variant.
4. Save `canyon_kit.blend`, then rebuild and export the arenas that use the kit.
