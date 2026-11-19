"""Arena-02 recipe: rebuilds the authored arena in the open arena-02.blend from the canyon kit.

Follows docs/artdirection.md: one benchmark camera (section 1), quiet floor first, three bands
(playable arena, boundary transition, unreachable outskirts) and terrain that covers every
camera footprint at the supported aspects. Run inside Blender (e.g. through MCP):

    import runpy; ns = runpy.run_path(".../tools/blender/arenas/arena_02.py")
    ns["build"]()            # rebuild collections, camera, sun, colliders; validates the scene
    ns["render_checks"]()    # gameplay-camera renders: centre, 4 edges, 4 corners
    ns["export"]()           # Haki Export Arena + the map's environment intensity

Everything is deterministic (fixed seeds); change the recipe and rebuild rather than hand-editing
instances.
"""
import math
from pathlib import Path
import random
import runpy
import sys

import bmesh
import bpy
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[3]
DIRECTORY = ROOT / "Assets/Game/Scenes/arena-02"
SOURCE = DIRECTORY / "Source"
PREVIEWS = ROOT / "build/arena-02-previews"

# ---------------------------------------------------------------- benchmark camera (artdirection 1)
PITCH = 57.0  # degrees below horizontal
FOV_Y = 25.0  # vertical field of view, degrees
DISTANCE = 25.37  # eye to ground focus, metres: ~20 m of ground width at the focus at 16:9
START_FOCUS = (0.0, -4.5)  # startup framing (player spawns a little south of centre)
ASPECTS = (16 / 9, 16 / 10, 21 / 9)

PLAY = 12  # playable area is [-PLAY, PLAY]^2 (24 x 24 cells)
MARGIN = 4.0  # extra terrain beyond the union of camera footprints

GROUND_TEXEL = 0.032  # metres per ground-colour texel

# Map-level lighting, locked per map (artdirection 5). Tuned in-engine together with the rendering
# values in Assets/Game/Project.json: the sun gives the broad light side; the image-based light is
# kept low so it fills shade without bleaching sand, wood and foliage.
SUN_ELEVATION, SUN_AZIMUTH = 52.0, 235.0  # afternoon, from the south-west
SUN_INTENSITY = 2.4
SUN_COLOR = (1.0, 0.94, 0.84)
ENVIRONMENT_INTENSITY = 0.4


def camera_basis():
    p = math.radians(PITCH)
    forward = Vector((0.0, math.cos(p), -math.sin(p)))
    right = Vector((1.0, 0.0, 0.0))
    up = right.cross(forward)
    return forward, right, up


def footprint(focus, aspect):
    """Ground-plane polygon seen by the benchmark camera looking at `focus` (x, y)."""
    forward, right, up = camera_basis()
    eye = Vector((focus[0], focus[1], 0.0)) - forward * DISTANCE
    ty = math.tan(math.radians(FOV_Y) / 2)
    tx = ty * aspect
    points = []
    for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
        ray = forward + right * (sx * tx) + up * (sy * ty)
        if ray.z >= -1e-6:
            raise ValueError("camera sees the horizon")
        t = -eye.z / ray.z
        hit = eye + ray * t
        points.append((hit.x, hit.y))
    return points


def coverage():
    """Ground rectangle covering every footprint for every allowed focus and aspect, plus margin.
    The focus can follow the player anywhere in the playable square."""
    xs, ys = [], []
    for aspect in ASPECTS:
        for fx in (-PLAY, PLAY):
            for fy in (-PLAY, PLAY):
                for x, y in footprint((fx, fy), aspect):
                    xs.append(x)
                    ys.append(y)
    # Snapped to whole metres so the terrain grid lines up with the 1 m placement grid.
    return (math.floor(min(xs) - MARGIN), math.floor(min(ys) - MARGIN), math.ceil(max(xs) + MARGIN), math.ceil(max(ys) + MARGIN))


# ---------------------------------------------------------------- layout (4-fold rotational symmetry)
# North-east quadrant in cell coordinates (cell (i, j) spans [i, i+1] x [j, j+1]); the other three
# quadrants are rotations, so every spawn side gets the same cover. The centre pond takes cell (0, 0).
Q_WALLS = {(6, 7), (7, 7), (8, 7), (8, 6), (8, 5), (1, 9), (1, 10)}
Q_WALLS_LOW = {(3, 4), (4, 4)}
Q_GRASS = {(3, 7), (4, 7), (4, 8), (4, 9), (10, 2), (10, 3), (10, 4), (2, 1), (3, 1)}
Q_CRATES = {(7, 6), (6, 2), (1, 5)}


