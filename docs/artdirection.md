# Art Direction: Arena Fighter

This file is the single source of truth for the game's visual direction. [palette.json](palette.json) is its color companion. Keep art direction in these two files; generated images, prompts, old reviews, and backups are not separate specifications.

**Game:** A PC-first, ten-player arena showdown game with original One Piece-inspired fighters and Haki-inspired abilities. Characters have chunky, readable proportions and grounded attacks. Keep the designs original.

**Style:** Stylized, hand-painted, simple shapes, broad color areas, soft contact shading. Warm, quiet terrain supports expressive characters and clear effects.

**Camera:** A steep, slightly angled gameplay view, like Brawl Stars. Judge the world at the size and angle the player actually sees.

**Main rule:** If it does not read from the gameplay camera, it does not ship. Gameplay clarity beats prettiness.

**Change rule:** Update this file before making an artistic change. Update `palette.json` too when a color changes, then update the affected recipes and assets. A new model or screenshot does not silently replace these rules.

## 1. Camera, scale, and readability

- Use one canonical gameplay camera profile: roughly **55–60 degrees downward**, with a near-orthographic look. Avoid a flat overhead view, a shallow angle, or a visible horizon.
- **Benchmark profile (decided 19 November 2026):** perspective projection, **57° pitch**, **25° vertical FOV**, eye **25.4 m** from the ground focus point (21.3 m high, 13.8 m behind it), which shows about **20 m of ground width at the focus at 16:9**. The narrow FOV keeps the near-orthographic look; wider aspects show more width at the same height. Do not change one of these values without re-checking scale and world coverage.
- Author that profile in Blender and preserve it through export and runtime. Runtime camera code must follow the same profile. Keep projection, pitch, view size/FOV, and height stable while judging art. A debug camera override is not the approval view.
- Keep the agreed follow behavior: preserve startup framing; begin smooth panning when a moving player approaches the screen threshold. Do not use an unsolicited startup recenter or zoom change to hide a framing problem.
- Use metres and a **1 m placement grid**. Use the current **1.8 m character controller** as the scale reference. Check visual character proportions against it; a downloaded model's original scale is not the standard.
- Players see tops and the floor. The floor fills much of the screen, so design it first. Spend prop detail on crate lids, wall tops, heads, hats, and shoulders.
- Read by silhouette and large shapes. Enlarge important props and features when needed for gameplay. Small decoration must stay quieter than the surface it sits on.
- **Shrink test:** view a gameplay screenshot at 25% size. Players, cover, crates, bushes, water, and floor must remain distinct.
- **Greyscale test:** remove color from the screenshot. The same important shapes must remain clear.

### The world must fill the view

The playable area and the visible world are different sizes. The visible world must extend beyond the playable area far enough to fill **every allowed camera position**, including the corners and the startup view.

- Check the full camera footprint on the ground, not just the point the camera aims at. Perspective and camera pitch can make the far edge see much farther than expected.
- Check the agreed window aspect ratios. At minimum test 16:9, 16:10, and 21:9, or explicitly constrain the game viewport to the supported range. Unsupported sizes need a defined framing policy.
- Extend terrain, water, and dressing beyond the union of those camera footprints, with extra margin for tall objects, shadows, and framing changes.
- Keep camera clamping and visible terrain coverage in agreement. A clamp based on player bounds alone is not sufficient.
- No bare world edge, missing floor, skybox strip, or glimpse under a terrain slab may appear during normal play. A skybox is lighting/background support, not a replacement for arena outskirts.

## 2. Contrast order

From least to most attention-grabbing:

1. **Floor:** muted, mid-brightness, low contrast; large, quiet shapes.
2. **Walls, crates, and bushes:** medium contrast; lighter tops, darker sides, clear silhouettes.
3. **Characters:** the strongest sustained saturation and silhouette contrast.
4. **Powers and attack warnings:** the brightest short-lived accents, with readable footprints and timing.

Keep most of the screen quiet so the important few things stand out. Decorative outskirts must stay below gameplay characters and effects in this order.

## 3. Color palette

