# Art Direction: Arena Fighter

This file is the single source of truth for the game's visual direction. [palette.json](palette.json) is its color companion. Keep art direction in these two files; generated images, prompts, old reviews, and backups are not separate specifications.

**Game:** A PC-first, ten-player arena showdown game with original One Piece-inspired fighters and Haki-inspired abilities. Characters have chunky, readable proportions and grounded attacks. Keep the designs original.

**Style:** Clearly three-dimensional, stylized game rendering matching the supplied Brawl Stars gameplay references. Solid chunky geometry, simple textured surfaces, readable side faces, and soft contact shading. Hand-painted texture work supports the 3D forms; the final image must not look like a flat 2D cartoon, brush-painted illustration, or watercolor concept.

**Reference priority (24 November 2026):** The user approved the fresh gameplay concept derived directly from the latest Brawl Stars screenshots: peach sand, coral clay walls, golden blade-built grass, varied grid-shaped blue water, and solid 3D fighters with low, tactile Haki effects. Use that concept and those screenshots as the visual benchmark. Earlier painterly/generated arena concepts remain rejected references.

**Camera:** A steep, slightly angled gameplay view, like Brawl Stars. Judge the world at the size and angle the player actually sees.

**Main rule:** If it does not read from the gameplay camera, it does not ship. Gameplay clarity beats prettiness.

**Change rule:** Update this file before making an artistic change. Update `palette.json` too when a color changes, then update the affected recipes and assets. A new model or screenshot does not silently replace these rules.

## 1. Camera, scale, and readability

- Use one canonical gameplay camera profile: roughly **55–60 degrees downward**, with a near-orthographic look. Avoid a flat overhead view, a shallow angle, or a visible horizon.
- **Benchmark profile (decided 19 November 2026):** perspective projection, **57° pitch**, **25° vertical FOV**, eye **25.4 m** from the ground focus point (21.3 m high, 13.8 m behind it), which shows about **20 m of ground width at the focus at 16:9**. The narrow FOV keeps the near-orthographic look; wider aspects show more width at the same height. Do not change one of these values without re-checking scale and world coverage.
- Author that profile in Blender and preserve it through export and runtime. Runtime camera code must follow the same profile. Keep projection, pitch, view size/FOV, and height stable while judging art. A debug camera override is not the approval view.
- Keep the agreed follow behavior: preserve startup framing; begin smooth panning when a moving player approaches the screen threshold. Do not use an unsolicited startup recenter or zoom change to hide a framing problem.
- Use metres and a **1 m placement grid**. Use the current **1.8 m character controller** as the scale reference. Check visual character proportions against it; a downloaded model's original scale is not the standard.
- Modular placement uses fixed axis alignment and **0/90/180/270-degree yaw**. Do not add random tilt, lean, or arbitrary yaw to gameplay walls, grass tiles, water tiles, crates, or barriers. Small shape variation belongs in the asset recipe; a camera exposing side faces does not mean a placed object should be slanted.
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

Quiet does not mean flat, grey, or lifeless. Keep warm light, controlled saturation, and broad painted material variation so sand, wood, and stone register at gameplay distance. Protect the contrast order through the size and strength of those details rather than removing them.

## 3. Color palette

- Choose authored colors from the roles in [palette.json](palette.json). It contains the shared swatches, not a requirement that every final lit pixel equal one exact hex value. Gradients, shading, and anti-aliasing naturally produce intermediate colors.
- Swatches now follow the approved fresh 3D concept: warm peach sand, coral clay, golden gameplay grass, green outskirts plants, and blue/cyan water. Hex values remain approximate authoring targets until checked under benchmark lighting; update both canonical files when a choice changes.
- Shadows lean toward **cool blue-purple `#3A3550`**, never neutral black or grey. Surface pigment, such as brown wood, is separate from the color of its lighting/shadow.
- The exact **ally blue** and **enemy red**, and confusingly similar accents, are reserved for team information. Keep environment water cyan/teal and vegetation in its own family so neither suggests team ownership.
- Each power type needs a signature palette color **and** a distinct shape, such as spikes, waves, or rounded impacts. Color alone is insufficient. Add a missing power role to the palette before authoring its effects.
- A future palette-tint shader should shift each material toward its **assigned role** by at most `maxTintShift`. Do not shift an entire scene toward one color or tint team markers, characters, and powers as though they were terrain.

**Implementation distinction:** The current runtime does not load `docs/palette.json` or perform this palette-tint step. Existing kit colors are baked into palette textures/materials. The JSON is the authoring specification today; wiring it into generators and runtime is separate implementation work.

## 4. Textures and materials

- Use large painted shapes: about **4–6 stones or planks across a 1 m surface**, where that detail is needed. No photographic detail, grain, scratches, or fine noise.
- Sand needs restrained surface-tone variation, sparse small ground accents, and a subtle larger tonal change across the map, as in the gameplay references. Avoid conspicuous brush strokes or swirled painted paths. Wood needs readable plank divisions; stone/clay needs solid planes, subtle surface marks, and shallow side-layer variation. Material detail must read as part of a 3D game surface at gameplay distance.
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