def rotations(cells):
    out = set()
    for i, j in cells:
        for _ in range(4):
            out.add((i, j))
            i, j = -1 - j, i
    return out


WALLS = rotations(Q_WALLS)
WALLS_LOW = rotations(Q_WALLS_LOW)
GRASS = rotations(Q_GRASS)
CRATES = rotations(Q_CRATES)
POND = {(0, 0), (-1, 0), (-1, -1), (0, -1)}


def walkable_connected():
    blocked = WALLS | WALLS_LOW | CRATES | POND
    free = {(i, j) for i in range(-PLAY, PLAY) for j in range(-PLAY, PLAY)} - blocked
    start = next(iter(free))
    seen, todo = {start}, [start]
    while todo:
        i, j = todo.pop()
        for n in ((i + 1, j), (i - 1, j), (i, j + 1), (i, j - 1)):
            if n in free and n not in seen:
                seen.add(n)
                todo.append(n)
    return len(seen) == len(free)


# ---------------------------------------------------------------- terrain shape
STREAM_X = (-16.6, -13.4)  # west stream channel (outside the playable square)
STREAM_DEPTH = 0.42
WATER_Z = -0.1


def _smooth(t):
    t = min(max(t, 0.0), 1.0)
    return t * t * (3 - 2 * t)


def _noise(x, y, seed=0):
    """Cheap smooth value noise from summed sines (deterministic, no numpy needed)."""
    s = seed * 1.7
    return (math.sin(x * 0.21 + s) * math.cos(y * 0.17 - s * 0.6) * 0.5
            + math.sin(x * 0.083 - y * 0.11 + s * 2.1) * 0.35
            + math.sin((x + y) * 0.37 + s) * 0.15)


def terrain_height(x, y):
    """0 on the playable floor; raised banks east and south, a stream bed west, mounds outside."""
    h = 0.0
    edge = max(abs(x), abs(y))
    # east: a dry bank rising from x = 12.7 to ~0.75 m
    if x > PLAY + 0.7:
        h = max(h, 0.75 * _smooth((x - PLAY - 0.7) / 1.1) + 0.25 * _smooth((x - PLAY - 4) / 6) * (0.5 + 0.5 * _noise(x, y, 3)))
    # south (foreground): a low bank, never above 0.55 m within sight of the play area
    if y < -PLAY - 0.8:
        h = max(h, 0.5 * _smooth((-PLAY - 0.8 - y) / 1.2))
    # north: ground rises gently behind the rocky rise
    if y > PLAY + 1.0:
        h = max(h, 0.6 * _smooth((y - PLAY - 1.0) / 6) * (0.7 + 0.3 * _noise(x, y, 5)))
    # outskirts mounds, fading in well away from the play area
    if edge > PLAY + 5:
        h += 0.35 * _smooth((edge - PLAY - 5) / 8) * (0.5 + 0.5 * _noise(x * 1.3, y * 1.3, 9))
    # west stream bed with sloped banks
    lo, hi = STREAM_X
    if lo - 1.2 < x < hi + 1.2:
        inside = min(x - (lo - 1.2), (hi + 1.2) - x)
        h = min(h, h - (STREAM_DEPTH + h) * _smooth(inside / 1.2))
    return h


# ---------------------------------------------------------------- Blender helpers
def _collection(root, name):
    c = bpy.data.collections.get(name) or bpy.data.collections.new(name)
    if c.name not in root.children:
        root.children.link(c)
    for obj in list(c.objects):
        bpy.data.objects.remove(obj, do_unlink=True)
    return c


def _put(piece, name, location, collection, angle=0.0, scale=1.0):
    obj = bpy.data.objects.new(name, bpy.data.objects[piece].data)
    obj.location = location
    obj.rotation_euler.z = angle
    obj.scale = (scale, scale, scale)
    collection.objects.link(obj)
    return obj


def _body(obj, size, offset, sensor=False):
    h = obj.haki_settings
    h.mode = "BODY"
    h.motion = "static"
    h.shape = "box"
    h.sensor = sensor
    h.layer = "trigger" if sensor else "static"
    h.friction = 0.65
    h.restitution = 0.0
    h.preview = False
    h.size = size
    h.offset = offset
    return obj


