"""Arena-02 recipe: rebuilds the authored arena in the open arena-02.blend from the canyon kit v2.

Visual targets:
  * docs/references/kit-v2/concept-gameplay-primary.jpg
  * the concept's arena composition (central clearing, Z wall, L pockets with caches and
    barrels, stepped water inlets, plus pool, big stepped golden grass, fenced mesa plateaus).

Rules: docs/artdirection.md, which sets
  * one benchmark camera (section 1);
  * three bands: playable arena, boundary transition, unreachable outskirts;
  * terrain covering every camera footprint at the supported aspects.

Run inside Blender (e.g. through MCP):
    import runpy; ns = runpy.run_path(".../tools/blender/arenas/arena_02.py")
    ns["build"]()            # rebuild collections, camera, sun, colliders; validates the scene
    ns["render_checks"]()    # gameplay-camera renders: start, centre, 4 edges, 4 corners
    ns["export"]()           # Haki Export Arena + the map's environment intensity

Everything is deterministic (fixed seeds). Change the recipe and rebuild rather than
hand-editing instances.
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

PLAY = 12  # playable area is [-PLAY, PLAY]^2 (cells -12..11 on both axes)
MARGIN = 4.0  # extra terrain beyond the union of camera footprints
GROUND_TEXEL = 0.032  # metres per ground-colour texel
PLATEAU = 0.6  # raised mesa ground in the outskirts

# Map-level lighting, locked per map (artdirection 5): a warm afternoon sun from the west-north-west,
# so shadows fall right and slightly down the screen as in the concept. Tuned in-engine with
# Assets/Game/Project.json rendering values.
SUN_ELEVATION, SUN_AZIMUTH = 50.0, 292.0
SUN_INTENSITY = 2.6
SUN_COLOR = (1.0, 0.95, 0.86)
ENVIRONMENT_INTENSITY = 0.45


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
    """Ground rectangle covering every footprint for every allowed focus and aspect, plus margin,
    snapped to whole metres so the terrain grid lines up with the 1 m placement grid."""
    xs, ys = [], []
    for aspect in ASPECTS:
        for fx in (-PLAY, PLAY):
            for fy in (-PLAY, PLAY):
                for x, y in footprint((fx, fy), aspect):
                    xs.append(x)
                    ys.append(y)
    return (math.floor(min(xs) - MARGIN), math.floor(min(ys) - MARGIN), math.ceil(max(xs) + MARGIN), math.ceil(max(ys) + MARGIN))


# ---------------------------------------------------------------- layout
# Cell (i, j) covers [i, i + 1] x [j, j + 1] in world space and kit modules are placed at its
# centre (i + 0.5, j + 0.5); playable cells are i, j in -12..11, covering exactly [-12, 12]^2.
#
# The north half (j >= 0) is authored; the south half is its 180 degree rotation
# (i, j) -> (-1 - i, -1 - j), so both spawn sides get the same routes and cover.
def _rows(*rows):
    out = {}
    for j, row in zip(range(11, 11 - len(rows), -1), rows):
        assert len(row) == 24, (j, len(row))
        for col, ch in enumerate(row):
            if ch != ".":
                out[(col - 12, j)] = ch
    return out


# columns: i = -12 ........................... 11
NORTH = _rows(
    "~~~~~~~.....gggggg......",  # j = 11
    "~~~~~~......gggggggg....",  # j = 10
    "~~~~.........gggg...###.",  # j = 9
    "~~~.c...............hb#.",  # j = 8
    "~~.........###..r.....#.",  # j = 7
    "ggg......###..x.##...c..",  # j = 6
    "gggg...r................",  # j = 5
    ".ggggg.......h.r.#####..",  # j = 4
    "..ggggg..............#..",  # j = 3
    "...ggg....r..........ggg",  # j = 2  (plus pool cells are added below)
    "o...................gggg",  # j = 1
    "...........r........gggg",  # j = 0
)
PLUS_POOL = {(7, 1), (6, 1), (8, 1), (7, 2), (7, 0)}


def rotate(cell):
    return (-1 - cell[0], -1 - cell[1])


LAYOUT = dict(NORTH)
for _cell in PLUS_POOL:
    LAYOUT[_cell] = "~"
for _cell, _ch in list(LAYOUT.items()):
    LAYOUT[rotate(_cell)] = _ch


def cells(kind):
    return {c for c, ch in LAYOUT.items() if ch == kind}


WALLS = cells("#")
GRASS = cells("g")
WATER_PLAY = cells("~")
CRATES = cells("c")
BARRELS = cells("b")
CACHES = cells("h")
CACTI = cells("x")
ROCK_TILES = cells("r")
BONES = cells("o")
BLOCKING = WALLS | WATER_PLAY | CRATES | BARRELS | CACHES | CACTI


def components(cell_set):
    cell_set = set(cell_set)
    out = []
    while cell_set:
        start = cell_set.pop()
        group, todo = {start}, [start]
        while todo:
            i, j = todo.pop()
            for n in ((i + 1, j), (i - 1, j), (i, j + 1), (i, j - 1)):
                if n in cell_set:
                    cell_set.remove(n)
                    group.add(n)
                    todo.append(n)
        out.append(group)
    return sorted(out, key=lambda g: min(g))


def walkable_connected():
    free = {(i, j) for i in range(-PLAY, PLAY) for j in range(-PLAY, PLAY)} - BLOCKING
    start = next(iter(free))
    seen, todo = {start}, [start]
    while todo:
        i, j = todo.pop()
        for n in ((i + 1, j), (i - 1, j), (i, j + 1), (i, j - 1)):
            if n in free and n not in seen:
                seen.add(n)
                todo.append(n)
    return len(seen) == len(free)


# ---------------------------------------------------------------- outskirts layout (cells outside the play square)
def outskirts(bounds):
    """Water and plateau cells beyond the playable square. Each side has its own reason movement
    stops: a lake fed by the north-west inlet, a fenced mesa rise to the north and east, a stream
    along the west, and a low south-east lake with a fenced mesa beyond (foreground kept low)."""
    x0, y0, x1, y1 = bounds
    i_range = range(math.floor(x0), math.ceil(x1))
    j_range = range(math.floor(y0), math.ceil(y1))
    play = lambda i, j: -PLAY <= i < PLAY and -PLAY <= j < PLAY
    water, plateau = set(), set()
    for i in i_range:
        for j in j_range:
            if play(i, j):
                continue
            # west stream, widening into the north-west lake
            if -18 <= i <= -13 and j >= -20:
                water.add((i, j))
            if j >= 12 and i <= -6 + (1 if j == 12 else 0) - max(0, j - 14) and i >= -18:
                water.add((i, j))
            # south-east lake below the rotated inlet
            if j <= -13 and 5 + max(0, -14 - j) <= i <= 20 and j >= -15:
                water.add((i, j))
    for i in i_range:
        for j in j_range:
            if play(i, j) or (i, j) in water:
                continue
            # north mesa: stepped cliff line, never touching the playable edge where the lake is
            edge_n = 13 + (1 if i % 7 in (2, 3, 4) else 0) + (1 if 4 <= i <= 6 else 0)
            if j >= edge_n and i >= -5:
                plateau.add((i, j))
            # north-west mesa beyond the lake, with the palms
            if i <= -19 and j >= -6:
                plateau.add((i, j))
            if j >= 15 and i <= -6:
                plateau.add((i, j))
            # east mesa: a stepped rise, broken by one sandy gap
            edge_e = 13 + (1 if j % 5 in (0, 1) else 0)
            if i >= edge_e and not (-3 <= j <= 1) and j > -13:
                plateau.add((i, j))
            # south-east mesa beyond the lake (foreground: 0.6 m high, three rows below the play edge)
            if j <= -16 and i >= 3:
                plateau.add((i, j))
    return water, plateau


# ---------------------------------------------------------------- Blender helpers
def _collection(root, name):
    c = bpy.data.collections.get(name) or bpy.data.collections.new(name)
    if c.name not in root.children:
        root.children.link(c)
    for obj in list(c.objects):
        bpy.data.objects.remove(obj, do_unlink=True)
    return c


def _world(cell, z=0.0):
    return (cell[0] + 0.5, cell[1] + 0.5, z)


def _put(piece, name, location, collection, quarter_turns=0, scale=1.0):
    obj = bpy.data.objects.new(name, bpy.data.objects[piece].data)
    obj.location = location
    obj.rotation_euler.z = quarter_turns * math.pi / 2
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


# ---------------------------------------------------------------- ground colour (unique baked map)
def _paint_ground(tex, bounds, contact_boxes, cliff_cells, grass_cells, plateau=()):
    """Peach sand with broad, quiet tonal variation (artdirection 4): large soft lighter and
    warmer areas, sparse small darker grains, soft contact darkening at the base of walls and
    mesa cliffs (>= minBakedBrightness), a faint warm shade under grass. Flat-lit."""
    import numpy as np

    x0, y0, x1, y1 = bounds
    w = int(round((x1 - x0) / GROUND_TEXEL))
    h = int(round((y1 - y0) / GROUND_TEXEL))
    w, h = (w + 3) // 4 * 4, (h + 3) // 4 * 4
    xs = x0 + (np.arange(w) + 0.5) * (x1 - x0) / w
    ys = y0 + (np.arange(h) + 0.5) * (y1 - y0) / h
    X, Y = np.meshgrid(xs, ys)
    base = tex["rgb"]("floor_sand.base")
    light = tex["lighten"](base, 0.07)
    warm = tex["mix"]("floor_sand.base", "floor_sand.edge", 0.35)
    shade = tex["mix"]("floor_sand.base", "floor_sand.shade", 0.3)
    grain = tex["mix"]("floor_sand.base", "floor_sand.edge", 0.7)
    smooth = tex["smoothstep"]
    colour = np.broadcast_to(base, (h, w, 3)).astype(np.float32).copy()
    # Broad variation (5-15 m): lighter and warmer areas (the gameplay references' "subtle larger
    # tonal change across the map").
    large = tex["periodic_noise"](h, w, 7.0 / GROUND_TEXEL / 2, 7.0 / GROUND_TEXEL / 2, 101)
    medium = tex["periodic_noise"](h, w, 1.6 / GROUND_TEXEL / 2, 1.6 / GROUND_TEXEL / 2, 102)
    colour += (light - colour) * (0.8 * smooth(0.1, 0.7, large))[..., None]
    colour += (warm - colour) * (0.7 * smooth(0.15, 0.75, -large))[..., None]
    colour += (shade - colour) * (0.35 * smooth(0.35, 0.9, medium))[..., None]
    # Sparse small darker grains and pebble marks (quieter than any prop).
    rng = np.random.default_rng(7)
    count = int((x1 - x0) * (y1 - y0) * 0.9)
    gx = rng.uniform(x0, x1, count)
    gy = rng.uniform(y0, y1, count)
    gr = rng.uniform(0.025, 0.06, count)
    for cx, cy, r in zip(gx, gy, gr):
        i0, i1 = int((cx - r - x0) / GROUND_TEXEL), int((cx + r - x0) / GROUND_TEXEL) + 2
        j0, j1 = int((cy - r - y0) / GROUND_TEXEL), int((cy + r - y0) / GROUND_TEXEL) + 2
        i0, j0, i1, j1 = max(i0, 0), max(j0, 0), min(i1, w), min(j1, h)
        if i0 >= i1 or j0 >= j1:
            continue
        d = np.hypot(X[j0:j1, i0:i1] - cx, (Y[j0:j1, i0:i1] - cy) * 1.2)
        k = (1 - smooth(r * 0.55, r, d)) * 0.45
        colour[j0:j1, i0:i1] += (grain - colour[j0:j1, i0:i1]) * k[..., None]
    # Soft contact darkening at the base of static blockers and mesa cliffs.
    contact = np.zeros((h, w), np.float32)

    def add_box(bx0, by0, bx1, by1, reach):
        nonlocal contact
        i0, i1 = max(int((bx0 - reach - x0) / GROUND_TEXEL), 0), min(int((bx1 + reach - x0) / GROUND_TEXEL) + 2, w)
        j0, j1 = max(int((by0 - reach - y0) / GROUND_TEXEL), 0), min(int((by1 + reach - y0) / GROUND_TEXEL) + 2, h)
        if i0 >= i1 or j0 >= j1:
            return
        sx, sy = X[j0:j1, i0:i1], Y[j0:j1, i0:i1]
        dx = np.maximum(np.maximum(bx0 - sx, sx - bx1), 0)
        dy = np.maximum(np.maximum(by0 - sy, sy - by1), 0)
        d = np.sqrt(dx * dx + dy * dy)
        contact[j0:j1, i0:i1] = np.maximum(contact[j0:j1, i0:i1], np.clip(1 - d / reach, 0, 1) ** 2)

    for box in contact_boxes:
        add_box(*box, 0.45)
    for i, j in cliff_cells:
        add_box(i, j, i + 1, j + 1, 0.5)
    # under the grass: a faint warm shade so gaps between blades read as depth, not bare sand
    for i, j in grass_cells:
        add_box(i + 0.08, j + 0.08, i + 0.92, j + 0.92, 0.12)
    # Mesa tops share this map: keep the cliff-foot darkening off them.
    for i, j in plateau:
        i0, i1 = max(int((i - x0) / GROUND_TEXEL), 0), min(int((i + 1 - x0) / GROUND_TEXEL) + 1, w)
        j0, j1 = max(int((j - y0) / GROUND_TEXEL), 0), min(int((j + 1 - y0) / GROUND_TEXEL) + 1, h)
        contact[j0:j1, i0:i1] = 0
    colour = tex["darken"](colour, 0.32 * contact)
    colour = np.clip(colour, 0, 1).astype(np.float32)

    image = bpy.data.images.get("arena02_ground") or bpy.data.images.new("arena02_ground", w, h, alpha=False)
    if tuple(image.size) != (w, h):
        image.scale(w, h)
    rgba = np.concatenate([colour, np.ones((h, w, 1), np.float32)], axis=2)
    image.pixels.foreach_set(rgba.ravel())
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
    bsdf.inputs["Roughness"].default_value = 0.95
    return material


def _ground_uv(bounds):
    x0, y0, x1, y1 = bounds
    return lambda co: ((co.x - x0) / (x1 - x0), (co.y - y0) / (y1 - y0))


def _ground_mesh(bounds, material, holes):
    """Flat sand at z = 0 over the coverage rectangle, one quad per cell, with holes where water
    modules and mesa masses bring their own surfaces."""
    x0, y0, x1, y1 = bounds
    uv_of = _ground_uv(bounds)
    bm = bmesh.new()
    uv = bm.loops.layers.uv.new("UVMap")
    verts = {}

    def vert(x, y):
        key = (x, y)
        if key not in verts:
            verts[key] = bm.verts.new((x, y, 0.0))
        return verts[key]

    for i in range(int(x0), int(x1)):
        for j in range(int(y0), int(y1)):
            if (i, j) in holes:
                continue
            face = bm.faces.new((vert(i, j), vert(i + 1, j), vert(i + 1, j + 1), vert(i, j + 1)))
            for loop in face.loops:
                loop[uv].uv = uv_of(loop.vert.co)
    mesh = bpy.data.meshes.get("Arena02_Ground") or bpy.data.meshes.new("Arena02_Ground")
    bm.to_mesh(mesh)
    bm.free()
    mesh.materials.clear()
    mesh.materials.append(material)
    for poly in mesh.polygons:
        poly.use_smooth = True
    return mesh


def _ground_remap(obj, ground_material, bounds, kit_sand):
    """Give a module copy's land faces the arena ground map (world-projected UVs), so water land
    fills and mesa tops continue the floor seamlessly."""
    uv_of = _ground_uv(bounds)
    if kit_sand not in [m.name for m in obj.data.materials]:
        return obj
    mesh = obj.data.copy()
    obj.data = mesh
    names = [m.name for m in mesh.materials]
    sand_index = names.index(kit_sand)
    mesh.materials[sand_index] = ground_material
    world = obj.matrix_world
    layer = mesh.uv_layers["UVMap"].data
    for poly in mesh.polygons:
        if poly.material_index == sand_index:
            for li in poly.loop_indices:
                co = world @ mesh.vertices[mesh.loops[li].vertex_index].co
                layer[li].uv = uv_of(co)
    return obj


# ---------------------------------------------------------------- static batching
CHUNK = 8  # metres; merged static dressing keeps per-chunk frustum culling


def _merge(collection, prefix):
    """Join the static meshes of a collection into one object per 8 m chunk. Fewer objects means
    far fewer draw calls in the forward and shadow passes. Custom (foliage) normals and materials
    survive the join."""
    groups = {}
    for obj in collection.objects:
        if obj.type != "MESH":
            continue
        key = (math.floor(obj.location.x / CHUNK), math.floor(obj.location.y / CHUNK))
        groups.setdefault(key, []).append(obj)
    merged = []
    for (cx, cy), objs in sorted(groups.items()):
        target = objs[0]
        target.data = target.data.copy()
        target.name = f"{prefix}_{cx}_{cy}"
        target.data.name = target.name
        if len(objs) > 1:
            with bpy.context.temp_override(active_object=target, object=target,
                                           selected_objects=objs, selected_editable_objects=objs):
                bpy.ops.object.join()
        merged.append(target)
    return merged


# ---------------------------------------------------------------- build
def build():
    if Path(bpy.data.filepath).resolve() != (DIRECTORY / "arena-02.blend").resolve():
        raise RuntimeError("Open arena-02.blend before building this arena")
    if not walkable_connected():
        raise RuntimeError("layout splits the walkable area")
    scene = bpy.context.scene
    kit = runpy.run_path(str(ROOT / "tools/blender/kits/canyon_kit.py"))
    tex = kit["TEX"]
    # Kit library (hidden): every module, plus water variants created on demand.
    old_library = bpy.data.collections.get("CanyonKit")
    if old_library:
        for obj in list(old_library.objects):
            bpy.data.objects.remove(obj, do_unlink=True)
    kit["build_all"]()
    library = kit["kit_collection"]()
    library.hide_viewport = library.hide_render = True
    root = bpy.data.collections["Arena_Map"]
    floor = _collection(root, "Arena_Floor")
    walls = _collection(root, "Arena_Walls")
    water = _collection(root, "Arena_Water")
    vegetation = _collection(root, "Arena_Vegetation")
    props = _collection(root, "Arena_Props")
    fences = _collection(root, "Arena_Fences")
    physics = _collection(root, "Arena_Physics")
    boundary = _collection(root, "Arena_Boundary")
    outskirts_c = _collection(root, "Arena_Outskirts")
    rng = random.Random(202)
    bounds = coverage()
    x0, y0, x1, y1 = bounds

    # -- playable band ------------------------------------------------------------------------
    # Cover: each connected wall is one continuous shell from the kit's clay recipe (same profile
    # as the cover modules), so strata and texture run unbroken round corners.
    contact = []
    for k, group in enumerate(components(WALLS)):
        local = [(i - min(c[0] for c in group), j - min(c[1] for c in group)) for i, j in group]
        anchor = (min(c[0] for c in group), min(c[1] for c in group))
        obj = kit["cover_piece"](f"arena_wall_{k:02}", local, seed=50 + k)
        for c in list(obj.users_collection):
            c.objects.unlink(obj)
        walls.objects.link(obj)
        obj.location = _world(anchor)
        for i, j in group:
            _blocker(f"WallBody_{i}_{j}", (i + 0.5, j + 0.5, 0.5), (1, 1, 1), physics)
            contact.append((i, j, i + 1, j + 1))
    # Concealment grass: kit modules chosen per cell; each cell is a nonblocking sensor.
    for cell in sorted(GRASS):
        _put(kit["grass_piece"](GRASS, cell), f"Grass_{cell[0]}_{cell[1]}", _world(cell), vegetation)
        sensor = bpy.data.objects.new(f"GrassZone_{cell[0]}_{cell[1]}", None)
        sensor.location = (cell[0] + 0.5, cell[1] + 0.5, 0.45)
        sensor.empty_display_size = 0.2
        physics.objects.link(sensor)
        _body(sensor, (1, 1, 0.9), (0, 0, 0), sensor=True)
    # Props: crates, barrels, Haki caches, cactus (all static here; breakables come later).
    for cells_, piece, size in ((CRATES, "crate", 0.9), (BARRELS, "barrel", 0.65), (CACHES, "haki_cache", 0.9), (CACTI, "cactus_flower", 0.55)):
        for cell in sorted(cells_):
            obj = _put(piece, f"{piece}_{cell[0]}_{cell[1]}", _world(cell), props, 0 if cell[1] >= 0 else 2)
            _body(obj, (size, size, 0.9), (0, 0, 0.45))
            contact.append((cell[0] + 0.5 - size / 2, cell[1] + 0.5 - size / 2, cell[0] + 0.5 + size / 2, cell[1] + 0.5 + size / 2))
    for cell in sorted(ROCK_TILES):
        _put("rock_tiles", f"RockTiles_{cell[0]}_{cell[1]}", _world(cell), props, rng.randrange(4))
    for cell in sorted(BONES):
        _put("bone_pile", f"Bones_{cell[0]}_{cell[1]}", _world(cell), props, 0 if cell[1] >= 0 else 2)

    # -- water: playable pools/inlets plus outskirts lake and stream, all from water modules ----
    water_out, plateau = outskirts(bounds)
    all_water = WATER_PLAY | water_out
    ground_material = _ground_material(bpy.data.images.get("arena02_ground") or bpy.data.images.new("arena02_ground", 4, 4))
    for cell in sorted(all_water):
        if not (x0 <= cell[0] < x1 and y0 <= cell[1] < y1):
            continue
        name = kit["water_piece"](all_water, cell)
        obj = _put(name, f"Water_{cell[0]}_{cell[1]}", _world(cell), water)
    for cell in sorted(WATER_PLAY):
        _blocker(f"WaterBody_{cell[0]}_{cell[1]}", (cell[0] + 0.5, cell[1] + 0.5, 0.4), (1, 1, 0.8), physics)

    # -- outskirts mesas: continuous clay masses with sand tops (the arena ground map) ---------
    cliff_cells = set()
    for k, group in enumerate(components(plateau)):
        anchor = (min(c[0] for c in group), min(c[1] for c in group))
        local = [(i - anchor[0], j - anchor[1]) for i, j in group]
        p = kit["Piece"](f"arena_mesa_{k:02}")
        p.sharp_angle = math.radians(55)
        bm, _, _ = kit["clay_mass"](local, PLATEAU, side="cliff_side", top="sand")
        p.add(bm, lambda f: "sand" if f.normal.z > 0.82 else "cliff_side",
              lambda f, l: (0.0, 0.0) if f.normal.z > 0.82 else kit["uv_world"](top=PLATEAU)(f, l), cull_bottom=True)
        obj = p.build(collection=boundary)
        obj.location = _world(anchor)
        for i, j in group:
            if any((i + dx, j + dy) not in plateau for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                cliff_cells.add((i, j))
    # -- terrain covering every camera footprint (+ margin), with the baked unique colour map --
    image = _paint_ground(tex, bounds, contact, cliff_cells, GRASS, plateau)
    ground_material = _ground_material(image)
    holes = {c for c in all_water} | {c for c in plateau if all((c[0] + dx, c[1] + dy) in plateau for dx in (-1, 0, 1) for dy in (-1, 0, 1))}
    ground = bpy.data.objects.new("Arena02_Ground", _ground_mesh(bounds, ground_material, holes))
    floor.objects.link(ground)
    _body(ground, (2 * PLAY + 2, 2 * PLAY + 2, 0.4), (0, 0, -0.2))
    bpy.context.view_layer.update()
    for obj in list(water.objects) + [o for o in boundary.objects if o.name.startswith("arena_mesa_")]:
        _ground_remap(obj, ground_material, bounds, "canyon_sand")

    # -- boundary: invisible blockers at the playable edge (water, mesa, stream give the reasons)
    edge = PLAY + 0.25
    _blocker("Bound_North", (0, edge, 1.0), (2 * PLAY + 1, 0.5, 2.0), physics)
    _blocker("Bound_South", (0, -edge, 1.0), (2 * PLAY + 1, 0.5, 2.0), physics)
    _blocker("Bound_East", (edge, 0, 1.0), (0.5, 2 * PLAY + 1, 2.0), physics)
    _blocker("Bound_West", (-edge, 0, 1.0), (0.5, 2 * PLAY + 1, 2.0), physics)
    # Fences: along the west stream bank (playable edge) and on mesa rims, in short runs.
    def fence_run(name, start, steps, direction, z=0.0, collection=fences):
        (sx, sy), (dx, dy) = start, direction
        turns = {(1, 0): 0, (0, 1): 1, (-1, 0): 2, (0, -1): 3}[(dx, dy)]
        for k in range(steps + 1):
            point = (sx + dx * k, sy + dy * k, z)
            _put("fence_post", f"{name}_post_{k}", point, collection)
            if k < steps:
                _put("fence_rope_1m", f"{name}_rope_{k}", point, collection, turns)

    fence_run("WestFenceA", (-12.0, -6.0), 5, (0, 1))
    fence_run("WestFenceB", (-12.0, 2.0), 3, (0, 1))
    for k, group in enumerate(components(plateau)):
        rim = sorted(c for c in group if (c[0], c[1] - 1) not in plateau and (c[0], c[1] - 1) not in all_water)
        runs, run = [], []
        for c in rim:
            if run and (c[0] != run[-1][0] + 1 or c[1] != run[-1][1]):
                runs.append(run)
                run = []
            run.append(c)
        if run:
            runs.append(run)
        for r, run in enumerate(runs):
            if len(run) >= 3 and rng.random() < 0.7:
                start = rng.randrange(0, max(1, len(run) - 3))
                length = min(len(run) - start, rng.choice((2, 3, 4)))
                i, j = run[start]
                fence_run(f"MesaFence_{k}_{r}", (i + 0.5, j + 0.32), length, (1, 0), PLATEAU)
    # Mesa dressing: clusters of large broad-leaf plants (the concept's palms) with a few shrubs
    # and rocks, set one to three cells back from the rim so they frame the arena; open sand
    # between clusters keeps it from becoming a uniform scatter.
    taken = set()
    near_rim = [c for c in plateau if c not in cliff_cells and
                any((c[0] + dx, c[1] + dy) in cliff_cells for dx in (-2, -1, 0, 1, 2) for dy in (-2, -1, 0, 1, 2))]
    rng.shuffle(near_rim)
    centres = []
    for cell in near_rim:
        if all(abs(cell[0] - a) + abs(cell[1] - b) >= 7 for a, b in centres):
            centres.append(cell)
    for c, (cx, cy) in enumerate(centres):
        count = rng.choice((2, 3, 3, 4))
        for k in range(count):
            cell = (cx + rng.randint(-1, 1) * (k > 0), cy + rng.randint(-1, 1) * (k > 0))
            if cell in taken or cell not in plateau or cell in cliff_cells:
                continue
            taken.add(cell)
            if k == 0:
                piece, scale = "outskirts_plant", rng.uniform(1.6, 2.1)
            else:
                piece = rng.choice(("outskirts_plant", "small_shrub", "rock_group", "outskirts_plant", "cactus_flower"))
                scale = rng.uniform(1.2, 1.7) if piece == "outskirts_plant" else rng.uniform(0.9, 1.3)
            _put(piece, f"Mesa_{piece}_{cell[0]}_{cell[1]}", _world(cell, PLATEAU), outskirts_c, rng.randrange(4), scale)
    # Low sand outskirts (south-west foreground, east gap): rock groups, bones, small shrubs.
    low = [(i, j) for i in range(int(x0), int(x1)) for j in range(int(y0), int(y1))
           if not (-PLAY - 1 <= i < PLAY + 1 and -PLAY - 1 <= j < PLAY + 1) and (i, j) not in all_water and (i, j) not in plateau]
    rng.shuffle(low)
    placed = 0
    for cell in low:
        if placed >= 40:
            break
        if any((cell[0] + dx, cell[1] + dy) in taken for dx in (-2, -1, 0, 1, 2) for dy in (-2, -1, 0, 1, 2)):
            continue
        taken.add(cell)
        piece = rng.choice(("rock_group", "small_shrub", "bone_pile", "rock_tiles", "cactus_flower", "outskirts_plant"))
        _put(piece, f"Low_{piece}_{cell[0]}_{cell[1]}", _world(cell), outskirts_c, rng.randrange(4))
        placed += 1

    # -- static batching: water, grass visuals, fences and outskirts dressing ---------------------
    bpy.context.view_layer.update()
    for collection, prefix in ((water, "WaterChunk"), (vegetation, "GrassChunk"), (fences, "FenceChunk"), (outskirts_c, "OutskirtsChunk")):
        _merge(collection, prefix)

    # -- one camera (benchmark profile) and one afternoon sun ------------------------------------
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
    camera.location = Vector((*START_FOCUS, 0.0)) - forward * DISTANCE
    camera.rotation_euler = forward.to_track_quat("-Z", "Y").to_euler()
    scene.camera = camera
    scene.render.resolution_x, scene.render.resolution_y, scene.render.resolution_percentage = 1600, 900, 100
    sun = bpy.data.objects["Arena02_Sun"]
    elevation, azimuth = math.radians(SUN_ELEVATION), math.radians(SUN_AZIMUTH)
    to_sun = Vector((math.cos(elevation) * math.sin(azimuth), math.cos(elevation) * math.cos(azimuth), math.sin(elevation)))
    sun.rotation_euler = (-to_sun).to_track_quat("-Z", "Y").to_euler()
    sun.data.energy = SUN_INTENSITY
    sun.data.color = SUN_COLOR

    # -- player controller (no visible mesh or spawn platform) -----------------------------------
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
    print("arena-02 built:", counts, "terrain", bounds, "ground map", image.size[:])
    return bounds


def export():
    """Haki Export Arena, then apply the map's environment intensity (the add-on writes 1.0)."""
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