Use two related foliage families. **Outskirts plants** use broader leaves and natural, varied clusters. **Gameplay concealment grass** uses slightly thinner, tapered blades at a higher density, forming a full canopy over its established tile footprint. Match the supplied gameplay screenshots' individual 3D blade forms and soft darkening near their bases. Keep both families' palette and overall shading related while making their different gameplay roles clear.

- Avoid repeated upright capsules, identical cones, a smooth cushion, or evenly spaced rows of leaves. A bush must not read as a tray of kernels or a rubber mat.
- Use a few larger clumps with smaller supporting leaves. Vary height, width, lean, rotation, and overlap deliberately; random jitter alone is not a design.
- Within gameplay grass clumps, use more overlapping, slightly narrower blades rather than a few oversized paddle-shaped leaves. Keep the patch full from above; thinner blades must not introduce visible soil gaps or become hair-like strands that shimmer at distance.
- Form the whole gameplay patch from individual overlapping blades, including the sides and lower perimeter. Its footprint can remain tile-shaped, but a solid green cube with blades attached on top is not acceptable.
- Make the top canopy full enough to read from the steep camera. Use larger upper leaves and lower edge leaves to give a readable crown and skirt, without obvious horizontal layers.
- Keep negative spaces broad and limited. Small gaps between every leaf create flicker and visual noise from far away.
- Build compatible one-cell variants that join into straight strips, corners, L-shapes, and larger patches. Adjacent cells should read as one patch, with no obvious repeated square tops or interior borders.
- Keep gameplay footprints and height ranges consistent across variants. Vary the visual edge, not the location of the concealment zone from seed to seed.
- Concealment bushes are **nonblocking trigger regions**. Decorative plants and playable hiding zones must remain visually distinguishable. Do not give every leaf a collider.
- Spend geometry on the top outline and outer contour. Hidden internal leaves do not justify extra complexity if they add nothing at gameplay scale.

## 7. Environment kit and map outskirts

Use scripted recipes with size and random-seed settings. Fix the recipe, rebuild the shared asset, then update instances; avoid separately repairing dozens of copies. Repeated pieces need designed variants that preserve grid joins and collision dimensions.

The minimum kit contains floor tiles; straight, corner, end, and T-junction cover; crates; concealment bushes; water/shoreline pieces; fences; rocks; vegetation; and outskirts dressing. Modeling sheets must show consistent front, right, back, top, and three-quarter views of the same asset, with dimensions and matching join profiles. Long walls and shaped water areas are assemblies of shared grid pieces, not separately improvised shapes.

Modeling-sheet dimensions use metres, ground-level origins, and Blender Z-up. Front is the -Y side, right is +X, back is +Y, and top is +Z; the three-quarter view shows front/right/top. Use the same orientation in every row.

| Family | Initial modeling dimensions |
| --- | --- |
| Floor and water modules | 1×1 m, surface at ground level; floor thickness is hidden in assembly |
| Cover | 1 m cell width, 1 m high; straight 2×1 m, L corner occupies three cells of 2×2 m, T occupies four cells of 3×2 m |
| Gameplay grass | 1×1 m patch, roughly 0.55–0.75 m high; two compatible blade-layout variants |
| Crates and Haki caches | 0.9×0.9×0.9 m; same footprint for intact/damaged/broken states |
| Barrel | About 0.65 m diameter and 0.9 m high |
| Fence | 1 m post-to-post repeat, about 0.8 m high |
| Haki orb | About 0.28 m solid diameter; glow does not define its mesh size |

Water straight/outer-corner/inner-corner bank profiles share the same shallow recess and narrow shore treatment. Show assembled L/T shapes to explain connections; they are examples, not additional one-off meshes. Decorative outskirts foliage, banks, and rock groups can have irregular silhouettes while retaining reproducible placement origins.

For water assembly, land top is 0.00 m and water top is -0.06 m. Any deeper bank/soil body shown in an isolated side view is buried construction, not a raised perimeter wall. Use these written dimensions and verify joins in Blender; generated turnaround drawings are visual references rather than measured CAD projections.

### Breakable Haki caches and pickups

Use **Haki Orbs** as the working pickup name. Breakable **Haki Caches** drop them; collecting orbs increases combat power and durability. Exact numbers and ability interactions remain gameplay tuning work.

- A cache shares the ordinary crate's footprint and chunky wooden construction, but has a simple inset violet orb/Willpower crest visible on its lid and sides. Keep the mark geometric and original, with no writing or borrowed anime symbol.
- Show intact, visibly damaged, and broken/debris states with matching construction and a readable orb pickup. Keep gameplay collision simple and separate from the debris visuals.
- The pickup is a compact violet sphere with a bright core and a restrained energy accent, readable without its glow. It must look different from team markers and must not be a copied green power cube.
- Show the orb's unlit solid shape as well as its lit gameplay appearance, so a modeling tool can reconstruct geometry without treating the effect as a hard shell.

### Armament Haki: fighter-specific coating

Armament visually coats the fighter's actual attacking surface in glossy dark violet/near-black, with a thin luminous violet rim and a short tactile impact accent. The coating must remain readable at gameplay scale without becoming a large aura or hiding the underlying 3D form.