def _blocker(name, centre, size, collection):
    obj = bpy.data.objects.new(name, None)
    obj.location = centre
    obj.empty_display_size = 0.25
    collection.objects.link(obj)
    return _body(obj, size, (0, 0, 0))


def _cell_centre(cell):
    return (cell[0] + 0.5, cell[1] + 0.5, 0.0)


# ---------------------------------------------------------------- ground colour (unique baked map)
def _paint_ground(kit, bounds, walls_footprint, grass_cells):
    """Broad, quiet floor colour: low-frequency variation in the floor_sand family, soft contact
    darkening at the base of static walls (>= minBakedBrightness), a faint grass tint under
    concealment patches, shoreline along the stream and darker bank slopes. Flat-lit: no cast
    shadows are painted in."""
    roles = kit["ROLES"]
    rgb = kit["_rgb"]
    x0, y0, x1, y1 = bounds
    w = int(round((x1 - x0) / GROUND_TEXEL))
    h = int(round((y1 - y0) / GROUND_TEXEL))
    w, h = (w + 3) // 4 * 4, (h + 3) // 4 * 4
    sand, edge, light = rgb(roles["floor_sand.base"]), rgb(roles["floor_sand.edge"]), rgb(roles["wall_top.base"])
    grass, shore, shadow = rgb(roles["floor_grass.base"]), rgb(roles["shoreline.base"]), rgb(kit["SHADOW"])
    min_baked = kit["MIN_BAKED"]
    try:
        import numpy as np
    except ImportError as error:  # Blender always ships numpy
        raise RuntimeError("numpy is required to paint the ground") from error
    xs = x0 + (np.arange(w) + 0.5) * (x1 - x0) / w
    ys = y0 + (np.arange(h) + 0.5) * (y1 - y0) / h
    X, Y = np.meshgrid(xs, ys)

    def lerp(a, b, t):
        t = np.clip(t, 0, 1)[..., None]
        return a * (1 - t) + b * t

    base = np.broadcast_to(np.array(sand), (h, w, 3)).astype(np.float32)
    # very broad variation (6-15 m) toward the warmer edge tone and a lighter tone
    n1 = np.sin(X * 0.19 + 1.3) * np.cos(Y * 0.15 - 0.4) * 0.6 + np.sin((X - Y) * 0.071 + 2.0) * 0.4
    n2 = np.sin(X * 0.11 - 0.7) * np.sin(Y * 0.13 + 1.1)
    color = lerp(base, np.array(edge), np.clip(n1, 0, 1) * 0.28)
    color = lerp(color, np.array(light), np.clip(n2, 0, 1) * 0.16)
    # worn paths in the outskirts (east-west track south-east, and one bending north)
    path = np.exp(-((Y + 16.5 + 1.2 * np.sin(X * 0.18)) ** 2) / 1.1) * (X > PLAY + 2)
    path += np.exp(-((X - 4.0 - 2.5 * np.sin(Y * 0.12)) ** 2) / 0.9) * (Y > PLAY + 4.5)
    color = lerp(color, np.array(edge), np.clip(path, 0, 1) * 0.5)
    # bank slopes (east/south/north) lean toward the edge tone; outskirts slightly lighter overall
    # Heights vary over metres: sample a 0.25 m grid and interpolate bilinearly (no stair-steps).
    cx = np.arange(x0, x1 + 0.25, 0.25)
    cy = np.arange(y0, y1 + 0.25, 0.25)
    coarse = np.array([[terrain_height(x, y) for x in cx] for y in cy])
    rows = np.array([np.interp(xs, cx, row) for row in coarse])
    heights = np.array([np.interp(ys, cy, col) for col in rows.T]).T
    gy, gx = np.gradient(heights, (y1 - y0) / h, (x1 - x0) / w)
    slope = np.sqrt(gx * gx + gy * gy)
    color = lerp(color, np.array(edge), np.clip(slope * 1.4, 0, 0.8))
    # stream shoreline: cream band where the bank meets the water
    lo, hi = STREAM_X
    shore_d = np.minimum(np.abs(X - lo), np.abs(X - hi))
    near_water = (heights < 0.05) & (X > lo - 2.2) & (X < hi + 2.2)
    color = lerp(color, np.array(shore), near_water * np.clip(1 - (shore_d - 0.6) / 1.4, 0, 1) * 0.85)
    # faint grass tint under concealment patches (helps the hiding zones read in greyscale)
    tint = np.zeros((h, w), np.float32)
    for i, j in grass_cells:
        dx = np.maximum(np.maximum(i - X, X - (i + 1)), 0)
        dy = np.maximum(np.maximum(j - Y, Y - (j + 1)), 0)
        tint = np.maximum(tint, np.clip(1 - np.sqrt(dx * dx + dy * dy) / 0.12, 0, 1))
    color = lerp(color, np.array(grass), tint * 0.22)
    # soft contact darkening at the base of static blockers, kept above minBakedBrightness
    contact = np.zeros((h, w), np.float32)
    for (cx0, cy0, cx1, cy1) in walls_footprint:
        dx = np.maximum(np.maximum(cx0 - X, X - cx1), 0)
        dy = np.maximum(np.maximum(cy0 - Y, Y - cy1), 0)
        d = np.sqrt(dx * dx + dy * dy)
        contact = np.maximum(contact, np.clip(1 - d / 0.5, 0, 1) ** 2)
    strength = 0.3
    assert 1 - strength >= min_baked
    color = lerp(color, np.array(shadow), contact * strength)
    color = np.clip(color, 0, 1)

    image = bpy.data.images.get("arena02_ground") or bpy.data.images.new("arena02_ground", w, h, alpha=False)
    if tuple(image.size) != (w, h):
        image.scale(w, h)
    rgba = np.concatenate([color, np.ones((h, w, 1), np.float32)], axis=2)
    image.pixels.foreach_set(rgba.astype(np.float32).ravel())
    image.update()
    SOURCE.mkdir(parents=True, exist_ok=True)
    image.filepath_raw = str(SOURCE / "ground_color.png")
    image.file_format = "PNG"
    image.save()
    return image