def render_checks(size=(1280, 720), spots=None):
    """Gameplay-camera renders at the startup view, centre, 4 edges and 4 corners (16:9, 21:9)."""
    scene = bpy.context.scene
    camera = scene.camera
    forward, _, _ = camera_basis()
    saved = (camera.location.copy(), scene.render.resolution_x, scene.render.resolution_y, scene.render.filepath)
    PREVIEWS.mkdir(parents=True, exist_ok=True)
    all_spots = {"start": START_FOCUS, "centre": (0, 0), "north": (0, PLAY), "south": (0, -PLAY), "east": (PLAY, 0),
                 "west": (-PLAY, 0), "ne": (PLAY, PLAY), "nw": (-PLAY, PLAY), "se": (PLAY, -PLAY), "sw": (-PLAY, -PLAY)}
    written = []
    try:
        for aspect_name, aspect in (("16x9", 16 / 9), ("21x9", 21 / 9)):
            scene.render.resolution_x = int(size[1] * aspect)
            scene.render.resolution_y = size[1]
            for name, (fx, fy) in all_spots.items():
                if spots and name not in spots:
                    continue
                if aspect_name == "21x9" and name not in ("start", "ne", "sw", "east", "west"):
                    continue
                camera.location = Vector((fx, fy, 0.0)) - forward * DISTANCE
                scene.render.filepath = str(PREVIEWS / f"{aspect_name}_{name}.png")
                bpy.ops.render.render(write_still=True)
                written.append(scene.render.filepath)
    finally:
        camera.location, scene.render.resolution_x, scene.render.resolution_y, scene.render.filepath = saved
    return written
