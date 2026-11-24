"""Canyon modular kit v2: builds every piece procedurally in Blender and exports one .glb per piece.

References: docs/references/kit-v2 (modelling sheets 01-05 and the gameplay concepts).

Run inside Blender (live via MCP, or headless) with canyon_kit.blend open:
    import runpy; ns = runpy.run_path("tools/blender/kits/canyon_kit.py")
    ns["build_all"](); ns["export_all"](); ns["render_sheets"]()

Conventions (artdirection.md section 7)
  * Metres, Blender Z-up, front = -Y, right = +X. Origins at ground level.
  * Single-cell pieces are centred on their cell. Multi-cell pieces (cover_straight_2, cover_L,
    cover_T, bank_2x1) use the centre of their first cell (the lowest x, then lowest y) as origin.
  * Only 0/90/180/270 degree yaw. Shape variation lives in the recipes (seeds), not in placement.
  * Materials carry painted, tileable textures (canyon_textures.py) mapped in world metres.
    UV offsets are chosen so that cell edges land on texture tile edges, so neighbouring modules
    continue the same texture across the join at any rotation.
  * Land top is z = 0, water top z = -0.06.
"""
import math
import os
import random
import runpy

import bmesh
import bpy
from mathutils import Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__)) if "__file__" in globals() else os.path.abspath("tools/blender/kits")
ROOT = os.path.abspath(os.path.join(HERE, "../../.."))
KIT_DIR = os.path.join(ROOT, "Assets/Game/Shared/Kits/canyon")
TEX_DIR = os.path.join(KIT_DIR, "Textures")
COLLECTION = "CanyonKit"
TEX = runpy.run_path(os.path.join(HERE, "canyon_textures.py"))
TEX["TEXTURES"]["cliff_side"] = (lambda: TEX["clay_side"](cap_role="floor_sand.edge"), True)


def srgb(value):
    return [float(c) for c in TEX["rgb"](value)]


def mix(a, b, t):
    return [float(c) for c in TEX["mix"](a, b, t)]


def to_linear(c):
    return [x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4 for x in c]


# ---------------------------------------------------------------- materials
WATER_TOP = -0.06
SHORE_LIP = 0.05
SHORE_BAND = 0.12

MATERIALS = {
    # name: texture / colour, normal map, roughness, emission
    "clay_side": dict(tex="clay_side", normal="clay_side_n", rough=0.9),
    "clay_top": dict(tex="clay_top", normal="clay_top_n", rough=0.9),
    "cliff_side": dict(tex="cliff_side", normal="cliff_side_n", rough=0.9),
    "sand": dict(tex="sand", rough=0.95),
    "wood": dict(tex="wood", normal="wood_n", rough=0.8),
    "wood_end": dict(tex="wood_end", rough=0.8),
    "rope": dict(tex="rope", rough=0.9),
    "grass": dict(tex="grass_blade", rough=0.7),
    "leaf": dict(tex="leaf", rough=0.65),
    "cactus": dict(tex="cactus", rough=0.7),
    "water": dict(tex="water", rough=0.12),
    "stem": dict(color=mix("crate_wood.band", "crate_wood.base", 0.6), rough=0.8),
    "thorn": dict(color=srgb("cactus.thorn"), rough=0.6),
    "petal": dict(color=srgb("flower.petal"), rough=0.6),
    "pollen": dict(color=srgb("flower.center"), rough=0.6),
    "lip": dict(color=srgb("shoreline.base"), rough=0.85),
    "lip_side": dict(color=srgb("shoreline.shade"), rough=0.85),
    "rock": dict(color=srgb("rock.base"), rough=0.85),
    "rock_coral": dict(color=mix("wall_top.base", "wall_side.base", 0.15), rough=0.85),
    "stone_tile": dict(color=mix("wall_top.base", "floor_sand.base", 0.45), rough=0.9),
    "iron": dict(color=srgb("iron.base"), rough=0.45, metal=0.35),
    "bone": dict(color=srgb("bone.base"), rough=0.6),
    "bone_dark": dict(color=mix("bone.shade", "outline", 0.55), rough=0.8),
    "haki_band": dict(color=mix("haki_orb.base", "crate_wood.band", 0.45), rough=0.6),
    "haki_ring": dict(color=mix("haki_orb.outline", "haki_orb.base", 0.25), rough=0.4),
    "haki_violet": dict(color=srgb("haki_orb.base"), rough=0.35),
    "haki_core": dict(color=mix("haki_orb.core", "haki_orb.base", 0.12), rough=0.3,
                      emission=mix("haki_orb.core", "haki_orb.base", 0.25), strength=3.0),
    "orb_swirl": dict(color=mix("haki_orb.outline", "haki_orb.base", 0.3), rough=0.35),
}

_images = {}


def texture_image(name, non_color=False):
    if name in _images and _images[name].name in bpy.data.images:
        return _images[name]
    path = os.path.join(TEX_DIR, name + ".png")
    image = bpy.data.images.load(path, check_existing=True)
    image.reload()
    image.colorspace_settings.name = "Non-Color" if non_color else "sRGB"
    _images[name] = image
    return image


def write_textures():
    """Paint every texture into the kit's Textures folder and reload any loaded images."""
    os.makedirs(TEX_DIR, exist_ok=True)
    TEX["write_all"](TEX_DIR)
    for image in bpy.data.images:
        if image.filepath and os.path.abspath(bpy.path.abspath(image.filepath)).startswith(TEX_DIR):
            image.reload()


def material(name):
    spec = MATERIALS[name]
    mat_name = "canyon_" + name
    mat = bpy.data.materials.get(mat_name) or bpy.data.materials.new(mat_name)
    mat.use_nodes = True
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    for node in list(nodes):
        if node.type not in ("BSDF_PRINCIPLED", "OUTPUT_MATERIAL"):
            nodes.remove(node)
    bsdf = next(n for n in nodes if n.type == "BSDF_PRINCIPLED")
    bsdf.inputs["Roughness"].default_value = spec.get("rough", 0.85)
    bsdf.inputs["Metallic"].default_value = spec.get("metal", 0.0)
    if "tex" in spec:
        tex = nodes.new("ShaderNodeTexImage")
        tex.image = texture_image(spec["tex"])
        tex.location = (-500, 250)
        tex.interpolation = "Linear"
        links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    else:
        bsdf.inputs["Base Color"].default_value = (*to_linear(spec["color"]), 1.0)
    if "normal" in spec:
        ntex = nodes.new("ShaderNodeTexImage")
        ntex.image = texture_image(spec["normal"], non_color=True)
        ntex.location = (-700, -150)
        nmap = nodes.new("ShaderNodeNormalMap")
        nmap.location = (-350, -150)
        links.new(ntex.outputs["Color"], nmap.inputs["Color"])
        links.new(nmap.outputs["Normal"], bsdf.inputs["Normal"])
    if "emission" in spec:
        bsdf.inputs["Emission Color"].default_value = (*to_linear(spec["emission"]), 1.0)
        bsdf.inputs["Emission Strength"].default_value = spec["strength"]
    return mat


# ---------------------------------------------------------------- UV rules (world metres)
def uv_world(scale=1.0, offset=(0.5, 0.5), top=None):
    """Planar projection in metres. Up/down faces use (x, y). Side faces use the horizontal
    tangent for u and height for v; with `top` set, v = 1 - (top - z), so the top of a wall
    always meets the top of the side texture (cap band) whatever the wall's height."""
    ox, oy = offset

    def rule(face, loop):
        n = face.normal
        p = loop.vert.co
        if abs(n.z) > 0.7:
            return ((p.x + ox) * scale, (p.y + oy) * scale)
        t = Vector((-n.y, n.x))
        if t.length < 1e-6:
            t = Vector((1, 0))
        t.normalize()
        u = (p.x + ox) * t.x + (p.y + oy) * t.y
        v = 1.0 - (top - p.z) if top is not None else p.z
        return (u * scale, v if top is not None else v * scale)

    return rule


def uv_grain(axis, scale=1.0):
    """Wood grain along `axis` (a Vector): u along the grain, v across it in the face plane."""
    axis = Vector(axis).normalized()

    def rule(face, loop):
        n = face.normal
        across = n.cross(axis)
        if across.length < 1e-4:  # end faces: planar across the section
            across = Vector((0, 0, 1)) if abs(axis.z) < 0.9 else Vector((1, 0, 0))
            other = axis.cross(across)
            return (loop.vert.co.dot(across) * scale, loop.vert.co.dot(other) * scale)
        across.normalize()
        return (loop.vert.co.dot(axis) * scale, loop.vert.co.dot(across) * scale)

    return rule


def uv_const(u, v):
    return lambda face, loop: (u, v)


def by_up(up, side, threshold=0.7):
    return lambda face: up if face.normal.z > threshold else side


# ---------------------------------------------------------------- piece builder
class Piece:
    """Accumulates parts (bmesh) with a material and UV rule each, into one mesh object."""

    def __init__(self, name):
        self.name = name
        self.bm = bmesh.new()
        self.uv = self.bm.loops.layers.uv.new("UVMap")
        self.materials = []
        self.sharp_angle = math.radians(40)
        self.flat = False

    def slot(self, name):
        if name not in self.materials:
            self.materials.append(name)
        return self.materials.index(name)

    def add(self, part, mat, uv=None, cull_bottom=False, cull_below=None, weld=True):
        """Merge `part`. `mat` is a material name or face -> name. `uv` is face, loop -> (u, v),
        or None to keep the part's own UVMap layer."""
        if weld:
            bmesh.ops.remove_doubles(part, verts=part.verts, dist=1e-5)
        part.normal_update()
        doomed = []
        for face in part.faces:
            zs = [v.co.z for v in face.verts]
            if cull_bottom and face.normal.z < -0.95:
                doomed.append(face)
            elif cull_below is not None and max(zs) <= cull_below + 1e-4:
                doomed.append(face)
        if doomed:
            bmesh.ops.delete(part, geom=doomed, context="FACES")
        layer = part.loops.layers.uv.get("UVMap") or part.loops.layers.uv.new("UVMap")
        for face in part.faces:
            face.material_index = self.slot(mat(face) if callable(mat) else mat)
            if uv is not None:
                for loop in face.loops:
                    loop[layer].uv = uv(face, loop)
        mesh = bpy.data.meshes.new("_part")
        part.to_mesh(mesh)
        part.free()
        self.bm.from_mesh(mesh)
        bpy.data.meshes.remove(mesh)
        return self

    def build(self, location=(0, 0, 0), collection=None):
        mesh = bpy.data.meshes.get(self.name) or bpy.data.meshes.new(self.name)
        # Tangents (for normal maps) need triangles or quads: split caps and other n-gons.
        bmesh.ops.triangulate(self.bm, faces=[f for f in self.bm.faces if len(f.verts) > 4], ngon_method="BEAUTY")
        self.bm.normal_update()
        self.bm.to_mesh(mesh)
        self.bm.free()
        for layer in list(mesh.uv_layers):
            if layer.name != "UVMap":
                mesh.uv_layers.remove(layer)
        material_indices = [poly.material_index for poly in mesh.polygons]
        mesh.materials.clear()
        for name in self.materials:
            mesh.materials.append(material(name))
        for poly, index in zip(mesh.polygons, material_indices):
            poly.material_index = index
            poly.use_smooth = not self.flat
        if not self.flat:
            mesh.set_sharp_from_angle(angle=self.sharp_angle)
        custom = mesh.attributes.get("_leaf_normal")
        if custom:
            mesh.normals_split_custom_set_from_vertices([tuple(Vector(d.vector).normalized()) for d in custom.data])
            mesh.attributes.remove(custom)
        obj = bpy.data.objects.get(self.name) or bpy.data.objects.new(self.name, mesh)
        obj.data = mesh
        target = collection or kit_collection()
        if obj.name not in target.objects:
            for c in list(obj.users_collection):
                c.objects.unlink(obj)
            target.objects.link(obj)
        obj.location = location
        obj.rotation_euler = (0, 0, 0)
        return obj