- Choose authored colors from the roles in [palette.json](palette.json). It contains the shared swatches, not a requirement that every final lit pixel equal one exact hex value. Gradients, shading, and anti-aliasing naturally produce intermediate colors.
- Its starting values are **provisional**. They came from an earlier proposed palette, not an approved runtime look. Judge adjustments in the test arena against the existing warm canyon setting, characters, and lighting; update both canonical files when a choice changes.
- Shadows lean toward **cool blue-purple `#3A3550`**, never neutral black or grey. Surface pigment, such as brown wood, is separate from the color of its lighting/shadow.
- The exact **ally blue** and **enemy red**, and confusingly similar accents, are reserved for team information. Keep environment water cyan/teal and vegetation in its own family so neither suggests team ownership.
- Each power type needs a signature palette color **and** a distinct shape, such as spikes, waves, or rounded impacts. Color alone is insufficient. Add a missing power role to the palette before authoring its effects.
- A future palette-tint shader should shift each material toward its **assigned role** by at most `maxTintShift`. Do not shift an entire scene toward one color or tint team markers, characters, and powers as though they were terrain.

**Implementation distinction:** The current runtime does not load `docs/palette.json` or perform this palette-tint step. Existing kit colors are baked into palette textures/materials. The JSON is the authoring specification today; wiring it into generators and runtime is separate implementation work.

## 4. Textures and materials

- Use large painted shapes: about **4–6 stones or planks across a 1 m surface**, where that detail is needed. No photographic detail, grain, scratches, or fine noise.
- Repeating floor and wall textures must be **flat-lit**. Do not paint a directional cast shadow into a repeatable tile: rotation and repetition would make the light disagree.
- Keep gaps between stones/planks shallow and close in value to the adjacent material. Avoid a dark outline around every piece.
- Use at most **two authored material maps:** color and normal. Flat-color pieces do not need an unnecessary normal map. Use constant roughness/metalness values rather than roughness or metalness maps.
- Build a normal map from the designed pieces and their rounded edges. Separate the planks or stones first; do not turn every bright brush mark into a bump. Do not rely on an image-generated normal or raw brightness-to-height conversion.
- One-off signs/emblems should have no text, a transparent background, and a silhouette that survives the gameplay camera.
- Judge texture scale beside a character in-game. A pleasing flat image is not enough.
- Floor/wall repeating detail should use world-space size in metres so stretching a mesh does not enlarge its stones or planks. Other assets can use generated UVs or palette lookup; world mapping is not a requirement for every prop.

## 5. Shading and lighting

- Use one directional afternoon light, offset from overhead. Lock one lighting setup per map; no day/night cycle or moving cloud shadows.
- Target **two-tone shading**: a broad light side and broad shade side with a soft transition. Shade leans toward the palette's blue-purple.
- The model's real shape decides the main light/shade regions. A normal map adds detail within those regions; it must not turn a stone into mottled camouflage or break foliage into noisy light patches.
- Bake soft AO/contact darkening for each kit piece: corners, the base of walls, under crate lids. `minBakedBrightness = 0.33` means baked darkening should retain at least one third of the unoccluded brightness. It is a darkening limit, not a minimum RGB value for every pigment.
- Keep baked shading soft and local. Bake it into a unique color map or suitable vertex data without adding a third required texture. Do not bake neighboring props' shadows into a reusable floor tile.
- Put a very broad, subtle color variation across the floor to soften repetition. Avoid separate bright squares or visible tile seams.
- Shade a bush as a coherent rounded **clump mass**, with restrained leaf-level highlights. Preserve its broken leaf silhouette; do not smooth the silhouette into an egg.
- Environment, characters, and effects should share the same light direction and map-level lighting values. Deliberate emissive effects can differ in response while retaining a clear place in the contrast order.

## 6. Foliage shape and assembly

The supplied Brawl Stars foliage references show **dense overlapping clumps** with uneven heights and a broken outer contour. The green, yellow, and blue reference variants demonstrate shape and mass; their exact colors do not override the palette.