- Physical brawler: striking fist and relevant forearm.
- Swordsman: the sword blade; hands and arms retain their normal materials.
- Kicker: the striking foot and relevant lower leg; arms remain uncoated.
- Reinforce the fighter's existing attack style rather than giving every character the same glowing fist. The eventual engine material effect needs fighter-specific affected regions; a generic whole-character color overlay is insufficient.
- Armament strengthens attacks. A defensive benefit is a possible gameplay extension, not a decided rule or numeric bonus yet.

### Observation Haki: brief nearby sensing

Show a restrained cyan radar ring moving outward along the ground from the user. Nearby opponents hidden in grass briefly appear as translucent cyan 3D silhouettes. Keep the grass present so the reveal is understandable. This information is the sensing player's view, not an effect that gives every opponent the same information. Activation conditions, range, duration, cooldown, and any dodge interaction remain undecided here.

### Conqueror's Haki: short orb-scaled burst

Show a composed fighter releasing a short, ground-hugging dark shockwave with crimson-violet lightning accents. Nearby fighters with substantially fewer Haki Orbs can suffer a heavy hit or knockdown; show stronger/equal-orb opponents resisting or being pushed back while remaining upright. Small orb-count badges can explain that comparison in a concept scene.

The burst radius, minimum orb count to activate it, damage, and whether weaker targets can be immediately knocked out are balance questions, not fixed rules yet. A concept can illustrate a six-orb user affecting a two-orb rival without establishing those values as a shipping threshold. No screaming power-up, floating aura pillar, or screen-filling blast that hides the arena.

### An arena is a place, not a cage

The map should look like a small playable part of a larger place. The supplied map-edge references use layered vegetation, raised terrain, shoreline, fences, and props beyond the fight. Carry that sense of continuation into the canyon setting; do not copy their jungle biome wholesale.

Use three connected bands:

1. **Playable arena:** clear routes, cover, concealment, crates, and readable ground.
2. **Boundary transition:** believable reasons movement stops, such as a bank, a cliff change, water, a fence section, or dense vegetation over a blocking form. Simple hidden colliders follow this visible logic.
3. **Unreachable outskirts:** continued terrain, grouped boulders, cactus/brush clusters, dry banks, distant paths, shoreline, or other restrained scenery that fills the camera view.

- Do not wrap all four sides in the same evenly spaced wall block. A repeated straight strip with decorative bumps is still a repeated strip.
- Compose **large, medium, and small masses**. Vary group lengths, setbacks, heights, orientations, and gaps. Keep the overall area intentional rather than scattering objects uniformly.
- Give each side a role: for example a rocky rise, a lower bank with brush, or a water edge. Reuse the same material/shape families so it still feels like one location.
- Compose ponds with their banks, nearby vegetation, and surrounding paths. Preserve useful routes, but avoid identical isolated water squares that look scattered onto the sand; broad bank shapes and a shared ground treatment should connect them to the place.
- Use varied grid-built water shapes: stepped inlets, connected stretches, and T/L-shaped areas with softened corners, as in the references. Water placement must help compose routes and cover pockets. Avoid turning every pool into a little round basin with an ornamental rim.
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

As reviewed on **19 November 2026**. The arena-02 look-development pass (palette wiring, cover walls, foliage, outskirts, camera, render settings) has landed; it is not yet approved as visually finished.

| Area | Current state / required work |
| --- | --- |
| Arena coverage | arena-02 terrain is sized from the union of benchmark-camera footprints over every focus in the playable square at 16:9, 16:10 and 21:9 plus 4 m, and `arena_rules.lua` clamps the frustum footprint to that rectangle. Checked in Blender and in-engine captures (centre, edges, corners, 16:9 and 21:9); not yet play-tested by hand. |
| Outskirts | arena-02 uses the three bands: a rocky rise north, a dry bank with brush/cacti east, a stream with a fence section west and low ledges south, then grouped outskirts clusters. Review against the latest user-supplied gameplay references; earlier generated concepts are not the approval benchmark. |
| Foliage | Concealment grass is now `grass_*` modules (canopy mass plus tapered blades, skirt only on exposed edges, seamless joins); outskirts use broad-leaf `shrub_a/b/c`. Review blade width and density beside a character. |
| Camera consistency | Resolved: the section 1 benchmark profile is authored in the arena-02 recipe and exported unchanged (perspective, 57°, 25° FOV). |
| Palette | Kit and arena generators read `palette.json` roles; baked darkening respects `minBakedBrightness`. Runtime palette loading/tinting is not present yet. |
| Shading | The current renderer is glTF PBR with shadows, environment lighting, SSAO, bloom, exposure/tone mapping, and FXAA. Two-tone shading with protected macro light/shade regions is a target, not an existing mode. |
| Texture filtering | Texture mipmap support exists; maximum supported anisotropic filtering still needs integration. |
| Look-dev tooling | Add repeatable gameplay captures and a plain-vs-styled switch as implementation work. |

Keep this status short and update it as the work lands. Do not create another art-direction folder, competing style README, or approval report to record the next decision.