def kit_collection():
    collection = bpy.data.collections.get(COLLECTION)
    if collection is None:
        collection = bpy.data.collections.new(COLLECTION)
        bpy.context.scene.collection.children.link(collection)
    return collection


# ---------------------------------------------------------------- primitives
def box(mn, mx, bevel=0.0, segments=1, edges="all"):
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    size = Vector(mx) - Vector(mn)
    bmesh.ops.transform(bm, matrix=Matrix.Diagonal((*size, 1.0)), verts=bm.verts)
    bmesh.ops.translate(bm, vec=(Vector(mx) + Vector(mn)) / 2, verts=bm.verts)
    if bevel > 0:
        top = max(v.co.z for v in bm.verts)
        if edges == "top":
            chosen = [e for e in bm.edges if any(abs(v.co.z - top) < 1e-6 for v in e.verts)]
        else:
            chosen = list(bm.edges)
        bmesh.ops.bevel(bm, geom=chosen, offset=bevel, segments=segments, affect="EDGES", profile=0.5, clamp_overlap=True)
    return bm


def prism(points, z0, z1, bevel=0.0, segments=1):
    bm = bmesh.new()
    bottom = [bm.verts.new((x, y, z0)) for x, y in points]
    top = [bm.verts.new((x, y, z1)) for x, y in points]
    bm.faces.new(list(reversed(bottom)))
    bm.faces.new(top)
    n = len(points)
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((bottom[i], bottom[j], top[j], top[i]))
    bm.normal_update()
    if bevel > 0:
        chosen = [e for e in bm.edges if all(abs(v.co.z - z1) < 1e-6 for v in e.verts)]
        bmesh.ops.bevel(bm, geom=chosen, offset=bevel, segments=segments, affect="EDGES", profile=0.5, clamp_overlap=True)
    return bm


def ngon(sides, radius, centre=(0, 0), jitter=0.0, rng=None, phase=0.0, scale=(1, 1)):
    rng = rng or random.Random(0)
    out = []
    for i in range(sides):
        a = phase + math.tau * i / sides
        r = radius * (1 + rng.uniform(-jitter, jitter))
        out.append((centre[0] + math.cos(a) * r * scale[0], centre[1] + math.sin(a) * r * scale[1]))
    return out


def loft(rings, cap_start=True, cap_end=True):
    bm = bmesh.new()
    vertices = [[bm.verts.new(co) for co in ring] for ring in rings]
    count = len(vertices[0])
    if cap_start:
        bm.faces.new(list(reversed(vertices[0])))
    if cap_end:
        bm.faces.new(vertices[-1])
    for lower, upper in zip(vertices, vertices[1:]):
        for i in range(count):
            j = (i + 1) % count
            bm.faces.new((lower[i], lower[j], upper[j], upper[i]))
    bm.normal_update()
    return bm


def lathe(profile, segments=16, centre=(0, 0)):
    """Surface of revolution from (radius, z) pairs, bottom to top; capped at both ends."""
    rings = [[(centre[0] + r * math.cos(math.tau * i / segments), centre[1] + r * math.sin(math.tau * i / segments), z)
              for i in range(segments)] for r, z in profile]
    return loft(rings)


def tube_path(points, radius, sides=6, uv_length=None):
    """Tube along a path with capped ends; UVs run u along the length (metres / uv_length)."""
    points = [Vector(p) for p in points]
    rings, lengths = [], [0.0]
    for i in range(1, len(points)):
        lengths.append(lengths[-1] + (points[i] - points[i - 1]).length)
    for i, point in enumerate(points):
        tangent = (points[min(i + 1, len(points) - 1)] - points[max(0, i - 1)]).normalized()
        up = Vector((0, 0, 1)) if abs(tangent.z) < 0.9 else Vector((1, 0, 0))
        a = tangent.cross(up).normalized()
        b = tangent.cross(a).normalized()
        rings.append([point + radius * (a * math.cos(j * math.tau / sides) + b * math.sin(j * math.tau / sides)) for j in range(sides)])
    bm = loft(rings)
    if uv_length:
        layer = bm.loops.layers.uv.new("UVMap")
        index = {v: k for k, v in enumerate(bm.verts)}
        for face in bm.faces:
            for loop in face.loops:
                k = index[loop.vert]
                ring, j = divmod(k, sides)
                loop[layer].uv = (lengths[ring] / uv_length, j / sides)
    return bm


def sphere(centre, radius, scale=(1, 1, 1), segments=10, rings=7):
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=segments, v_segments=rings, radius=radius)
    bmesh.ops.transform(bm, matrix=Matrix.Diagonal((*scale, 1.0)), verts=bm.verts)
    bmesh.ops.translate(bm, vec=Vector(centre), verts=bm.verts)
    return bm


def rotated(bm, angle_z=0.0, angle_x=0.0, angle_y=0.0, pivot=(0, 0, 0)):
    m = Matrix.Rotation(angle_z, 4, "Z") @ Matrix.Rotation(angle_y, 4, "Y") @ Matrix.Rotation(angle_x, 4, "X")
    bmesh.ops.rotate(bm, verts=bm.verts, cent=Vector(pivot), matrix=m)
    return bm


def moved(bm, offset):
    bmesh.ops.translate(bm, vec=Vector(offset), verts=bm.verts)
    return bm


def rock_shape(centre, size, seed, facets=1):
    """Faceted low-poly rock: a squashed icosphere with seeded vertex offsets and a flat base."""
    rng = random.Random(seed)
    bm = bmesh.new()
    bmesh.ops.create_icosphere(bm, subdivisions=facets, radius=1.0)
    for v in bm.verts:
        v.co *= 1 + rng.uniform(-0.12, 0.12)
    bmesh.ops.transform(bm, matrix=Matrix.Diagonal((size[0] / 2, size[1] / 2, size[2] / 2, 1.0)), verts=bm.verts)
    for v in bm.verts:
        v.co.z += size[2] / 2 * 0.82
        v.co.z = max(v.co.z, 0.0)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-4)
    rotated(bm, angle_z=rng.uniform(0, math.tau))
    moved(bm, centre)
    return bm


# ---------------------------------------------------------------- grid outlines
DIRECTIONS = {"E": (1, 0), "N": (0, 1), "W": (-1, 0), "S": (0, -1)}
MODULES = {"pillar": "", "end": "E", "straight": "EW", "corner": "EN", "t": "EWN", "cross": "ENWS"}


def cell_outline(cells):
    """Counter-clockwise outline of a union of unit cells centred on integer coordinates."""
    cells = set(cells)
    edges = {}
    for i, j in cells:
        x0, y0, x1, y1 = i - 0.5, j - 0.5, i + 0.5, j + 0.5
        for a, b in (((x0, y0), (x1, y0)), ((x1, y0), (x1, y1)), ((x1, y1), (x0, y1)), ((x0, y1), (x0, y0))):
            if (b, a) in edges:
                del edges[(b, a)]
            else:
                edges[(a, b)] = True
    following = {a: b for a, b in edges}
    start = min(following)
    loop, point = [start], following[start]
    while point != start:
        loop.append(point)
        point = following[point]
    out = []
    n = len(loop)
    for k in range(n):
        a, b, c = Vector(loop[k - 1]), Vector(loop[k]), Vector(loop[(k + 1) % n])
        if abs((b - a).to_3d().cross((c - b).to_3d()).z) > 1e-9:
            out.append(loop[k])
    return out


def offset_ring(outline, inset, z, round_radius=0.0, steps=3, clamp=None):
    """Offset a rectilinear CCW outline inward by `inset` and round convex corners with a small arc
    (the same vertex count at any inset, so rings can be lofted). Points are clamped to +-clamp."""
    n = len(outline)
    ring = []
    for k in range(n):
        prev, cur, nxt = Vector(outline[k - 1]), Vector(outline[k]), Vector(outline[(k + 1) % n])
        a, b = (cur - prev).normalized(), (nxt - cur).normalized()
        na, nb = Vector((-a.y, a.x)), Vector((-b.y, b.x))
        v = cur + (na + nb) * inset
        if a.x * b.y - a.y * b.x > 0 and round_radius > 0:
            centre = v + (na + nb) * round_radius
            start = math.atan2(-na.y, -na.x)
            points = [centre + Vector((math.cos(start + (math.pi / 2) * s / steps), math.sin(start + (math.pi / 2) * s / steps))) * round_radius
                      for s in range(steps + 1)]
        else:
            points = [v] * (steps + 1) if round_radius > 0 else [v]
        for p in points:
            x, y = p.x, p.y
            if clamp is not None:
                x, y = min(max(x, -clamp), clamp), min(max(y, -clamp), clamp)
            ring.append((x, y, z))
    return ring


def drop_open_faces(bm, open_sides, limit=0.5):
    bm.normal_update()
    doomed = []
    for face in bm.faces:
        c = face.calc_center_median()
        for d in open_sides:
            dx, dy = DIRECTIONS[d]
            if abs(c.x * dx + c.y * dy - limit) < 1e-4 and (face.normal.x * dx + face.normal.y * dy) > 0.9:
                doomed.append(face)
                break
    bmesh.ops.delete(bm, geom=doomed, context="FACES")


def piece_for(cells, cell):
    """(module kind, quarter turns) for `cell` inside a set of cells; modules open E, EW, EN, EWN."""
    i, j = cell
    sides = "".join(d for d, (dx, dy) in DIRECTIONS.items() if (i + dx, j + dy) in cells)
    order = "ENWS"
    for kind, base in MODULES.items():
        if len(base) != len(sides):
            continue
        for turns in range(4):
            if {order[(order.index(c) + turns) % 4] for c in base} == set(sides):
                return kind, turns
    raise ValueError(f"no module for {sides}")


wall_piece_for = piece_for  # name used by older recipes


# ---------------------------------------------------------------- clay masses (cover, cliffs, banks)
COVER_HEIGHT = 1.0
COVER_LOW = 0.5
PLATEAU = 0.6