def _ground_material(image):
    material = bpy.data.materials.get("arena02_ground") or bpy.data.materials.new("arena02_ground")
    material.use_nodes = True
    nodes, links = material.node_tree.nodes, material.node_tree.links
    bsdf = next(n for n in nodes if n.type == "BSDF_PRINCIPLED")
    tex = next((n for n in nodes if n.type == "TEX_IMAGE"), None) or nodes.new("ShaderNodeTexImage")
    tex.image = image
    tex.interpolation = "Linear"
    links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    bsdf.inputs["Roughness"].default_value = 0.9
    return material


def _ground_mesh(bounds, material):
    x0, y0, x1, y1 = bounds
    nx, ny = int(math.ceil(x1 - x0)), int(math.ceil(y1 - y0))
    bm = bmesh.new()
    uv = bm.loops.layers.uv.new("UVMap")
    verts = {}
    for i in range(nx + 1):
        for j in range(ny + 1):
            x = x0 + (x1 - x0) * i / nx
            y = y0 + (y1 - y0) * j / ny
            verts[i, j] = bm.verts.new((x, y, terrain_height(x, y)))
    for i in range(nx):
        for j in range(ny):
            cx, cy = x0 + (x1 - x0) * (i + 0.5) / nx, y0 + (y1 - y0) * (j + 0.5) / ny
            if (math.floor(cx), math.floor(cy)) in POND:
                continue  # the pond pieces bring their own floor
            face = bm.faces.new((verts[i, j], verts[i + 1, j], verts[i + 1, j + 1], verts[i, j + 1]))
            for loop in face.loops:
                co = loop.vert.co
                loop[uv].uv = ((co.x - x0) / (x1 - x0), (co.y - y0) / (y1 - y0))
    mesh = bpy.data.meshes.get("Arena02_Ground") or bpy.data.meshes.new("Arena02_Ground")
    bm.to_mesh(mesh)
    bm.free()
    mesh.materials.clear()
    mesh.materials.append(material)
    for poly in mesh.polygons:
        poly.use_smooth = True
    return mesh


def _water_plane(kit, bounds, palette):
    x0, y0, x1, y1 = bounds
    lo, hi = STREAM_X
    bm = bmesh.new()
    uv = bm.loops.layers.uv.new("UVMap")
    u, v = kit["swatch_uv"]("water")
    # The plane's long edges stay under the bank slope, where the terrain is above WATER_Z.
    face = bm.faces.new([bm.verts.new(c) for c in ((lo - 1.05, y0, WATER_Z), (hi + 1.05, y0, WATER_Z),
                                                   (hi + 1.05, y1, WATER_Z), (lo - 1.05, y1, WATER_Z))])
    for loop in face.loops:
        loop[uv].uv = (u, v)
    mesh = bpy.data.meshes.get("Arena02_Stream") or bpy.data.meshes.new("Arena02_Stream")
    bm.to_mesh(mesh)
    bm.free()
    mesh.materials.clear()
    mesh.materials.append(palette)
    return mesh