Use two related foliage families. **Outskirts plants** keep the broader leaves and natural, varied clusters shown in the approved arena concept. **Gameplay concealment grass** uses slightly thinner, tapered blades at a higher density, forming a full canopy over its established tile footprint. Keep their palette and overall shading related while making their different gameplay roles clear.

- Avoid repeated upright capsules, identical cones, a smooth cushion, or evenly spaced rows of leaves. A bush must not read as a tray of kernels or a rubber mat.
- Use a few larger clumps with smaller supporting leaves. Vary height, width, lean, rotation, and overlap deliberately; random jitter alone is not a design.
- Within gameplay grass clumps, use more overlapping, slightly narrower blades rather than a few oversized paddle-shaped leaves. Keep the patch full from above; thinner blades must not introduce visible soil gaps or become hair-like strands that shimmer at distance.
- Make the top canopy full enough to read from the steep camera. Use larger upper leaves and lower edge leaves to give a readable crown and skirt, without obvious horizontal layers.
- Keep negative spaces broad and limited. Small gaps between every leaf create flicker and visual noise from far away.
- Build compatible one-cell variants that join into straight strips, corners, L-shapes, and larger patches. Adjacent cells should read as one patch, with no obvious repeated square tops or interior borders.
- Keep gameplay footprints and height ranges consistent across variants. Vary the visual edge, not the location of the concealment zone from seed to seed.
- Concealment bushes are **nonblocking trigger regions**. Decorative plants and playable hiding zones must remain visually distinguishable. Do not give every leaf a collider.
- Spend geometry on the top outline and outer contour. Hidden internal leaves do not justify extra complexity if they add nothing at gameplay scale.

## 7. Environment kit and map outskirts

Use scripted recipes with size and random-seed settings. Fix the recipe, rebuild the shared asset, then update instances; avoid separately repairing dozens of copies. Repeated pieces need designed variants that preserve grid joins and collision dimensions.

The minimum kit contains floor tiles; straight, corner, end, and T-junction cover; crates; concealment bushes; water/shoreline pieces; fences; rocks; vegetation; and outskirts dressing. Build only what the test arena needs first.

### An arena is a place, not a cage

The map should look like a small playable part of a larger place. The supplied map-edge references use layered vegetation, raised terrain, shoreline, fences, and props beyond the fight. Carry that sense of continuation into the canyon setting; do not copy their jungle biome wholesale.

Use three connected bands:

1. **Playable arena:** clear routes, cover, concealment, crates, and readable ground.
2. **Boundary transition:** believable reasons movement stops, such as a bank, a cliff change, water, a fence section, or dense vegetation over a blocking form. Simple hidden colliders follow this visible logic.
3. **Unreachable outskirts:** continued terrain, grouped boulders, cactus/brush clusters, dry banks, distant paths, shoreline, or other restrained scenery that fills the camera view.

- Do not wrap all four sides in the same evenly spaced wall block. A repeated straight strip with decorative bumps is still a repeated strip.
- Compose **large, medium, and small masses**. Vary group lengths, setbacks, heights, orientations, and gaps. Keep the overall area intentional rather than scattering objects uniformly.
- Give each side a role: for example a rocky rise, a lower bank with brush, or a water edge. Reuse the same material/shape families so it still feels like one location.
- Let some natural forms cross the visual grid while keeping gameplay placement/collision predictable. The grid is a construction aid, not the outline of every rock and plant.
- Continue the ground behind the boundary. Never reveal empty space through a gap between border modules.
- Lower foreground masses and set tall dressing back far enough that they cannot hide the player, projectiles, or attack warnings. Test corners and lower screen edges especially carefully.
- Use simple collision volumes independent of decorative complexity. No physical spawn platforms, visible debug markers, or extra gameplay obstacles should appear as decoration.
- Changes to camera coverage must account for the **whole visible world**, not just increase the walkable floor or duplicate the existing border farther out.

## 8. Engine presentation targets

These are requirements for the final look, not a claim that each is already implemented.