def clay_profile(height):
    """(z, inset) rings: a soft foot, shallow strata grooves where the side texture's seams fall,
    a slightly proud coral cap and a rounded top edge."""
    seams = [height - (1 - s) for s in TEX["CLAY_SEAMS"]]
    cap = seams[0]
    profile = [(0.0, 0.02), (0.025, 0.012)]
    for s in sorted(seams[1:]):
        if s > 0.06:
            profile += [(s - 0.012, 0.012), (s, 0.02), (s + 0.012, 0.012)]
    profile += [(cap - 0.01, 0.012), (cap, 0.018), (cap + 0.012, 0.0), (height - 0.05, 0.0),
                (height - 0.022, 0.01), (height - 0.007, 0.026), (height, 0.046)]
    return profile


def clay_mass(cells, height, open_sides="", clamp=None, side="clay_side", top="clay_top", corner=0.06):
    outline = cell_outline(cells)
    rings = [offset_ring(outline, inset, z, round_radius=corner, clamp=clamp) for z, inset in clay_profile(height)]
    bm = loft(rings)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-6)
    bmesh.ops.dissolve_degenerate(bm, edges=bm.edges, dist=1e-6)
    if open_sides:
        drop_open_faces(bm, open_sides)
    return bm, by_up(top, side, 0.82), uv_world(top=height)


def pebbles(piece, rng, count, height, area=0.32, mat="clay_top", centre=(0, 0)):
    """Small flattened stones resting on a cap, the 'pebble bumps' on the reference cover tops."""
    for _ in range(count):
        x = centre[0] + rng.uniform(-area, area)
        y = centre[1] + rng.uniform(-area, area)
        r = rng.uniform(0.05, 0.07)
        stone = sphere((x, y, height - 0.006), r, scale=(1.0, rng.uniform(0.75, 1.0), 0.5), segments=10, rings=6)
        piece.add(stone, mat, uv_world(), cull_below=height)
        if rng.random() < 0.6:
            r2 = r * rng.uniform(0.5, 0.7)
            a = rng.uniform(0, math.tau)
            stone = sphere((x + math.cos(a) * (r + r2 * 0.8), y + math.sin(a) * (r + r2 * 0.8), height - 0.004), r2, scale=(1, 1, 0.5), segments=8, rings=5)
            piece.add(stone, mat, uv_world(), cull_below=height)


def cover_module(name, open_sides="", height=COVER_HEIGHT, seed=1, stones=True):
    """One cell of continuous cover. `open_sides` (any of ENWS) join flush to the next module, so
    walls of any outline assemble with continuous strata and texture."""
    p = Piece(name)
    p.sharp_angle = math.radians(55)
    cells = [(0, 0)] + [DIRECTIONS[d] for d in open_sides]
    bm, mat, uv = clay_mass(cells, height, open_sides, clamp=0.5)
    p.add(bm, mat, uv, cull_bottom=True)
    if stones:
        rng = random.Random(seed * 31 + len(open_sides))
        pebbles(p, rng, rng.choice((1, 2)), height)
    return p.build()