# ---------------------------------------------------------------- rock masses for the rocky rise
def _rock_mass(kit, name, seed, rx, ry, height):
    """Tiered wall-palette rock mass: a broad base, a ledge and a narrower rounded crown."""
    rng = random.Random(seed)
    Piece, loft, ngon = kit["Piece"], kit["loft"], kit["ngon"]
    p = Piece(name)
    p.gradient_range = (0.0, height)
    p.sharp_angle = math.radians(42)
    sides = 10
    outline = ngon(sides, 1.0, jitter=0.14, rng=rng, phase=rng.uniform(0, 1))
    profile = [(0.0, 1.0), (0.08, 1.02), (0.5 * height, 0.95), (0.56 * height, 0.8), (0.6 * height, 0.76),
               (0.9 * height, 0.7), (height - 0.06, 0.62), (height, 0.5)]
    shift = (rng.uniform(-0.12, 0.12) * rx, rng.uniform(-0.12, 0.12) * ry)
    rings = []
    for z, k in profile:
        f = 0.0 if z < 0.5 * height else 1.0
        rings.append([(x * rx * k + shift[0] * f, y * ry * k + shift[1] * f, z) for x, y in outline])
    p.add(loft(rings), kit["by_normal"]("wall_top", "wall_side"), cull_bottom=True)
    return p.build()