- Shadows cover the entire gameplay view at the agreed camera scale, with stable, readable softness and enough detail at that distance.
- Generate mipmaps for sampled art textures, including textures created from code. Use the highest supported anisotropic filtering for angled surfaces. Render targets and other textures used for different jobs need their appropriate settings rather than blindly generating every mip chain.
- No automatic distance-based geometry simplification for this initial fixed-view arena. Make the base assets suitable for their gameplay size first; introduce LOD only after a measured need and a deliberate revision here.
- Set bloom above ordinary wall/terrain brightness, so powers and intended emissive effects glow rather than the whole arena.
- Choose exposure and tone mapping together to preserve color separation. Do not bleach sand, wood, foliage, or character highlights to the same pale color.
- Use light anti-aliasing. Gentle sharpening is optional if the final image benefits; avoid halos or noisy leaf edges.
- No tilt-shift blur, heavy vignette, or moving cloud shadows. The screen edges must remain useful for detecting enemies.
- Keep PC keyboard/mouse presentation and a minimal bottom-center ability bar. No mobile virtual sticks or touch-control clutter. The screenshot references guide world art and camera, not the PC HUD.

## 9. Look development and approval

Start with one small test arena containing a continuous floor, a wall set, crates, a joined bush patch, a character, one power effect, and a representative stretch of boundary/outskirts.

Use the same camera, character scale, lighting, exposure, and screenshot positions for each comparison. Check center, four edges, and four corners, including supported wide views. A close-up kit render can help diagnose an issue, but it cannot approve an asset.

Screenshot mode should eventually provide fixed test cameras and one switch for a plain-vs-styled comparison. Keep resolution and exposure identical. The styled version must improve gameplay readability, not just add effects.

Approval checklist:

- [ ] Passes shrink and greyscale tests through the gameplay camera.
- [ ] Floor stays quiet; characters and attack warnings remain dominant.
- [ ] Silhouettes are clear from above, including the tops of props.
- [ ] Gameplay grass has slightly thinner blades, a dense canopy, and seamless patch joins; outskirts plants retain broader, natural leaf clusters.
- [ ] Palette roles are respected; environment colors do not suggest team ownership.
- [ ] Repeating textures are flat-lit; local baked darkening retains at least one third brightness.
- [ ] No fine noise, exaggerated seams, deep repeated wall folds, or obvious floor repetition.
- [ ] Outskirts form a believable larger place rather than a uniform cage.
- [ ] No exposed world edge at startup, during movement, or at supported aspect ratios.
- [ ] Foreground decoration cannot hide gameplay; colliders and hiding zones match their visual purpose.
- [ ] Exported camera, materials, and normals give the intended result in the engine.
- [ ] The canonical files were updated before any artistic change, and affected assets were rebuilt/exported together when required.

## 10. Current implementation and next work

As reviewed on **19 November 2026**, this pass updates the specification and removes old art-direction output. It does not approve the current arena as visually finished.

| Area | Current state / required work |
| --- | --- |
| Arena coverage | The visible floor is finite and needs a camera-footprint check across startup, follow positions, and aspect ratios; extend the visible landscape and align its clamp accordingly. |
| Outskirts | Current rim/corner modules repeat around the perimeter. Replace that composition with the three bands and grouped variation described above. |
| Foliage | The arena concept's natural broad-leaf outskirts direction is approved. Gameplay concealment grass needs slightly thinner blades and greater density; update the recipe's canopy/silhouette using section 6 as the target. |
| Camera consistency | The refinement recipe sets an orthographic camera, while the current exported arena-02 scene stores a perspective camera. Choose and preserve one benchmark profile before approving asset scale or coverage. |
| Palette | Current generators have hard-coded swatches and existing exports contain baked colors. Connect future generation/material roles to `palette.json`; runtime palette loading/tinting is not present yet. |
| Shading | The current renderer is glTF PBR with shadows, environment lighting, SSAO, bloom, exposure/tone mapping, and FXAA. Two-tone shading with protected macro light/shade regions is a target, not an existing mode. |
| Texture filtering | Texture mipmap support exists; maximum supported anisotropic filtering still needs integration. |
| Look-dev tooling | Add repeatable gameplay captures and a plain-vs-styled switch as implementation work. |

Keep this status short and update it as the work lands. Do not create another art-direction folder, competing style README, or approval report to record the next decision.