def cover_piece(name, cells, height=COVER_HEIGHT, seed=1):
    """A whole cover shape as one continuous shell (the named sheet pieces)."""
    p = Piece(name)
    p.sharp_angle = math.radians(55)
    bm, mat, uv = clay_mass(cells, height)
    p.add(bm, mat, uv, cull_bottom=True)
    rng = random.Random(seed)
    for cell in rng.sample(list(cells), max(1, len(cells) // 2)):
        pebbles(p, rng, 1, height, area=0.3, centre=cell)
    return p.build()


def _module_builder(prefix, kind, builder, **kw):
    def build():
        return builder(f"{prefix}_{kind}", MODULES[kind], **kw)

    build.__name__ = f"{prefix}_{kind}"
    return build


COVER_MODULES = [_module_builder("wall", k, cover_module, seed=3) for k in MODULES]
COVER_LOW_MODULES = []
for _k in MODULES:
    def _low(kind=_k):
        return cover_module(f"wall_{kind}_low", MODULES[kind], height=COVER_LOW, seed=5)
    _low.__name__ = f"wall_{_k}_low"
    COVER_LOW_MODULES.append(_low)


def cover_unit():
    return cover_piece("cover_unit", [(0, 0)], seed=2)


def cover_straight_2():
    return cover_piece("cover_straight_2", [(0, 0), (1, 0)], seed=4)


def cover_L():
    return cover_piece("cover_L", [(0, 0), (1, 0), (0, 1)], seed=6)


def cover_T():
    return cover_piece("cover_T", [(0, 0), (1, 0), (2, 0), (1, 1)], seed=8)


def ground_module(name, open_sides="", height=PLATEAU):
    """Raised plateau ground (the 'flat ground block'): sand top, layered cliff sides."""
    p = Piece(name)
    p.sharp_angle = math.radians(55)
    cells = [(0, 0)] + [DIRECTIONS[d] for d in open_sides]
    bm, mat, uv = clay_mass(cells, height, open_sides, clamp=0.5, side="cliff_side", top="sand")
    p.add(bm, lambda f: "sand" if f.normal.z > 0.82 else "cliff_side",
          lambda f, l: uv_world(0.5)(f, l) if f.normal.z > 0.82 else uv(f, l), cull_bottom=True)
    return p.build()


GROUND_MODULES = [_module_builder("ground", k, ground_module) for k in MODULES]


def ground_block():
    return ground_module("ground_block", "")


def wavy_outline(width, depth, seed, radius=0.18, amplitude=0.035, step=0.09, centre=(0, 0)):
    """Rounded rectangle with a seeded wobble: outskirts banks and plateau edges."""
    rng = random.Random(seed)
    hw, hd = width / 2 - radius, depth / 2 - radius
    path = []
    corners = [((hw, -hd), -math.pi / 2), ((hw, hd), 0.0), ((-hw, hd), math.pi / 2), ((-hw, -hd), math.pi)]
    for (cx, cy), a0 in corners:
        for s in range(4):
            a = a0 + (math.pi / 2) * s / 3
            path.append((cx + math.cos(a) * radius, cy + math.sin(a) * radius))
        # straight run to the next corner
    dense = []
    for k in range(len(path)):
        a, b = Vector(path[k]), Vector(path[(k + 1) % len(path)])
        steps = max(1, int((b - a).length / step))
        for s in range(steps):
            dense.append(a + (b - a) * (s / steps))
    phase = rng.uniform(0, math.tau)
    out = []
    for k, p in enumerate(dense):
        normal = p.normalized() if p.length > 1e-6 else Vector((1, 0))
        wobble = amplitude * (0.6 * math.sin(k * 0.9 + phase) + 0.4 * math.sin(k * 2.3 + phase * 2)) + rng.uniform(-0.25, 0.25) * amplitude
        q = p + normal * wobble
        out.append((q.x + centre[0], q.y + centre[1]))
    return out


def free_mass(outline, height, side="cliff_side", top="sand"):
    """Clay mass from an arbitrary CCW outline (no grid corners)."""
    rings = []
    for z, inset in clay_profile(height):
        ring = []
        n = len(outline)
        for k in range(n):
            prev, cur, nxt = Vector(outline[k - 1]), Vector(outline[k]), Vector(outline[(k + 1) % n])
            a, b = (cur - prev).normalized(), (nxt - cur).normalized()
            inward = Vector((-a.y, a.x)) + Vector((-b.y, b.x))
            inward = inward.normalized() if inward.length > 1e-6 else Vector((0, 0))
            q = cur + inward * inset
            ring.append((q.x, q.y, z))
        rings.append(ring)
    bm = loft(rings)
    return bm


def bank_2x1():
    p = Piece("bank_2x1")
    p.sharp_angle = math.radians(55)
    bm = free_mass(wavy_outline(2.0, 1.0, 17, centre=(0.5, 0)), PLATEAU)
    p.add(bm, lambda f: "sand" if f.normal.z > 0.82 else "cliff_side",
          lambda f, l: uv_world(0.5)(f, l) if f.normal.z > 0.82 else uv_world(top=PLATEAU)(f, l), cull_bottom=True)
    return p.build()


# ---------------------------------------------------------------- floor
def sand_tile(name="sand_tile_a", chips=()):
    p = Piece(name)
    p.add(box((-0.5, -0.5, -0.08), (0.5, 0.5, 0.0), bevel=0.02, segments=2, edges="top"),
          by_up("sand", "sand", 0.0), uv_world(0.5), cull_bottom=True)
    rng = random.Random(len(name))
    for x, y, r in chips:
        shape = ngon(6, r, (x, y), jitter=0.12, rng=rng, phase=rng.uniform(0, 1))
        p.add(prism(shape, -0.01, 0.012, bevel=0.008), "stone_tile", uv_world(), cull_bottom=True)
    return p.build()


def sand_tile_a():
    return sand_tile("sand_tile_a", [(0.28, 0.3, 0.05)])


def sand_tile_b():
    return sand_tile("sand_tile_b", [(-0.15, -0.05, 0.075), (0.28, 0.12, 0.06)])


def rock_tiles(name="rock_tiles", seed=3, count=4):
    """A cluster of flat pentagonal stone slabs set into the sand (the concept's 4-slab clusters)."""
    rng = random.Random(seed)
    p = Piece(name)
    p.sharp_angle = math.radians(35)
    spots = [(-0.14, 0.13), (0.15, 0.15), (-0.13, -0.15), (0.16, -0.12)][:count]
    for x, y in spots:
        shape = ngon(5, rng.uniform(0.12, 0.14), (x + rng.uniform(-0.015, 0.015), y + rng.uniform(-0.015, 0.015)),
                     jitter=0.1, rng=rng, phase=rng.uniform(0, math.tau))
        p.add(prism(shape, -0.02, 0.035, bevel=0.015, segments=2), "stone_tile", uv_world(), cull_bottom=True)
    return p.build()


# ---------------------------------------------------------------- water and shores
def _water_cell_boundary(closed, inner):
    """Water region of one cell as a CCW list of (point, shore) where shore marks points whose
    following segment runs along land. `closed` = sides with land, `inner` = diagonal corners
    (e.g. "NE") that are land while both adjacent sides are water."""
    L = SHORE_LIP
    R = 0.24  # rounded corner where two banks meet
    corners = [("S", "E", (1, -1)), ("N", "E", (1, 1)), ("N", "W", (-1, 1)), ("S", "W", (-1, -1))]
    # Walk SE -> NE -> NW -> SW (CCW). Each corner contributes points; segments between corners
    # lie on one side: SE->NE east, NE->NW north, NW->SW west, SW->SE south.
    side_after = {0: "E", 1: "N", 2: "W", 3: "S"}
    side_before = {0: "S", 1: "E", 2: "N", 3: "W"}
    pts = []
    for idx, (sy, sx, (gx, gy)) in enumerate(corners):
        before, after = side_before[idx], side_after[idx]
        cx = gx * (0.5 - (L if sx in closed else 0.0))
        cy = gy * (0.5 - (L if sy in closed else 0.0))
        if sx in closed and sy in closed:
            # convex water corner: arc from the `before` side to the `after` side
            ox, oy = cx - gx * R, cy - gy * R
            a_before = math.atan2(*reversed((gx if before in "EW" else 0, gy if before in "NS" else 0)))
            a_after = math.atan2(*reversed((gx if after in "EW" else 0, gy if after in "NS" else 0)))
            while a_after < a_before:
                a_after += math.tau
            for s in range(5):
                a = a_before + (a_after - a_before) * s / 4
                pts.append(((ox + R * math.cos(a), oy + R * math.sin(a)), True))
        elif (sy + sx) in inner or (sx not in closed and sy not in closed and (sy + sx) in inner):
            # land corner poking into the water: concave arc round the cell corner
            ox, oy = gx * 0.5, gy * 0.5
            a_from = math.atan2(-gy if before in "NS" else 0, -gx if before in "EW" else 0)
            a_from = math.atan2(*reversed((0 if before in "NS" else -gx, 0 if before in "EW" else -gy)))
            # arc starts on the `before` edge, ends on the `after` edge, bulging toward the centre
            start = Vector((ox, oy)) + (Vector((0, -gy)) if before in "EW" else Vector((-gx, 0))) * L
            end = Vector((ox, oy)) + (Vector((-gx, 0)) if after in "NS" else Vector((0, -gy))) * L
            a0 = math.atan2(start.y - oy, start.x - ox)
            a1 = math.atan2(end.y - oy, end.x - ox)
            while a1 > a0:
                a1 -= math.tau
            for s in range(4):
                a = a0 + (a1 - a0) * s / 3
                pts.append(((ox + L * math.cos(a), oy + L * math.sin(a)), s < 3))
        else:
            pts.append(((cx, cy), after in closed))
    # mark segments: a segment along a closed side is shore
    out = []
    for k, (p, flag) in enumerate(pts):
        q = pts[(k + 1) % len(pts)][0]
        mid = ((p[0] + q[0]) / 2, (p[1] + q[1]) / 2)
        on_open = any(abs(mid[0] * dx + mid[1] * dy - 0.5) < 1e-4 for d, (dx, dy) in DIRECTIONS.items() if d not in closed)
        out.append((Vector(p), flag and not on_open))
    return out


def _keep_on_open_edge(q, r, closed):
    """A shore offset that starts on an open cell edge slides along that edge (so the band and lip
    meet the neighbouring water cell exactly); everything stays inside the cell."""
    r = Vector(r)
    for d, (dx, dy) in DIRECTIONS.items():
        if d in closed:
            continue
        if dx and abs(q.x * dx - 0.5) < 1e-6:
            r.x = q.x
        if dy and abs(q.y * dy - 0.5) < 1e-6:
            r.y = q.y
    return Vector((min(max(r.x, -0.5), 0.5), min(max(r.y, -0.5), 0.5)))


def water_module(name, closed="", inner=""):
    """One water cell. Land sides get a rounded brown lip (land top 0.00 -> water top -0.06) and a
    light shore band in the water; open sides join flush to the next water cell. Any pond, inlet
    or channel shape assembles from these, so every shore shares one profile."""
    p = Piece(name)
    p.sharp_angle = math.radians(50)
    loop = _water_cell_boundary(closed, inner)
    n = len(loop)
    pts = [q for q, _ in loop]
    shore_seg = [s for _, s in loop]
    # per-vertex outward (toward land) normal from adjacent shore segments
    normals = []
    for k in range(n):
        acc = Vector((0, 0))
        for seg in (k - 1, k):
            if shore_seg[seg % n]:
                a, b = pts[seg % n], pts[(seg + 1) % n]
                d = (b - a).normalized()
                acc += Vector((d.y, -d.x))  # right-hand normal of a CCW loop points outward
        normals.append(acc.normalized() if acc.length > 1e-6 else None)
    # water surface: outer band ring (at the shore) and inner ring (band width inward)
    bm = bmesh.new()
    uv = bm.loops.layers.uv.new("UVMap")
    outer = [bm.verts.new((q.x, q.y, WATER_TOP)) for q in pts]
    inner_ring = []
    for q, nrm in zip(pts, normals):
        r = q - nrm * SHORE_BAND if nrm is not None else q
        # keep open-edge points on their edge
        r = _keep_on_open_edge(q, r, closed)
        inner_ring.append(bm.verts.new((r.x, r.y, WATER_TOP)))
    band_u = {}
    for k in range(n):
        band_u[outer[k]] = 0.02 if normals[k] is not None else 0.98
        band_u[inner_ring[k]] = 0.98
    for k in range(n):
        j = (k + 1) % n
        quad = [outer[k], outer[j], inner_ring[j], inner_ring[k]]
        if len({v for v in quad}) == 4 and (outer[k].co - inner_ring[k].co).length + (outer[j].co - inner_ring[j].co).length > 1e-6:
            try:
                bm.faces.new(quad)
            except ValueError:
                pass
    unique_inner = []
    for v in inner_ring:
        if not unique_inner or (unique_inner[-1].co - v.co).length > 1e-6:
            unique_inner.append(v)
    if len(unique_inner) > 2 and (unique_inner[0].co - unique_inner[-1].co).length < 1e-6:
        unique_inner.pop()
    try:
        bm.faces.new(unique_inner)
    except ValueError:
        pass
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-6)
    bm.normal_update()
    for face in bm.faces:
        if face.normal.z < 0:
            face.normal_flip()
        for l in face.loops:
            l[uv].uv = (band_u.get(l.vert, 0.98), 0.5)
    bmesh.ops.triangulate(bm, faces=[f for f in bm.faces if len(f.verts) > 4])
    p.add(bm, "water", None, weld=False)
    # rim: loft along each shore run from the water edge out to the land top
    runs, current = [], []
    for k in range(n + 1):
        idx = k % n
        if shore_seg[idx]:
            if not current:
                current = [idx]
            current.append((idx + 1) % n)
        elif current:
            runs.append(current)
            current = []
    if current:
        if runs and runs[0][0] == current[-1]:
            runs[0] = current[:-1] + runs[0]
        else:
            runs.append(current)
    profile = [(0.0, WATER_TOP - 0.06, "lip_side"), (0.0, WATER_TOP + 0.025, "lip_side"),
               (0.012, -0.012, "lip"), (0.028, -0.001, "lip"), (SHORE_LIP, 0.0, "lip")]
    for run in runs:
        rim = bmesh.new()
        rows = []
        for idx in run:
            q, nrm = pts[idx], normals[idx] or Vector((0, 0))
            row = []
            for d, z, _ in profile:
                r = q + nrm * d
                r = _keep_on_open_edge(q, r, closed)
                row.append(rim.verts.new((r.x, r.y, z)))
            rows.append(row)
        for a, b in zip(rows, rows[1:]):
            for level in range(len(profile) - 1):
                quad = [a[level], a[level + 1], b[level + 1], b[level]]
                if len({tuple(round(c, 6) for c in v.co) for v in quad}) >= 3:
                    face = rim.faces.new(quad)
                    face.material_index = 0 if profile[level][2] == "lip_side" else 1
        bmesh.ops.remove_doubles(rim, verts=rim.verts, dist=1e-6)
        bmesh.ops.dissolve_degenerate(rim, edges=rim.edges, dist=1e-6)
        rim.normal_update()
        # faces must point up/toward the water
        p.add(rim, lambda f: "lip_side" if f.material_index == 0 else "lip", uv_const(0.5, 0.5), weld=False)
    # land fill: cell corners outside the rim (around rounded water corners)
    for idx, (sy, sx, (gx, gy)) in enumerate([("S", "E", (1, -1)), ("N", "E", (1, 1)), ("N", "W", (-1, 1)), ("S", "W", (-1, -1))]):
        if sx in closed and sy in closed:
            corner = Vector((gx * 0.5, gy * 0.5))
            arc = [pts[k] + normals[k] * SHORE_LIP for k in range(n)
                   if normals[k] is not None and (pts[k] - corner).length < 0.5 and
                   (pts[k].x * gx > 0.5 - SHORE_LIP - 0.3) and (pts[k].y * gy > 0.5 - SHORE_LIP - 0.3)]
            if len(arc) >= 2:
                fan = bmesh.new()
                c = fan.verts.new((corner.x, corner.y, 0.0))
                vs = [fan.verts.new((min(max(a.x, -0.5), 0.5), min(max(a.y, -0.5), 0.5), 0.0)) for a in arc]
                for a, b in zip(vs, vs[1:]):
                    if (a.co - b.co).length > 1e-6:
                        f = fan.faces.new((c, a, b))
                fan.normal_update()
                for f in fan.faces:
                    if f.normal.z < 0:
                        f.normal_flip()
                p.add(fan, "sand", uv_world(0.5))
    return p.build()


def water_fill():
    return water_module("water_fill", "")


def water_bank():
    return water_module("water_bank", "N")


def water_outer():
    return water_module("water_outer", "NE")


def water_inner():
    return water_module("water_inner", "", "NE")


def water_key(cells, cell):
    """(closed sides, inner corners) for a water cell inside a set of water cells."""
    i, j = cell
    closed = "".join(d for d, (dx, dy) in DIRECTIONS.items() if (i + dx, j + dy) not in cells)
    inner = ""
    for a, b in (("N", "E"), ("N", "W"), ("S", "E"), ("S", "W")):
        if a not in closed and b not in closed:
            dx, dy = DIRECTIONS[b][0], DIRECTIONS[a][1]
            if (i + dx, j + dy) not in cells:
                inner += a + b
    return closed, inner


# ---------------------------------------------------------------- gameplay grass
GRASS_HEIGHT = 0.65
BLADE_RINGS = ((0.0, 0.5), (0.16, 0.86), (0.36, 1.0), (0.58, 0.88), (0.78, 0.6), (0.91, 0.3), (1.0, 0.0))


def blade(base, heading, lean, length, width=0.1, thickness=0.07, curl=0.35, sides=6):
    """Chunky teardrop blade (sheet 03 #3): rounded section, widest low, pointed tip, curving
    toward `heading`. UVs: u across the blade, v root -> tip (grass_blade.png)."""
    out = Vector((math.cos(heading), math.sin(heading), 0.0))
    side = Vector((-out.y, out.x, 0.0))
    up = Vector((0, 0, 1))
    rings, centres, frames = [], [], []
    for t, w in BLADE_RINGS:
        a = lean + curl * t * t
        centre = Vector(base) + out * (math.sin(lean) * length * t + curl * 0.25 * length * t ** 3) + up * (math.cos(a) * length * t)
        half_w, half_t = width * w / 2, thickness * max(w, 0.05) / 2
        normal_dir = (out * math.cos(a) - up * math.sin(a)).normalized()
        ring = []
        for k in range(sides):
            ang = math.tau * k / sides
            ring.append(tuple(centre + side * (math.cos(ang) * half_w) - normal_dir * (math.sin(ang) * half_t)))
        rings.append(ring)
        frames.append((side, normal_dir))
    bm = loft(rings, cap_start=True, cap_end=False)
    # collapse the tip ring to a point
    tip = list(bm.verts)[-sides:]
    tip_centre = sum((v.co for v in tip), Vector()) / sides
    for v in tip:
        v.co = tip_centre
    bmesh.ops.remove_doubles(bm, verts=tip, dist=1e-6)
    uv = bm.loops.layers.uv.new("UVMap")
    normals = bm.verts.layers.float_vector.new("_leaf_normal")
    root = Vector(base)
    for v in bm.verts:
        rel = v.co - root
        t = min(max(rel.dot(up) / max(length, 1e-6), 0.0), 1.0)
        radial = rel - up * rel.dot(up) - out * (math.sin(lean) * rel.dot(up))
        radial = radial.normalized() if radial.length > 1e-6 else out
        v[normals] = (up * 0.75 + radial * 0.45 + out * 0.2).normalized()
    for face in bm.faces:
        for loop in face.loops:
            rel = loop.vert.co - root
            t = min(max(rel.dot(up) / max(length * 0.98, 1e-6), 0.0), 1.0)
            s = rel.dot(side) / (width * 0.5)
            loop[uv].uv = (0.5 + 0.48 * max(-1.0, min(1.0, s)), t)
    return bm


def grass_module(name, open_sides="", seed=1):
    """Gameplay concealment grass for one cell (sheet 03 #1/#2): a dense field of overlapping
    teardrop blades, about 6 x 6 per cell, full from above. Blades along closed (exposed) sides
    are shorter and lean outward, so a patch reads as one rounded mass; open sides continue into
    the neighbouring module with no border. A low root mass hides the soil between blades."""
    rng = random.Random(seed * 7919 + sum(ord(c) * (k + 1) for k, c in enumerate(open_sides)))
    p = Piece(name)
    p.sharp_angle = math.radians(89)
    closed = [d for d in DIRECTIONS if d not in open_sides]
    # root mass (orange-dark, mostly hidden), flush on open sides
    cells = [(0, 0)] + [DIRECTIONS[d] for d in open_sides]
    outline = cell_outline(cells)
    body_profile = [(0.0, 0.16), (0.08, 0.16), (0.13, 0.22)]
    rings = [offset_ring(outline, inset, z, round_radius=0.1, clamp=0.5) for z, inset in body_profile]
    body = loft(rings)
    bmesh.ops.remove_doubles(body, verts=body.verts, dist=1e-6)
    drop_open_faces(body, open_sides)
    body_normals = body.verts.layers.float_vector.new("_leaf_normal")
    for v in body.verts:
        v[body_normals] = (0, 0, 1)
    p.add(body, "grass", uv_const(0.5, 0.12), cull_bottom=True)
    n = 7
    step = 1.0 / n
    for ix in range(n):
        for iy in range(n):
            x = -0.5 + step * (ix + 0.5) + rng.uniform(-0.3, 0.3) * step
            y = -0.5 + step * (iy + 0.5) + rng.uniform(-0.3, 0.3) * step
            # distance to the nearest exposed edge
            edge, outward = 1.0, None
            for d in closed:
                dx, dy = DIRECTIONS[d]
                dist = 0.5 - (x * dx + y * dy)
                if dist < edge:
                    edge, outward = dist, math.atan2(dy, dx)
            rim = 1 - min(edge / 0.3, 1.0)
            height = GRASS_HEIGHT * rng.uniform(0.86, 1.0) * (1 - 0.15 * rim)
            if outward is not None and rim > 0:
                heading = outward + rng.uniform(-0.6, 0.6)
                lean = math.radians(rng.uniform(6, 12) + 10 * rim)
            else:
                heading = rng.uniform(0, math.tau)
                lean = math.radians(rng.uniform(2, 8))
            width = rng.uniform(0.14, 0.17)
            # keep the leaning tips inside the cell on open sides
            reach = math.sin(lean) * height + 0.05
            bx = x - math.cos(heading) * reach * 0.3
            by = y - math.sin(heading) * reach * 0.3
            for d in DIRECTIONS:
                dx, dy = DIRECTIONS[d]
                limit = 0.5 if d in open_sides else 0.54
                over = (bx + math.cos(heading) * reach) * dx + (by + math.sin(heading) * reach) * dy - limit
                if over > 0:
                    bx -= dx * over
                    by -= dy * over
            p.add(blade((bx, by, -0.03), heading, lean, height / math.cos(lean * 0.7) + 0.03, width,
                        thickness=width * 0.7, curl=rng.uniform(0.08, 0.22)), "grass", None, cull_below=0.0)
    return p.build()


GRASS_MODULES = [_module_builder("grass", k, grass_module, seed=3) for k in MODULES]


def grass_a():
    return grass_module("grass_a", "", seed=11)


def grass_b():
    return grass_module("grass_b", "", seed=29)


def grass_blade():
    p = Piece("grass_blade")
    p.sharp_angle = math.radians(89)
    p.add(blade((0, 0, 0), math.pi / 2, math.radians(3), 0.665, 0.1, 0.07, curl=0.22), "grass", None)
    return p.build()


# ---------------------------------------------------------------- outskirts dressing
def cactus_flower():
    """Barrel cactus with ribs, thorn studs and one flower (sheet 03 #4), 0.6 x 0.6 x 1.0."""
    p = Piece("cactus_flower")
    p.sharp_angle = math.radians(70)
    ribs, segments = 8, 32
    body_h, radius = 0.92, 0.27
    rings = []
    zs = [0.0, 0.08, 0.27, 0.49, 0.67, 0.8, 0.88, 0.92]
    radii = [0.8, 0.95, 1.0, 1.0, 0.95, 0.8, 0.55, 0.0]
    for z, rr in zip(zs, radii):
        ring = []
        for k in range(segments):
            a = math.tau * k / segments
            rib = 1 + 0.07 * math.cos(ribs * a)
            r = max(radius * rr * rib, 0.002)
            ring.append((math.cos(a) * r, math.sin(a) * r, z))
        rings.append(ring)
    body = loft(rings, cap_end=False)
    p.add(body, "cactus", lambda f, l: (math.atan2(l.vert.co.y, l.vert.co.x) / math.tau * 4, l.vert.co.z / body_h), cull_bottom=True)
    rng = random.Random(4)
    for k in range(ribs):
        a = math.tau * k / ribs
        for z, rr in ((0.2, 1.0), (0.45, 1.0), (0.66, 0.92)):
            if rng.random() < 0.85:
                aa = a + (math.pi / ribs if z == 0.45 else 0)
                r = radius * rr * 1.07 + 0.005
                p.add(sphere((math.cos(aa) * r, math.sin(aa) * r, z), 0.022, segments=6, rings=4), "thorn", uv_const(0.5, 0.5))
    # flower: five rounded petals and a domed centre
    top = body_h - 0.02
    for k in range(5):
        a = math.tau * k / 5 + 0.3
        petal = sphere((math.cos(a) * 0.075, math.sin(a) * 0.075, top + 0.035), 0.06, scale=(1.25, 0.9, 0.45), segments=8, rings=5)
        rotated(petal, angle_z=a, pivot=(math.cos(a) * 0.075, math.sin(a) * 0.075, top + 0.035))
        p.add(petal, "petal", uv_const(0.5, 0.5))
    p.add(sphere((0, 0, top + 0.06), 0.045, scale=(1, 1, 0.6), segments=8, rings=5), "pollen", uv_const(0.5, 0.5))
    return p.build()


def broad_leaf(base, heading, length, width, droop=0.25, lift=0.0, sides=6, rows=6):
    """Rounded broad leaf (outskirts plant): an ovate sheet with a midrib crease, drooping at its
    tip. UV u across, v base -> tip (leaf.png). Double-sided by thickness."""
    out = Vector((math.cos(heading), math.sin(heading), 0))
    side = Vector((-out.y, out.x, 0))
    up = Vector((0, 0, 1))
    bm = bmesh.new()
    uv = bm.loops.layers.uv.new("UVMap")
    normals = bm.verts.layers.float_vector.new("_leaf_normal")
    grid_top, grid_bot = [], []
    for r in range(rows + 1):
        t = r / rows
        w = width * math.sin(math.pi * min(0.08 + t * 0.92, 1.0)) ** 0.7 * (1 - 0.15 * t)
        row_t, row_b = [], []
        for c in range(sides + 1):
            s = c / sides * 2 - 1
            along = length * t
            z = lift * t - droop * length * t * t + 0.05 * width * (1 - abs(s)) - 0.03 * width * s * s
            pos = Vector(base) + out * along + side * (s * w / 2) + up * z
            vt = bm.verts.new(pos + up * 0.006)
            vb = bm.verts.new(pos - up * 0.006)
            bend = (up + out * (2 * droop * t) - side * (0.35 * s)).normalized()
            vt[normals] = bend
            vb[normals] = -bend
            row_t.append((vt, (0.5 + 0.5 * s, t)))
            row_b.append((vb, (0.5 + 0.5 * s, t)))
        grid_top.append(row_t)
        grid_bot.append(row_b)
    for r in range(rows):
        for c in range(sides):
            for grid, flip in ((grid_top, True), (grid_bot, False)):
                quad = [grid[r][c], grid[r][c + 1], grid[r + 1][c + 1], grid[r + 1][c]]
                if flip:
                    quad = quad[::-1]
                verts = [q[0] for q in quad]
                if len(set(verts)) < 3:
                    continue
                try:
                    face = bm.faces.new(verts)
                except ValueError:
                    continue
                for loop, (_, coord) in zip(face.loops, quad):
                    loop[uv].uv = coord
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-6)
    return bm