# ---------------------------------------------------------------- build
def build():
    if Path(bpy.data.filepath).resolve() != (DIRECTORY / "arena-02.blend").resolve():
        raise RuntimeError("Open arena-02.blend before building this arena")
    if not walkable_connected():
        raise RuntimeError("layout splits the walkable area")
    scene = bpy.context.scene
    kit = runpy.run_path(str(ROOT / "tools/blender/kits/canyon_kit.py"))
    extras = runpy.run_path(str(ROOT / "tools/blender/kits/canyon_arena_assets.py"))
    kit["build_all"]()
    extras_objects = extras["build_assets"]()
    library = bpy.data.collections["CanyonKit"]
    for obj in extras_objects:
        for c in list(obj.users_collection):
            c.objects.unlink(obj)
        library.objects.link(obj)
    library.hide_viewport = library.hide_render = True
    palette = kit["palette_material"]()
    root = bpy.data.collections["Arena_Map"]
    # Retired collections from the previous recipe.
    for name in ("Arena_Bounds", "Arena_Spawns", "Arena_Dressing"):
        old = bpy.data.collections.get(name)
        if old:
            for obj in list(old.objects):
                bpy.data.objects.remove(obj, do_unlink=True)
            bpy.data.collections.remove(old)
    floor = _collection(root, "Arena_Floor")
    walls = _collection(root, "Arena_Walls")
    water = _collection(root, "Arena_Water")
    vegetation = _collection(root, "Arena_Vegetation")
    props = _collection(root, "Arena_Props")
    fences = _collection(root, "Arena_Fences")
    physics = _collection(root, "Arena_Physics")
    boundary = _collection(root, "Arena_Boundary")
    outskirts = _collection(root, "Arena_Outskirts")
    rng = random.Random(202)

    # -- playable band: cover walls (continuous modules), concealment grass, crates, pond
    wall_boxes = []
    for cells, suffix, height in ((WALLS, "", 1.0), (WALLS_LOW, "_low", 0.5)):
        for cell in sorted(cells):
            kind, turns = kit["wall_piece_for"](cells, cell)
            obj = _put("wall_" + kind + suffix, f"Wall{suffix}_{cell[0]}_{cell[1]}", _cell_centre(cell), walls, turns * math.pi / 2)
            _body(obj, (1, 1, height), (0, 0, height / 2))
            wall_boxes.append((cell[0], cell[1], cell[0] + 1, cell[1] + 1))
    for cell in sorted(GRASS):
        kind, turns = kit["wall_piece_for"](GRASS, cell)
        obj = _put("grass_" + kind, f"Grass_{cell[0]}_{cell[1]}", _cell_centre(cell), vegetation, turns * math.pi / 2)
        _body(obj, (1, 1, 0.9), (0, 0, 0.45), sensor=True)
    for cell in sorted(CRATES):
        x, y, _ = _cell_centre(cell)
        obj = _put("crate", f"Crate_{cell[0]}_{cell[1]}", (x, y, 0), props, rng.uniform(-0.12, 0.12))
        _body(obj, (0.9, 0.9, 0.9), (0, 0, 0.45))
    for k, cell in enumerate(((0, 0), (-1, 0), (-1, -1), (0, -1))):
        _put("water_corner_outer", f"Pond_{k}", _cell_centre(cell), water, k * math.pi / 2)
    _blocker("PondBlocker", (0, 0, 0.4), (2, 2, 0.8), physics)

    # -- terrain covering every camera footprint (+ margin), with a baked unique colour map
    bounds = coverage()
    x0, y0, x1, y1 = bounds
    image = _paint_ground(kit, bounds, wall_boxes, GRASS)
    ground = bpy.data.objects.new("Arena02_Ground", _ground_mesh(bounds, _ground_material(image)))
    floor.objects.link(ground)
    _body(ground, (2 * PLAY + 2, 2 * PLAY + 2, 0.4), (0, 0, -0.2))
    stream = bpy.data.objects.new("Arena02_Stream", _water_plane(kit, bounds, palette))
    water.objects.link(stream)

    # -- boundary transition: each side gives a visible reason movement stops
    edge = PLAY + 0.25
    _blocker("Bound_North", (0, edge, 1.0), (2 * PLAY + 1, 0.5, 2.0), physics)
    _blocker("Bound_South", (0, -edge, 1.0), (2 * PLAY + 1, 0.5, 2.0), physics)
    _blocker("Bound_East", (edge, 0, 1.0), (0.5, 2 * PLAY + 1, 2.0), physics)
    _blocker("Bound_West", (-edge, 0, 1.0), (0.5, 2 * PLAY + 1, 2.0), physics)

    # North: a rocky rise of grouped masses (large, medium, small), varied setback and height.
    variants = []
    for k, (rx, ry, height) in enumerate(((2.6, 1.7, 3.4), (1.9, 1.4, 2.6), (1.5, 1.1, 1.9), (2.2, 1.5, 3.0), (1.2, 0.9, 1.4), (3.0, 2.0, 2.2))):
        name = f"rock_mass_{k}"
        _rock_mass(kit, name, 40 + k, rx, ry, height)  # lands in the hidden CanyonKit library
        variants.append(name)
    north = [(-10.5, 14.3, 0), (-6.2, 15.6, 3), (-2.3, 14.1, 1), (1.8, 15.9, 5), (6.0, 14.4, 2), (9.6, 15.2, 3),
             (13.6, 14.6, 0), (-14.2, 15.0, 4), (-8.4, 18.6, 5), (3.8, 19.4, 0), (11.8, 19.0, 1), (-1.6, 18.0, 4)]
    for k, (x, y, variant) in enumerate(north):
        _put(variants[variant], f"NorthRock_{k:02}", (x, y, terrain_height(x, y) - 0.05), boundary, rng.uniform(0, math.tau), rng.uniform(0.9, 1.15))
    for k in range(10):
        x, y = rng.uniform(-13, 13), rng.uniform(12.6, 13.3)
        _put("rock_boulder", f"NorthBoulder_{k:02}", (x, y, 0), boundary, rng.uniform(0, math.tau), rng.uniform(0.8, 1.4))
    # East: a lower dry bank; brush, cacti and boulders gather in groups along its crest.
    east_groups = [(-10.2, 3), (-5.6, 4), (-1.0, 2), (3.4, 4), (7.9, 3), (11.0, 2)]
    for g, (gy, count) in enumerate(east_groups):
        for k in range(count):
            x = PLAY + rng.uniform(1.9, 3.4)
            y = gy + rng.uniform(-1.3, 1.3)
            piece = ("shrub_b", "shrub_a", "cactus_barrel", "rock_boulder", "shrub_c", "cactus_tall")[(g * 2 + k) % 6]
            scale = rng.uniform(1.1, 1.6) if piece == "rock_boulder" else rng.uniform(0.95, 1.25)
            _put(piece, f"EastBank_{g}_{k}", (x, y, terrain_height(x, y) - 0.03), boundary, rng.uniform(0, math.tau), scale)
    for k in range(4):
        y = rng.uniform(-11, 11)
        x = PLAY + rng.uniform(1.0, 1.4)
        _put("rock_boulder", f"EastToe_{k:02}", (x, y, terrain_height(x, y) - 0.05), boundary, rng.uniform(0, math.tau), rng.uniform(0.8, 1.1))
    # West: a water edge; a fence section marks the drop toward the stream.
    for k, y in enumerate(range(-6, 7)):
        point = (-PLAY - 0.5, y + 0.0, terrain_height(-PLAY - 0.5, y))
        _put("fence_post", f"WestFencePost_{k:02}", point, fences)
        if y < 6:
            _put("fence_rail", f"WestFenceRail_{k:02}", point, fences, math.pi / 2)
    for k in range(6):
        y = rng.choice((-1, 1)) * rng.uniform(7.5, 12)
        x = -PLAY - rng.uniform(0.5, 1.1)
        _put("rock_boulder", f"WestBoulder_{k:02}", (x, y, terrain_height(x, y) - 0.03), boundary, rng.uniform(0, math.tau), rng.uniform(0.7, 1.1))
    # South (foreground): low rock ledges (<= 0.5 m) and low shrubs in groups, so nothing can hide
    # the player at the bottom of the screen.
    for k, (x, width) in enumerate(((-9.5, 2.2), (-3.0, 1.6), (2.4, 2.6), (8.6, 1.8))):
        name = f"south_ledge_{k}"
        _rock_mass(kit, name, 70 + k, width, 0.7, 0.45)
        y = -PLAY - 1.9
        _put(name, f"SouthLedge_{k}", (x, y, terrain_height(x, y) - 0.08), boundary, rng.uniform(-0.15, 0.15))
    for k in range(9):
        x = -11.5 + k * 2.8 + rng.uniform(-0.7, 0.7)
        y = -PLAY - rng.uniform(1.2, 1.6)
        _put(("shrub_c", "grass_tuft", "rock_pebbles")[k % 3], f"SouthBank_{k:02}", (x, y, terrain_height(x, y)), boundary, rng.uniform(0, math.tau), rng.uniform(0.75, 0.95))

    # -- outskirts: grouped boulders, shrubs and cacti in restrained clusters (not uniform)
    clusters = [(-24, 10, 6), (-22, -14, 5), (20, -17, 6), (24, 6, 5), (-6, 21, 5), (10, -19, 4), (19, 18, 6),
                (-19, 2, 4), (27, -6, 5), (-27, -4, 4), (-12, -19, 5), (2, -19.5, 3), (27, 15, 4), (-26, 18, 5),
                (16, 21, 3), (21, -8, 4)]
    for c, (cx, cy, count) in enumerate(clusters):
        anchor = None
        for k in range(count):
            x, y = cx + rng.uniform(-2.4, 2.4), cy + rng.uniform(-1.8, 1.8)
            if not (x0 < x < x1 and y0 < y < y1) or (STREAM_X[0] - 1.5 < x < STREAM_X[1] + 1.5):
                continue
            if k == 0 and c % 3 == 0:
                piece = variants[(c // 3) % len(variants)]  # a few large masses anchor some clusters
                scale = rng.uniform(0.55, 0.8)
            else:
                piece = ("rock_boulder", "shrub_b", "shrub_a", "cactus_barrel", "rock_pebbles", "cactus_tall", "shrub_c")[(c + k) % 7]
                scale = rng.uniform(1.4, 2.6) if piece == "rock_boulder" else rng.uniform(0.9, 1.35)
            _put(piece, f"Outskirt_{c}_{k}", (x, y, terrain_height(x, y) - 0.04), outskirts, rng.uniform(0, math.tau), scale)

    # -- one camera (benchmark profile) and one afternoon sun
    for obj in list(scene.objects):
        if obj.type == "CAMERA" and obj.name != "Arena02_Camera":
            bpy.data.objects.remove(obj, do_unlink=True)
        elif obj.type == "LIGHT" and obj.name != "Arena02_Sun":
            bpy.data.objects.remove(obj, do_unlink=True)
    camera = bpy.data.objects["Arena02_Camera"]
    camera.data.type = "PERSP"
    camera.data.sensor_fit = "VERTICAL"
    camera.data.angle_y = math.radians(FOV_Y)
    camera.data.clip_start, camera.data.clip_end = 0.5, 120.0
    forward, _, _ = camera_basis()
    focus = Vector((*START_FOCUS, 0.0))
    camera.location = focus - forward * DISTANCE
    camera.rotation_euler = forward.to_track_quat("-Z", "Y").to_euler()
    scene.camera = camera
    scene.render.resolution_x, scene.render.resolution_y, scene.render.resolution_percentage = 1600, 900, 100
    sun = bpy.data.objects["Arena02_Sun"]
    elevation, azimuth = math.radians(SUN_ELEVATION), math.radians(SUN_AZIMUTH)
    to_sun = Vector((math.cos(elevation) * math.sin(azimuth), math.cos(elevation) * math.cos(azimuth), math.sin(elevation)))
    sun.rotation_euler = (-to_sun).to_track_quat("-Z", "Y").to_euler()
    sun.data.energy = SUN_INTENSITY
    sun.data.color = SUN_COLOR

    # -- player controller (no visible mesh or spawn platform)
    gameplay = bpy.data.collections["Arena_Gameplay"]
    for obj in list(gameplay.objects):
        bpy.data.objects.remove(obj, do_unlink=True)
    scene.cursor.location = (START_FOCUS[0], START_FOCUS[1] + 0.5, 0.03)
    bpy.ops.haki.add_marker(character=True)
    player = bpy.context.object
    player.name = "Player"
    for c in list(player.users_collection):
        c.objects.unlink(player)
    gameplay.objects.link(player)
    h = player.haki_settings
    h.radius, h.height, h.step, h.speed = 0.35, 1.8, 0.3, 5
    h.local_player = True
    h.preview = True
    h.script_enabled = True
    h.script_path = "player.lua"

    project = scene.haki_project
    project.root = str(ROOT)
    project.scene_id = project.pack = "arena-02"
    project.scene_directory = str(DIRECTORY)
    project.python = str(ROOT / ".venv/bin/python")
    project.engine = str(ROOT / "build/Engine")
    project.scene_script = "arena_rules.lua"
    project.default_skybox = True
    project.skybox_asset = ""
    project.import_after_export = True
    project.set_entry = True
    bpy.ops.haki.repair_ids()
    editor = next(m for n, m in sys.modules.items() if n.endswith("haki_editor.editor"))
    errors = editor.validate_scene(bpy.context)
    if errors:
        raise RuntimeError("\n".join(errors))
    counts = {c.name: len(c.objects) for c in root.children}
    print("arena-02 built:", counts, "terrain", tuple(round(v, 1) for v in bounds), "ground map", image.size[:])
    return bounds


def export():
    """Haki Export Arena, then apply the map's environment intensity. The add-on currently always
    writes 1.0 for the engine default skybox; this keeps the tuned value until it has a setting."""
    import json

    bpy.ops.wm.save_mainfile()
    result = bpy.ops.haki.export()
    if result != {"FINISHED"}:
        raise RuntimeError(f"Haki export failed: {result}")
    path = DIRECTORY / "arena-02.scene.json"
    data = json.loads(path.read_text(encoding="utf-8"))
    if data.get("environment"):
        data["environment"]["intensity"] = ENVIRONMENT_INTENSITY
        path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    return path


def render_checks(size=(1280, 720)):
    """Gameplay-camera renders at the startup view, centre, 4 edges and 4 corners (16:9 and 21:9)."""
    scene = bpy.context.scene
    camera = scene.camera
    forward, _, _ = camera_basis()
    saved = (camera.location.copy(), scene.render.resolution_x, scene.render.resolution_y, scene.render.filepath)
    PREVIEWS.mkdir(parents=True, exist_ok=True)
    spots = {"start": START_FOCUS, "centre": (0, 0), "north": (0, PLAY), "south": (0, -PLAY), "east": (PLAY, 0),
             "west": (-PLAY, 0), "ne": (PLAY, PLAY), "nw": (-PLAY, PLAY), "se": (PLAY, -PLAY), "sw": (-PLAY, -PLAY)}
    written = []
    try:
        for aspect_name, aspect in (("16x9", 16 / 9), ("21x9", 21 / 9)):
            scene.render.resolution_x = int(size[1] * aspect)
            scene.render.resolution_y = size[1]
            for name, (fx, fy) in spots.items():
                if aspect_name == "21x9" and name not in ("start", "ne", "sw", "east", "west"):
                    continue
                camera.location = Vector((fx, fy, 0.0)) - forward * DISTANCE
                scene.render.filepath = str(PREVIEWS / f"{aspect_name}_{name}.png")
                bpy.ops.render.render(write_still=True)
                written.append(scene.render.filepath)
    finally:
        camera.location, scene.render.resolution_x, scene.render.resolution_y, scene.render.filepath = saved
    return written