def outskirts_plant(name="outskirts_plant", seed=5, size=1.0, leaves=6):
    """Broad-leaf outskirts plant (sheet 03 #5): big rounded leaves on curved brown stems."""
    rng = random.Random(seed)
    p = Piece(name)
    p.sharp_angle = math.radians(89)
    for k in range(leaves):
        heading = math.tau * k / leaves + rng.uniform(-0.3, 0.3)
        reach = rng.uniform(0.1, 0.2) * size
        top = rng.uniform(0.56, 0.74) * size
        out = Vector((math.cos(heading), math.sin(heading), 0))
        stem = [tuple(out * (reach * t * t) + Vector((0, 0, top * t))) for t in (0, 0.33, 0.66, 1.0)]
        p.add(tube_path(stem, 0.02 * size, sides=6, uv_length=1.0), "stem", None)
        p.add(broad_leaf(Vector(stem[-1]) - out * 0.06, heading + rng.uniform(-0.2, 0.2), rng.uniform(0.4, 0.48) * size,
                         rng.uniform(0.4, 0.46) * size, droop=rng.uniform(0.4, 0.6), lift=0.08 * size, rows=7, sides=7), "leaf", None)
    for k in range(4):
        heading = math.tau * k / 4 + rng.uniform(-0.4, 0.4)
        p.add(broad_leaf((0, 0, 0.05), heading, 0.3 * size, 0.26 * size, droop=0.7, lift=0.15 * size), "leaf", None)
    return p.build()


def small_shrub():
    """Low four-leaf clover shrub (sheet 05), 1 x 1 x 0.6 footprint, about 0.35 m tall."""
    rng = random.Random(8)
    p = Piece("small_shrub")
    p.sharp_angle = math.radians(89)
    for k in range(4):
        heading = math.tau * k / 4 + math.pi / 4 + rng.uniform(-0.15, 0.15)
        out = Vector((math.cos(heading), math.sin(heading), 0))
        p.add(tube_path([(0, 0, 0), tuple(out * 0.05 + Vector((0, 0, 0.12)))], 0.022, sides=5, uv_length=1.0), "stem", None)
        p.add(broad_leaf(tuple(out * 0.02 + Vector((0, 0, 0.14))), heading, 0.36, 0.36, droop=0.45, lift=0.12, rows=7, sides=7), "leaf", None)
    return p.build()


def rock_group():
    """Three faceted rocks (sheet 03 #7): a large mauve-grey rock, a coral rock and a small one."""
    p = Piece("rock_group")
    p.sharp_angle = math.radians(32)
    p.add(rock_shape((0.12, 0.08, 0), (0.86, 0.74, 0.8), 3, facets=2), "rock", uv_const(0.5, 0.5), cull_bottom=True)
    p.add(rock_shape((-0.42, -0.16, 0), (0.5, 0.42, 0.38), 7, facets=2), "rock_coral", uv_const(0.5, 0.5), cull_bottom=True)
    p.add(rock_shape((0.58, -0.26, 0), (0.3, 0.26, 0.2), 11, facets=2), "rock", uv_const(0.5, 0.5), cull_bottom=True)
    return p.build()


def bone_pile():
    """Sparse bones (sheet 05): a femur, a skull and a few upright shards, 0.2 m tall."""
    p = Piece("bone_pile")
    p.sharp_angle = math.radians(60)

    def femur(centre, heading, length):
        out = Vector((math.cos(heading), math.sin(heading), 0))
        a = Vector(centre) - out * length / 2 + Vector((0, 0, 0.03))
        b = Vector(centre) + out * length / 2 + Vector((0, 0, 0.03))
        p.add(tube_path([tuple(a), tuple(b)], 0.018, sides=6), "bone", uv_const(0.5, 0.5))
        side = Vector((-out.y, out.x, 0))
        for end in (a, b):
            for s in (-1, 1):
                p.add(sphere(tuple(end + side * 0.022 * s), 0.03, segments=6, rings=4), "bone", uv_const(0.5, 0.5))

    femur((0.15, 0.2, 0), 0.5, 0.34)
    femur((-0.25, -0.05, 0), 2.2, 0.2)
    # skull
    p.add(sphere((0.2, -0.22, 0.09), 0.095, scale=(1.0, 0.95, 0.95), segments=10, rings=7), "bone", uv_const(0.5, 0.5), cull_below=0.0)
    p.add(box((0.15, -0.33, 0.0), (0.25, -0.24, 0.07), bevel=0.02), "bone", uv_const(0.5, 0.5), cull_bottom=True)
    for s in (-1, 1):
        p.add(sphere((0.2 + 0.035 * s, -0.305, 0.11), 0.026, scale=(1, 0.5, 1.1), segments=6, rings=4), "bone_dark", uv_const(0.5, 0.5))
    # upright shards
    rng = random.Random(2)
    for x, y in ((-0.3, 0.22), (-0.2, 0.3), (-0.36, 0.08), (-0.1, 0.12)):
        h = rng.uniform(0.07, 0.13)
        p.add(tube_path([(x, y, 0), (x + rng.uniform(-0.02, 0.02), y, h)], 0.013, sides=5), "bone", uv_const(0.5, 0.5))
    return p.build()


# ---------------------------------------------------------------- wooden props
CRATE = 0.9


def plank(mn, mx, axis, bevel=0.012):
    return box(mn, mx, bevel=bevel, segments=1), uv_grain(axis)


def crate_shell(p, h=CRATE, braces=("front", "back"), top_x=True, cracks=False):
    """Crate construction (sheet 04): a recessed panel core, frame boards round every face,
    horizontal planks between them and diagonal braces; the lid has an X."""
    r = h / 2
    frame, proud = 0.1, 0.025
    p.add(box((-r + 0.02, -r + 0.02, 0.0), (r - 0.02, r - 0.02, h - 0.02)), "wood", uv_grain((1, 0, 0)), cull_bottom=True)
    # vertical corner posts and horizontal frame rails
    for sx in (-1, 1):
        for sy in (-1, 1):
            x0, x1 = sorted((sx * r, sx * (r - frame)))
            y0, y1 = sorted((sy * r, sy * (r - frame)))
            bm, uv = plank((x0, y0, 0.0), (x1, y1, h), (0, 0, 1))
            p.add(bm, "wood", uv, cull_bottom=True)
    for z0 in (0.0, h - frame):
        for axis in ((1, 0, 0), (0, 1, 0)):
            for s in (-1, 1):
                if axis[0]:
                    mn = (-r + frame, min(s * r, s * r - s * proud), z0)
                    mx = (r - frame, max(s * r, s * r - s * proud), z0 + frame)
                else:
                    mn = (min(s * r, s * r - s * proud), -r + frame, z0)
                    mx = (max(s * r, s * r - s * proud), r - frame, z0 + frame)
                bm, uv = plank(mn, mx, axis)
                p.add(bm, "wood", uv, cull_bottom=True)
    # side planks: three horizontal boards per side, inset slightly behind the frame
    inner = r - 0.008
    for face_axis, s in (("y", -1), ("y", 1), ("x", -1), ("x", 1)):
        for k in range(3):
            z0 = frame + k * (h - 2 * frame) / 3 + 0.006
            z1 = frame + (k + 1) * (h - 2 * frame) / 3 - 0.006
            if face_axis == "y":
                mn, mx = (-r + frame, min(s * inner, s * (inner - 0.02)), z0), (r - frame, max(s * inner, s * (inner - 0.02)), z1)
                axis = (1, 0, 0)
            else:
                mn, mx = (min(s * inner, s * (inner - 0.02)), -r + frame, z0), (max(s * inner, s * (inner - 0.02)), r - frame, z1)
                axis = (0, 1, 0)
            bm, uv = plank(mn, mx, axis, bevel=0.008)
            p.add(bm, "wood", uv)
    # diagonal braces on the front/back
    span = h - 2 * frame
    length = math.hypot(span, span) - 0.05
    for name, s, sign in (("front", -1, 1), ("back", 1, -1)):
        if name not in braces:
            continue
        brace = box((-length / 2, -0.012, -0.05), (length / 2, 0.012, 0.05), bevel=0.01)
        rotated(brace, angle_y=math.radians(45 * sign))
        moved(brace, (0, s * (r + 0.004), h / 2))
        p.add(brace, "wood", uv_grain((1, 0, 1) if sign > 0 else (1, 0, -1)))
    # lid: planks and an X brace
    for k in range(3):
        x0 = -r + frame + k * (2 * r - 2 * frame) / 3 + 0.006
        x1 = -r + frame + (k + 1) * (2 * r - 2 * frame) / 3 - 0.006
        bm, uv = plank((x0, -r + frame, h - 0.04), (x1, r - frame, h - 0.005), (0, 1, 0), bevel=0.008)
        p.add(bm, "wood", uv, cull_bottom=True)
    if top_x:
        for sign in (1, -1):
            brace = box((-length / 2, -0.05, -0.012), (length / 2, 0.05, 0.012), bevel=0.01)
            rotated(brace, angle_z=math.radians(45 * sign))
            moved(brace, (0, 0, h + 0.004))
            p.add(brace, "wood", uv_grain((1, sign, 0)))


def crate():
    p = Piece("crate")
    p.sharp_angle = math.radians(40)
    crate_shell(p)
    return p.build()


def barrel():
    """Stave barrel (sheet 04): bulging staves with shallow grooves, two iron bands, a lid with
    a rim and three boards. 0.65 m diameter, 0.9 m high."""
    p = Piece("barrel")
    p.sharp_angle = math.radians(35)
    staves, per = 14, 3
    segments = staves * per
    h, r_end, r_mid = 0.9, 0.29, 0.325
    rings = []
    for z in (0.0, 0.08, 0.2, 0.33, 0.45, 0.57, 0.7, 0.82, h):
        t = z / h
        r = r_end + (r_mid - r_end) * math.sin(math.pi * t)
        ring = []
        for k in range(segments):
            a = math.tau * k / segments
            groove = 0.006 if k % per == 0 else 0.0
            ring.append((math.cos(a) * (r - groove), math.sin(a) * (r - groove), z))
        rings.append(ring)
    body = loft(rings, cap_start=False, cap_end=False)
    p.add(body, "wood", lambda f, l: (l.vert.co.z, math.atan2(l.vert.co.y, l.vert.co.x) * r_mid), cull_bottom=True)
    # lid: recessed boards inside a raised rim
    p.add(lathe([(r_end - 0.035, h - 0.03), (r_end - 0.035, h - 0.035)], segments=segments), "stem", uv_const(0.5, 0.5))
    rim = lathe([(r_end + 0.001, h - 0.002), (r_end + 0.002, h + 0.012), (r_end - 0.03, h + 0.012), (r_end - 0.034, h - 0.04)], segments=segments)
    p.add(rim, "wood", lambda f, l: (math.atan2(l.vert.co.y, l.vert.co.x) * r_end, l.vert.co.z))
    for k in (-1, 0, 1):
        w = 0.17
        half = math.sqrt(max((r_end - 0.04) ** 2 - (k * w) ** 2, 0.01))
        board = box((k * w - w / 2 + 0.011, -half + 0.035, h - 0.03), (k * w + w / 2 - 0.011, half - 0.035, h - 0.004), bevel=0.008)
        p.add(board, "wood", uv_grain((0, 1, 0)))
    # iron bands
    for zc in (0.2, 0.7):
        t = zc / h
        r = r_end + (r_mid - r_end) * math.sin(math.pi * t) + 0.004
        band = lathe([(r - 0.004, zc - 0.045), (r + 0.012, zc - 0.04), (r + 0.014, zc), (r + 0.012, zc + 0.04), (r - 0.004, zc + 0.045)], segments=36)
        p.add(band, "iron", uv_const(0.5, 0.5))
    return p.build()


FENCE_HEIGHT = 0.8
ROPE_LEVELS = (0.42, 0.62)


def fence_post():
    p = Piece("fence_post")
    p.sharp_angle = math.radians(60)
    profile = [(0.085, 0.0), (0.092, 0.05), (0.09, 0.4), (0.093, 0.7), (0.088, FENCE_HEIGHT - 0.03), (0.07, FENCE_HEIGHT)]
    rings = [[(math.cos(math.tau * k / 14) * r, math.sin(math.tau * k / 14) * r, z) for k in range(14)] for r, z in profile]
    body = loft(rings, cap_start=False, cap_end=False)
    p.add(body, "wood", lambda f, l: (l.vert.co.z, math.atan2(l.vert.co.y, l.vert.co.x) * 0.09))
    cap = bmesh.new()
    ring = [cap.verts.new((math.cos(math.tau * k / 14) * 0.07, math.sin(math.tau * k / 14) * 0.07, FENCE_HEIGHT)) for k in range(14)]
    cap.faces.new(ring)
    p.add(cap, "wood_end", lambda f, l: (l.vert.co.x / 0.25 + 0.5, l.vert.co.y / 0.25 + 0.5))
    # rope lashings where the ropes meet the post
    for z in ROPE_LEVELS:
        lash = [(0.1 * math.cos(t * math.tau * 2.2 / 24), 0.1 * math.sin(t * math.tau * 2.2 / 24), z - 0.025 + t * 0.05 / 24) for t in range(25)]
        p.add(tube_path(lash, 0.02, sides=6, uv_length=0.25), "rope", None)
    return p.build()


def fence_rope(name="fence_rope_1m", length=1.0):
    """Two sagging ropes from this cell's post (x = 0) to the next post at x = length."""
    p = Piece(name)
    p.sharp_angle = math.radians(70)
    for z in ROPE_LEVELS:
        pts = [(length * t, 0.0, z - 0.05 * math.sin(math.pi * t)) for t in [k / 10 for k in range(11)]]
        p.add(tube_path(pts, 0.03, sides=7, uv_length=0.25), "rope", None)
    return p.build()


def fence_rope_1m():
    return fence_rope()


def fence_2m():
    """Two posts 1 m apart with ropes (sheet 04 / 05 fence segment), origin on the first post."""
    p = Piece("fence_2m")
    p.sharp_angle = math.radians(60)
    post =bpy.data.objects.get("fence_post") or fence_post()
    rope = bpy.data.objects.get("fence_rope_1m") or fence_rope_1m()
    for src, offsets in ((post, (0.0, 1.0)), (rope, (0.0,))):
        for x in offsets:
            bm = bmesh.new()
            bm.from_mesh(src.data)
            moved(bm, (x, 0, 0))
            names = [m.name.replace("canyon_", "") for m in src.data.materials]
            p.add(bm, lambda f, names=names: names[f.material_index], None, weld=False)
    return p.build()


# ---------------------------------------------------------------- Haki caches and orbs
def crest(p, centre, normal, radius=0.12):
    """Inset violet orb crest: a dark ring, a violet disc and a bright core (sheet 04)."""
    normal = Vector(normal).normalized()
    rot = normal.to_track_quat("Z", "Y").to_matrix().to_4x4()

    def disc(r0, z0, z1, mat, segs=20):
        bm = lathe([(r0, z0), (r0, z1), (0.0001, z1)], segments=segs)
        bmesh.ops.transform(bm, matrix=rot, verts=bm.verts)
        moved(bm, centre)
        p.add(bm, mat, uv_const(0.5, 0.5))

    disc(radius, -0.01, 0.012, "haki_ring")
    disc(radius * 0.78, 0.0, 0.02, "haki_violet")
    dome = sphere((0, 0, 0.018), radius * 0.48, scale=(1, 1, 0.45), segments=12, rings=6)
    bmesh.ops.transform(dome, matrix=rot, verts=dome.verts)
    moved(dome, centre)
    p.add(dome, "haki_core", uv_const(0.5, 0.5))


def haki_band(p, h=CRATE, z0=0.36, z1=0.54):
    r = h / 2 + 0.006
    p.add(box((-r, -r, z0), (r, r, z1), bevel=0.01), "haki_band", uv_const(0.5, 0.5))


def haki_cache():
    p = Piece("haki_cache")
    p.sharp_angle = math.radians(40)
    crate_shell(p, braces=("back",), top_x=False)
    haki_band(p)
    crest(p, (0, -CRATE / 2 - 0.006, 0.45), (0, -1, 0))
    crest(p, (0, 0, CRATE + 0.0), (0, 0, 1), radius=0.14)
    return p.build()


def haki_cache_damaged():
    """Same footprint, visibly split: a cracked lid sitting askew, a broken corner board,
    dark cracks across the front (sheet 04)."""
    p = Piece("haki_cache_damaged")
    p.sharp_angle = math.radians(40)
    crate_shell(p, braces=("back",), top_x=False)
    haki_band(p)
    crest(p, (0, -CRATE / 2 - 0.006, 0.45), (0, -1, 0))
    crest(p, (0, 0, CRATE), (0, 0, 1), radius=0.14)
    # cracks: dark recessed zigzags on the front, right and lid
    crack_mat = "bone_dark"
    for path in ([(-0.12, -0.462, 0.88), (-0.06, -0.462, 0.72), (-0.12, -0.462, 0.6), (-0.04, -0.462, 0.42)],
                 [(0.18, -0.462, 0.3), (0.1, -0.462, 0.18), (0.16, -0.462, 0.05)],
                 [(0.462, -0.2, 0.86), (0.462, -0.05, 0.66), (0.462, -0.15, 0.5), (0.462, 0.05, 0.3)],
                 [(0.25, -0.3, 0.905), (0.12, -0.1, 0.905), (0.2, 0.08, 0.905)]):
        p.add(tube_path(path, 0.01, sides=4), crack_mat, uv_const(0.5, 0.5))
    # a broken-off chunk at the top front-left corner, leaning out
    chunk = box((-0.45, -0.47, 0.72), (-0.25, -0.42, 0.9), bevel=0.01)
    rotated(chunk, angle_y=math.radians(-12), angle_x=math.radians(10), pivot=(-0.35, -0.45, 0.72))
    p.add(chunk, "wood", uv_grain((0, 0, 1)))
    return p.build()


def haki_cache_broken():
    """Debris of a broken cache around the exposed crest board (sheet 04), same origin."""
    p = Piece("haki_cache_broken")
    p.sharp_angle = math.radians(40)
    rng = random.Random(12)
    # crest board lying in the middle
    p.add(box((-0.17, -0.17, 0.0), (0.17, 0.17, 0.05), bevel=0.012), "wood", uv_grain((1, 0, 0)), cull_bottom=True)
    crest(p, (0, 0, 0.05), (0, 0, 1), radius=0.11)
    pieces = [(-0.36, 0.0, 0.4, 0.1, 90, True), (0.36, 0.05, 0.36, 0.1, 85, True), (0.05, 0.36, 0.42, 0.11, 8, False),
              (-0.05, -0.36, 0.34, 0.1, -10, False), (-0.3, 0.3, 0.26, 0.09, 45, False), (0.3, -0.3, 0.24, 0.09, -40, False),
              (0.32, 0.3, 0.2, 0.08, 120, True)]
    for x, y, length, width, yaw, upright in pieces:
        if upright:
            board = box((-length / 2, -0.03, 0.0), (length / 2, 0.03, width * 1.4), bevel=0.01)
            axis = (1, 0, 0)
        else:
            board = box((-length / 2, -width / 2, 0.0), (length / 2, width / 2, 0.05), bevel=0.01)
            axis = (1, 0, 0)
        rotated(board, angle_z=math.radians(yaw + rng.uniform(-6, 6)))
        moved(board, (x, y, 0))
        p.add(board, "wood", uv_grain(Matrix.Rotation(math.radians(yaw), 3, "Z") @ Vector(axis)), cull_bottom=True)
    # a strip of the violet band
    band = box((-0.2, -0.04, 0.0), (0.2, 0.04, 0.03), bevel=0.008)
    rotated(band, angle_z=math.radians(-25))
    moved(band, (-0.12, -0.2, 0.0))
    p.add(band, "haki_band", uv_const(0.5, 0.5), cull_bottom=True)
    return p.build()


ORB_RADIUS = 0.14
ORB_HOVER = 0.2


def haki_orb():
    """Haki orb (sheet 04): a 0.28 m violet sphere with a dark swirl band and a bright core.
    The origin is on the ground; the orb hovers 0.2 m above it. Glow comes from the emissive
    core and bloom, not from extra geometry."""
    p = Piece("haki_orb")
    p.sharp_angle = math.radians(80)
    c = Vector((0, 0, ORB_HOVER + ORB_RADIUS))
    p.add(sphere(c, ORB_RADIUS, segments=20, rings=12), "haki_violet", uv_const(0.5, 0.5))
    # swirl: a raised band wrapping around the sphere, tilted, thick on one side
    swirl = []
    for k in range(33):
        t = k / 32
        a = -math.pi * 0.95 + t * math.pi * 1.7
        tilt = 0.35 * math.sin(a * 0.8 + 0.6)
        d = Vector((math.cos(a) * math.cos(tilt), math.sin(a) * math.cos(tilt), math.sin(tilt) + 0.15))
        swirl.append(tuple(c + d.normalized() * (ORB_RADIUS - 0.004)))
    p.add(tube_path(swirl, 0.024, sides=8), "orb_swirl", uv_const(0.5, 0.5))
    core_dir = Vector((-0.35, -1.0, 0.15)).normalized()
    ring = lathe([(ORB_RADIUS * 0.4, -0.006), (ORB_RADIUS * 0.42, 0.004), (ORB_RADIUS * 0.3, 0.008)], segments=24)
    rot = core_dir.to_track_quat("Z", "Y").to_matrix().to_4x4()
    bmesh.ops.transform(ring, matrix=rot, verts=ring.verts)
    moved(ring, c + core_dir * (ORB_RADIUS * 0.93))
    p.add(ring, "haki_ring", uv_const(0.5, 0.5))
    core = sphere((0, 0, 0), ORB_RADIUS * 0.3, scale=(1, 1, 0.55), segments=14, rings=8)
    bmesh.ops.transform(core, matrix=rot, verts=core.verts)
    moved(core, c + core_dir * (ORB_RADIUS * 0.94))
    p.add(core, "haki_core", uv_const(0.5, 0.5))
    return p.build()


# ---------------------------------------------------------------- catalogue
PIECES = [
    cover_unit, cover_straight_2, cover_L, cover_T,
    *COVER_MODULES, *COVER_LOW_MODULES,
    ground_block, *GROUND_MODULES, bank_2x1,
    sand_tile_a, sand_tile_b, rock_tiles,
    water_fill, water_bank, water_outer, water_inner,
    grass_a, grass_b, grass_blade, *GRASS_MODULES,
    cactus_flower, outskirts_plant, small_shrub, rock_group, bone_pile,
    crate, barrel, fence_post, fence_rope_1m, fence_2m,
    haki_cache, haki_cache_damaged, haki_cache_broken, haki_orb,
]

# Sheet sizes (x, y, z) from docs/references/kit-v2, checked by check_dimensions().
SHEET_SIZES = {
    "cover_unit": (1, 1, 1), "cover_straight_2": (2, 1, 1), "cover_L": (2, 2, 1), "cover_T": (3, 2, 1),
    "ground_block": (1, 1, 0.6), "bank_2x1": (2, 1, 0.6),
    "sand_tile_a": (1, 1, 0.08), "sand_tile_b": (1, 1, 0.08),
    "water_fill": (1, 1, None), "water_bank": (1, 1, None), "water_outer": (1, 1, None), "water_inner": (1, 1, None),
    "grass_a": (1, 1, 0.65), "grass_b": (1, 1, 0.65), "grass_blade": (0.1, 0.07, 0.65),
    "cactus_flower": (0.6, 0.6, 1.0), "outskirts_plant": (1, 1, 0.8), "rock_group": (1.5, 1, 0.8),
    "crate": (0.9, 0.9, 0.9), "barrel": (0.65, 0.65, 0.9), "fence_post": (None, None, 0.8),
    "haki_cache": (0.9, 0.9, 0.9), "haki_orb": (0.28, 0.28, 0.28),
}


def clear_kit():
    collection = bpy.data.collections.get(COLLECTION)
    if collection:
        for obj in list(collection.objects):
            mesh = obj.data
            bpy.data.objects.remove(obj, do_unlink=True)
            if mesh and mesh.users == 0:
                bpy.data.meshes.remove(mesh)


def build_all(spacing=3.4, only=None):
    write_textures()
    if only is None:
        clear_kit()
    objects = []
    for i, builder in enumerate(PIECES):
        if only and builder.__name__ not in only:
            continue
        obj = builder()
        obj.location = ((i % 8) * spacing, -(i // 8) * spacing, 0)
        objects.append(obj)
    return objects


def stats():
    out = []
    for builder in PIECES:
        obj = bpy.data.objects.get(builder.__name__)
        if obj:
            out.append((obj.name, sum(len(p.vertices) - 2 for p in obj.data.polygons)))
    return out


def check_dimensions(tolerance=0.03):
    problems = []
    for name, size in SHEET_SIZES.items():
        obj = bpy.data.objects.get(name)
        if not obj:
            problems.append((name, "missing"))
            continue
        xs = [v.co.x for v in obj.data.vertices]
        ys = [v.co.y for v in obj.data.vertices]
        zs = [v.co.z for v in obj.data.vertices]
        actual = (max(xs) - min(xs), max(ys) - min(ys), max(zs) - min(zs))
        for axis, want, have in zip("xyz", size, actual):
            if want is not None and abs(want - have) > max(tolerance, want * 0.06):
                problems.append((name, axis, want, round(have, 3)))
    return problems


def export_all(names=None):
    models = os.path.join(KIT_DIR, "Models")
    os.makedirs(models, exist_ok=True)
    for o in bpy.context.selected_objects:
        o.select_set(False)
    for builder in PIECES:
        if names and builder.__name__ not in names:
            continue
        obj = bpy.data.objects[builder.__name__]
        saved = obj.location.copy()
        obj.location = (0, 0, 0)
        obj.select_set(True)
        bpy.context.view_layer.objects.active = obj
        bpy.ops.export_scene.gltf(
            filepath=os.path.join(models, obj.name + ".glb"),
            use_selection=True, export_yup=True, export_apply=True, export_tangents=True,
            export_cameras=False, export_lights=False, export_materials="EXPORT", export_image_format="AUTO",
        )
        obj.select_set(False)
        obj.location = saved


# ---------------------------------------------------------------- turnaround sheets
SHEET_VIEWS = {  # name: (direction from target to camera)
    "front": Vector((0, -1, 0)), "right": Vector((1, 0, 0)), "back": Vector((0, 1, 0)),
    "top": Vector((0, -0.0001, 1)), "34": Vector((0.75, -1.0, 0.95)),
}
SHEET_DIR = os.path.join(ROOT, "build/kit-v2-sheets")


def _studio(scene):
    world = scene.world or bpy.data.worlds.new("KitStudio")
    scene.world = world
    world.use_nodes = True
    bg = next(n for n in world.node_tree.nodes if n.type == "BACKGROUND")
    bg.inputs["Color"].default_value = (0.78, 0.77, 0.80, 1)
    bg.inputs["Strength"].default_value = 0.5
    sun_data = bpy.data.lights.get("KitSun") or bpy.data.lights.new("KitSun", "SUN")
    sun_data.energy = 2.4
    sun_data.color = (1.0, 0.96, 0.9)
    sun_data.angle = math.radians(8)
    sun = bpy.data.objects.get("KitSun") or bpy.data.objects.new("KitSun", sun_data)
    if sun.name not in scene.collection.objects:
        scene.collection.objects.link(sun)
    sun.rotation_euler = (math.radians(40), 0, math.radians(-35))
    return sun


def render_piece_views(name, size=360, views=SHEET_VIEWS):
    """Orthographic front/right/back/top/3-4 views of one piece, like the reference sheets."""
    scene = bpy.context.scene
    obj = bpy.data.objects[name]
    bpy.context.view_layer.update()
    pts = [obj.matrix_world @ Vector(c) for c in obj.bound_box]
    lo = Vector([min(p[i] for p in pts) for i in range(3)])
    hi = Vector([max(p[i] for p in pts) for i in range(3)])
    centre = (lo + hi) / 2
    extent = max(hi - lo)
    cam_data = bpy.data.cameras.get("KitSheetCam") or bpy.data.cameras.new("KitSheetCam")
    cam = bpy.data.objects.get("KitSheetCam") or bpy.data.objects.new("KitSheetCam", cam_data)
    if cam.name not in scene.collection.objects:
        scene.collection.objects.link(cam)
    cam_data.type = "ORTHO"
    cam_data.clip_end = 100
    _studio(scene)
    hidden = []
    for other in scene.objects:
        if other.type == "MESH" and other != obj:
            hidden.append((other, other.hide_render))
            other.hide_render = True
    old = (scene.camera, scene.render.resolution_x, scene.render.resolution_y, scene.render.filepath, scene.render.film_transparent)
    os.makedirs(SHEET_DIR, exist_ok=True)
    out = []
    try:
        scene.camera = cam
        scene.render.resolution_x = scene.render.resolution_y = size
        scene.render.film_transparent = False
        for view, direction in views.items():
            d = direction.normalized()
            cam.location = centre + d * (extent * 3 + 4)
            cam.rotation_euler = (-d).to_track_quat("-Z", "Y").to_euler()
            cam_data.ortho_scale = extent * 1.25
            path = os.path.join(SHEET_DIR, f"{name}_{view}.png")
            scene.render.filepath = path
            bpy.ops.render.render(write_still=True)
            out.append(path)
    finally:
        scene.camera, scene.render.resolution_x, scene.render.resolution_y, scene.render.filepath, scene.render.film_transparent = old
        for other, flag in hidden:
            other.hide_render = flag
    return out


def render_sheets(names=None, size=300):
    done = []
    for builder in PIECES:
        if names and builder.__name__ not in names:
            continue
        done += render_piece_views(builder.__name__, size)
    return done


# ---------------------------------------------------------------- assembly sample (join test)
SAMPLE = "CanyonSample"


def place_cells(collection, prefix, cells, origin=(0, 0), z=0.0, builder=None):
    """Place module instances for a set of cells (walls, grass or plateau ground)."""
    out = []
    for cell in sorted(cells):
        kind, turns = piece_for(cells, cell)
        name = f"{prefix}_{kind}"
        obj = bpy.data.objects.new(f"{prefix}_{cell[0]}_{cell[1]}", bpy.data.objects[name].data)
        obj.location = (origin[0] + cell[0], origin[1] + cell[1], z)
        obj.rotation_euler = (0, 0, turns * math.pi / 2)
        collection.objects.link(obj)
        out.append(obj)
    return out


def water_piece(cells, cell):
    """Mesh object name for a water cell; variants are generated on demand and cached."""
    closed, inner = water_key(cells, cell)
    name = "water_" + (closed or "o") + ("_" + inner if inner else "")
    if name not in bpy.data.objects:
        obj = water_module(name, closed, inner)
        obj.location = (0, 0, -50)
    return name


def place_water(collection, cells, origin=(0, 0)):
    out = []
    for cell in sorted(cells):
        name = water_piece(cells, cell)
        obj = bpy.data.objects.new(f"Water_{cell[0]}_{cell[1]}", bpy.data.objects[name].data)
        obj.location = (origin[0] + cell[0], origin[1] + cell[1], 0)
        collection.objects.link(obj)
        out.append(obj)
    return out


def build_sample(origin=(0.0, -16.0)):
    collection = bpy.data.collections.get(SAMPLE) or bpy.data.collections.new(SAMPLE)
    if collection.name not in bpy.context.scene.collection.children:
        bpy.context.scene.collection.children.link(collection)
    for obj in list(collection.objects):
        bpy.data.objects.remove(obj, do_unlink=True)
    ox, oy = origin
    # sand ground with holes for water
    water = {(1, 1), (2, 1), (1, 2), (1, 3)} | {(6, 5), (7, 5), (8, 5), (7, 4)} | {(10, 1), (11, 1), (12, 1), (11, 0), (11, 2)}
    bm = bmesh.new()
    for i in range(-2, 15):
        for j in range(-2, 10):
            if (i, j) in water:
                continue
            bm.faces.new([bm.verts.new((ox + i + dx, oy + j + dy, 0)) for dx, dy in ((-0.5, -0.5), (0.5, -0.5), (0.5, 0.5), (-0.5, 0.5))])
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-6)
    p = Piece("sample_ground")
    p.add(bm, "sand", uv_world(0.5, offset=(0, 0)))
    ground = p.build(collection=collection)
    place_water(collection, water, origin)
    place_cells(collection, "grass", {(4, 1), (4, 2), (4, 3), (5, 3), (6, 3)} | {(9, 4), (10, 4), (9, 5), (10, 5)}, origin)
    place_cells(collection, "wall", {(0, 6), (1, 6), (2, 6), (3, 6), (3, 7)}, origin)
    place_cells(collection, "wall", {(6, 7), (7, 7), (8, 7), (7, 8)}, origin)
    for name, cell in (("crate", (13, 6)), ("barrel", (12, 6)), ("haki_cache", (12, 4)), ("haki_orb", (13, 4)),
                       ("cactus_flower", (0, 3)), ("rock_tiles", (5, 0)), ("bone_pile", (8, 1)), ("rock_group", (13, 8)),
                       ("outskirts_plant", (11, 8)), ("small_shrub", (9, 8))):
        obj = bpy.data.objects.new("S_" + name, bpy.data.objects[name].data)
        obj.location = (ox + cell[0], oy + cell[1], 0)
        collection.objects.link(obj)
    return collection


def render_sample(path=None, focus=(6.5, 3.5), origin=(0.0, -16.0), distance=19.0, pitch=57, fov=25):
    """Gameplay-camera render of the sample (benchmark pitch and FOV, closer distance)."""
    scene = bpy.context.scene
    cam_data = bpy.data.cameras.get("SampleCam") or bpy.data.cameras.new("SampleCam")
    cam = bpy.data.objects.get("SampleCam") or bpy.data.objects.new("SampleCam", cam_data)
    if cam.name not in scene.collection.objects:
        scene.collection.objects.link(cam)
    cam_data.type = "PERSP"
    cam_data.sensor_fit = "VERTICAL"
    cam_data.angle_y = math.radians(fov)
    pr = math.radians(pitch)
    forward = Vector((0, math.cos(pr), -math.sin(pr)))
    target = Vector((origin[0] + focus[0], origin[1] + focus[1], 0))
    cam.location = target - forward * distance
    cam.rotation_euler = forward.to_track_quat("-Z", "Y").to_euler()
    _studio(scene)
    hidden = [(o, o.hide_render) for o in kit_collection().objects]
    for o, _ in hidden:
        o.hide_render = True
    old = (scene.camera, scene.render.resolution_x, scene.render.resolution_y, scene.render.filepath)
    try:
        scene.camera = cam
        scene.render.resolution_x, scene.render.resolution_y = 1280, 720
        scene.render.filepath = path or os.path.join(SHEET_DIR, "sample_gameplay.png")
        bpy.ops.render.render(write_still=True)
    finally:
        scene.camera, scene.render.resolution_x, scene.render.resolution_y, scene.render.filepath = old
        for o, flag in hidden:
            o.hide_render = flag
    return scene.render.filepath
