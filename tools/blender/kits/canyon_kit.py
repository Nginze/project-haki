"""Canyon modular kit: builds every piece procedurally in Blender and exports one .glb per piece.

Run inside Blender (live via MCP, or headless):
    exec(open("tools/blender/kits/canyon_kit.py").read()); build_all(); export_all()

Conventions
  * 1 grid cell = 1 m. Every piece's origin is the centre of its cell at ground level (z = 0).
  * Floor pieces (sand / water) sit below the origin with their walkable top at z = 0.
  * Blocking pieces fill the cell edge to edge (+-0.5 m) so neighbours join without gaps.
  * The whole kit shares ONE material and ONE 64x64 palette texture: every face's UVs point at
    a flat-colour swatch, so pieces batch together and need no other textures.
  * Rotate pieces in 90 degree steps to cover every orientation (edges, corners, fences).
"""
import math
import os
import random

import bmesh
import bpy
from mathutils import Matrix, Vector

KIT_DIR = "/home/nginze/Documents/engine-sandbox/Assets/Game/Shared/Kits/canyon"
COLLECTION = "CanyonKit"
MATERIAL = "canyon_palette"

# ---------------------------------------------------------------- palette (from docs/palette.json)
# Every colour comes from a role in docs/palette.json (the canonical art-direction palette).
# Swatch names are what the geometry code asks for; ROLE_MAP says which palette role(s) feed
# each one. A string is a flat swatch; a list is a vertical gradient (bottom -> top) whose V
# coordinate comes from vertex height (see Piece.gradient_range), used for soft baked darkening.
import json

ROOT = os.path.abspath(os.path.join(KIT_DIR, "../../../../.."))
PALETTE_JSON = os.path.join(ROOT, "docs", "palette.json")


def _load_palette():
    with open(PALETTE_JSON, encoding="utf-8") as f:
        data = json.load(f)
    roles = {}
    for group in ("environment", "teams", "powers", "ui"):
        for role, value in data.get(group, {}).items():
            if isinstance(value, dict):
                for variant, hexcolor in value.items():
                    roles[f"{role}.{variant}"] = hexcolor.lstrip("#").upper()
            else:
                roles[role] = value.lstrip("#").upper()
    return roles, data["rules"]


ROLES, RULES = _load_palette()
SHADOW = RULES["shadowColor"].lstrip("#")
MIN_BAKED = RULES["minBakedBrightness"]


def _rgb(hexcolor):
    return [int(hexcolor[k : k + 2], 16) / 255 for k in (0, 2, 4)]


def _hex(rgb):
    return "".join(f"{round(min(max(c, 0.0), 1.0) * 255):02X}" for c in rgb)


def _luma(rgb):
    lin = [c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4 for c in rgb]
    return 0.2126 * lin[0] + 0.7152 * lin[1] + 0.0722 * lin[2]


def mix(a, b, t):
    """Blend two palette roles (or hex values) in sRGB."""
    a, b = _rgb(ROLES.get(a, a)), _rgb(ROLES.get(b, b))
    return _hex([x + (y - x) * t for x, y in zip(a, b)])


def baked(role, amount):
    """Soft contact darkening: lean `role` toward the cool shadow colour by `amount`, but never
    below minBakedBrightness of its unoccluded brightness (art direction section 5)."""
    base = ROLES.get(role, role)
    for step in range(101):
        t = amount * (1 - step / 100)
        candidate = mix(base, SHADOW, t)
        if _luma(_rgb(candidate)) >= MIN_BAKED * _luma(_rgb(base)):
            return candidate
    return base


ROLE_MAP = {
    # floor
    "sand": "floor_sand.base",
    "sand_side": "floor_sand.edge",
    # cover walls, boundary masses and rocks share one family: light tops, darker sides
    "wall_top": "wall_top.base",
    "wall_side": [baked("wall_side.base", 0.45), "wall_side.base", mix("wall_side.base", "wall_top.base", 0.18)],
    "rock": mix("wall_top.base", "wall_side.base", 0.25),
    "rock_dark": ["wall_side.base", mix("wall_side.base", "wall_top.base", 0.15)],
    # water stays cyan/teal, well away from the reserved ally blue
    "water": mix("water.shallow", "water.deep", 0.45),
    "water_side": "water.deep",
    "bank": "shoreline.base",
    "bank_side": "shoreline.shade",
    # wood: lighter lids/caps, darker recessed panels and bands
    "wood": ["crate_wood.base", mix("crate_wood.base", "crate_wood.highlight", 0.4)],
    "wood_top": "crate_wood.highlight",
    "wood_cap": mix("crate_wood.highlight", "rope.highlight", 0.3),
    "wood_panel": mix("crate_wood.base", "crate_wood.band", 0.3),
    "wood_dark": "crate_wood.band",
    "rope": "rope.base",
    # foliage: coherent clump shading from shade (roots) through base to highlight (tips)
    "leaf_grad": [baked("bush.shade", 0.2), "bush.shade", "bush.base", "bush.highlight"],
    "shrub_grad": [baked("bush.shade", 0.25), "bush.shade", mix("bush.base", "floor_grass.base", 0.35), mix("bush.highlight", "floor_grass.base", 0.4)],
    # cactus / flower (also exported as small solid materials by canyon_arena_assets.py)
    "cactus": ["cactus.shade", "cactus.base", "cactus.highlight"],
    "cactus_thorn": "cactus.thorn",
    "flower": "flower.petal",
    "flower_center": "flower.center",
    # spare floor families for future maps
    "grass_floor": "floor_grass.base",
    "stone_floor": "floor_stone.base",
}


def _resolve(value):
    return ROLES.get(value, value)


PALETTE = {}
GRADIENTS = {}
for _name, _value in ROLE_MAP.items():
    if isinstance(_value, list):
        GRADIENTS[_name] = [_resolve(v) for v in _value]
        PALETTE[_name] = GRADIENTS[_name][len(_value) // 2]
    else:
        PALETTE[_name] = _resolve(_value)
SWATCHES = list(PALETTE)
GRID = math.ceil(math.sqrt(len(SWATCHES)))
SWATCH_PX = 16


def swatch_uv(name):
    i = SWATCHES.index(name)
    col, row = i % GRID, i // GRID
    return ((col + 0.5) / GRID, 1.0 - (row + 0.5) / GRID)


def _gradient(stops, t):
    t = min(max(t, 0.0), 1.0) * (len(stops) - 1)
    i = min(int(t), len(stops) - 2)
    a, b = _rgb(stops[i]), _rgb(stops[i + 1])
    return [x + (y - x) * (t - i) for x, y in zip(a, b)]


def palette_image():
    size = GRID * SWATCH_PX
    image = bpy.data.images.get(MATERIAL)
    if image is None or tuple(image.size) != (size, size):
        if image:
            bpy.data.images.remove(image)
        image = bpy.data.images.new(MATERIAL, size, size, alpha=False)
    pixels = [0.0] * (size * size * 4)
    for y in range(size):
        row = (size - 1 - y) // SWATCH_PX  # pixel rows are bottom-up, swatch rows top-down
        for x in range(size):
            index = row * GRID + x // SWATCH_PX
            swatch = SWATCHES[index] if index < len(SWATCHES) else None
            if swatch is None:
                rgb = _rgb(ROLES["floor_sand.base"])
            elif swatch in GRADIENTS:
                rgb = _gradient(GRADIENTS[swatch], (y % SWATCH_PX) / (SWATCH_PX - 1))
            else:
                # This is a byte-buffer sRGB image. Store encoded swatches; the image
                # texture performs the sRGB-to-linear conversion when it is sampled.
                rgb = _rgb(PALETTE[swatch])
            o = (y * size + x) * 4
            pixels[o : o + 4] = [*rgb, 1.0]
    image.pixels[:] = pixels
    image.update()
    os.makedirs(os.path.join(KIT_DIR, "Textures"), exist_ok=True)
    image.filepath_raw = os.path.join(KIT_DIR, "Textures", "canyon_palette.png")
    image.file_format = "PNG"
    image.save()
    return image


def palette_material():
    material = bpy.data.materials.get(MATERIAL) or bpy.data.materials.new(MATERIAL)
    material.use_nodes = True
    nodes, links = material.node_tree.nodes, material.node_tree.links
    bsdf = next(n for n in nodes if n.type == "BSDF_PRINCIPLED")
    tex = next((n for n in nodes if n.type == "TEX_IMAGE"), None) or nodes.new("ShaderNodeTexImage")
    tex.image = palette_image()
    tex.interpolation = "Linear"  # flat swatches are sampled at their centres, gradients stay smooth
    tex.location = (-400, 200)
    links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    bsdf.inputs["Roughness"].default_value = 0.85
    return material


# ---------------------------------------------------------------- geometry helpers
def by_normal(top, side, bottom=None):
    """Colour rule: faces pointing up get `top`, everything else `side` (or `bottom`)."""

    def rule(face):
        if face.normal.z > 0.6:
            return top
        if bottom and face.normal.z < -0.6:
            return bottom
        return side

    return rule


class Piece:
    """Accumulates coloured parts into one bmesh."""

    def __init__(self, name):
        self.name = name
        self.bm = bmesh.new()
        self.uv = self.bm.loops.layers.uv.new("UVMap")
        self.sharp_angle = math.radians(40)
        self.gradient_range = (0.0, 1.0)  # heights mapped to the bottom/top of gradient swatches

    def add(self, part, color, cull_below=None, cull_bottom=False):
        """Merge bmesh `part` in. `color` is a swatch name or a face -> swatch rule.
        cull_bottom drops downward faces (resting on the ground / another part);
        cull_below drops faces entirely below that height (hidden under the floor)."""
        # Weld only within this part. Separate overlapping solids can share positions
        # without sharing a surface (e.g. a shoreline bank and its water slab).
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
        uv = part.loops.layers.uv.get("UVMap") or part.loops.layers.uv.new("UVMap")
        for face in part.faces:
            swatch = color(face) if callable(color) else color
            u, v = swatch_uv(swatch)
            if swatch in GRADIENTS:
                z0, z1 = self.gradient_range
                half = 0.5 / GRID * (SWATCH_PX - 1) / SWATCH_PX  # stay inside the swatch's pixel centres
                for loop in face.loops:
                    t = min(max((loop.vert.co.z - z0) / (z1 - z0), 0.0), 1.0)
                    loop[uv].uv = (u, v - half + 2 * half * t)
            else:
                for loop in face.loops:
                    loop[uv].uv = (u, v)
        mesh = bpy.data.meshes.new("_part")
        part.to_mesh(mesh)
        part.free()
        self.bm.from_mesh(mesh)
        bpy.data.meshes.remove(mesh)
        return self

    def build(self, location=(0, 0, 0), flat=False, materials=None):
        mesh = bpy.data.meshes.get(self.name)
        if mesh is None:
            mesh = bpy.data.meshes.new(self.name)
        # Parts already have outward winding. Recalculating after merging their open,
        # intersecting shells can reverse an entire bank and darken its top in-game.
        self.bm.normal_update()
        self.bm.to_mesh(mesh)
        self.bm.free()
        # The palette must be on TEXCOORD_0 in the GLB as well as in Blender.
        for layer in list(mesh.uv_layers):
            if layer.name != "UVMap":
                mesh.uv_layers.remove(layer)
        mesh.uv_layers.active_index = 0
        mesh.uv_layers[0].active_render = True
        material_indices = [poly.material_index for poly in mesh.polygons]
        mesh.materials.clear()
        for material in materials or [palette_material()]:
            mesh.materials.append(material)
        for poly, index in zip(mesh.polygons, material_indices):
            poly.material_index = index
        for poly in mesh.polygons:
            poly.use_smooth = not flat
        if not flat:
            mesh.set_sharp_from_angle(angle=self.sharp_angle)
        leaf_normals = mesh.attributes.get("_leaf_normal")
        if leaf_normals:
            mesh.normals_split_custom_set_from_vertices([tuple(d.vector.normalized()) for d in leaf_normals.data])
            mesh.attributes.remove(leaf_normals)
        obj = bpy.data.objects.get(self.name)
        if obj is None:
            obj = bpy.data.objects.new(self.name, mesh)
        obj.data = mesh
        collection = kit_collection()
        if obj.name not in collection.objects:
            for c in obj.users_collection:
                c.objects.unlink(obj)
            collection.objects.link(obj)
        obj.location = location
        return obj


def kit_collection():
    collection = bpy.data.collections.get(COLLECTION)
    if collection is None:
        collection = bpy.data.collections.new(COLLECTION)
        bpy.context.scene.collection.children.link(collection)
    return collection


def box(mn, mx, bevel=0.0, segments=1, edges="all"):
    """Axis-aligned box from corner mn to mx, optionally with bevelled edges.
    edges: "all", "top" (only edges touching the top face) or "vertical"."""
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    size = Vector(mx) - Vector(mn)
    centre = (Vector(mx) + Vector(mn)) / 2
    bmesh.ops.transform(bm, matrix=Matrix.Diagonal((*size, 1.0)), verts=bm.verts)
    bmesh.ops.translate(bm, vec=centre, verts=bm.verts)
    if bevel > 0:
        top = max(v.co.z for v in bm.verts)
        if edges == "top":
            chosen = [e for e in bm.edges if any(abs(v.co.z - top) < 1e-6 for v in e.verts)]
        elif edges == "vertical":
            chosen = [e for e in bm.edges if abs(e.verts[0].co.z - e.verts[1].co.z) > 1e-6]
        else:
            chosen = list(bm.edges)
        bmesh.ops.bevel(bm, geom=chosen, offset=bevel, segments=segments, affect="EDGES", profile=0.5, clamp_overlap=True)
    return bm


def prism(points, z0, z1, bevel=0.0, segments=1):
    """Extrude a 2D polygon (counter-clockwise list of (x, y)) from z0 to z1; bevel its top edges."""
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
    points = []
    for i in range(sides):
        a = phase + 2 * math.pi * i / sides
        r = radius * (1 + rng.uniform(-jitter, jitter))
        points.append((centre[0] + math.cos(a) * r * scale[0], centre[1] + math.sin(a) * r * scale[1]))
    return points


def blob(centre, radius, height, segments=8, rings=6):
    """Leaf/capsule blob: a UV sphere stretched to `height`, base at centre.z."""
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=segments, v_segments=rings, radius=1.0)
    bmesh.ops.transform(bm, matrix=Matrix.Diagonal((radius, radius, height / 2, 1.0)), verts=bm.verts)
    bmesh.ops.translate(bm, vec=Vector(centre) + Vector((0, 0, height / 2)), verts=bm.verts)
    return bm


def capsule(centre, radius, height, segments=8, rings=6, bulge=0.0):
    """Round-topped leaf capsule from centre.z up to centre.z + height (bottom half hidden in the ground).
    bulge > 0 makes the body swell towards its middle, like a leaf."""
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=segments, v_segments=rings, radius=radius)
    normals = bm.verts.layers.float_vector.new("_leaf_normal")
    for v in bm.verts:
        v[normals] = v.co.normalized()
        if v.co.z > 1e-6:
            v.co.z += height - 2 * radius
    if bulge:
        for v in bm.verts:
            t = min(max((v.co.z + radius) / height, 0.0), 1.0)
            k = 1.0 + bulge * math.sin(math.pi * t)
            v.co.x *= k
            v.co.y *= k
    bmesh.ops.translate(bm, vec=Vector(centre) + Vector((0, 0, radius)), verts=bm.verts)
    return bm


def cylinder(centre, radius, z0, z1, segments=10, bevel=0.0):
    return prism(ngon(segments, radius, centre), z0, z1, bevel=bevel, segments=2 if bevel else 1)


def rotated(bm, angle_z=0.0, angle_x=0.0, angle_y=0.0, pivot=(0, 0, 0)):
    m = Matrix.Rotation(angle_z, 4, "Z") @ Matrix.Rotation(angle_y, 4, "Y") @ Matrix.Rotation(angle_x, 4, "X")
    bmesh.ops.rotate(bm, verts=bm.verts, cent=Vector(pivot), matrix=m)
    return bm


def loft(rings):
    """Join equally sized counter-clockwise cross sections into one continuous shell."""
    bm = bmesh.new()
    vertices = [[bm.verts.new(co) for co in ring] for ring in rings]
    count = len(vertices[0])
    bm.faces.new(list(reversed(vertices[0])))
    bm.faces.new(vertices[-1])
    for lower, upper in zip(vertices, vertices[1:]):
        for i in range(count):
            j = (i + 1) % count
            bm.faces.new((lower[i], lower[j], upper[j], upper[i]))
    bm.normal_update()
    return bm


def tube_path(points, radius, sides=4):
    """Small low-poly rope/grain tube following a path, with capped ends."""
    points = [Vector(p) for p in points]
    rings = []
    for i, point in enumerate(points):
        tangent = (points[min(i + 1, len(points) - 1)] - points[max(0, i - 1)]).normalized()
        up = Vector((0, 0, 1)) if abs(tangent.z) < 0.9 else Vector((1, 0, 0))
        a = tangent.cross(up).normalized()
        b = tangent.cross(a).normalized()
        rings.append([point + radius * (a * math.cos(j * math.tau / sides) +
                                       b * math.sin(j * math.tau / sides)) for j in range(sides)])
    return loft(rings)


# ---------------------------------------------------------------- pieces
TILE_DEPTH = 0.2  # floor tiles hang 0.2 m below the walkable surface
WATER_LEVEL = -0.08


def cliff_block(name="cliff_block", height=1.0):
    """Continuous clay block with restrained strata, without separately bevelled stack gaps."""
    p = Piece(name)
    p.gradient_range = (0.0, height)
    profiles = [(0.0, 0.49), (0.025, 0.5)]
    for seam in ([0.36, 0.7] if height >= 0.75 else [0.25]):
        profiles.extend([(seam - 0.009, 0.5), (seam, 0.4965), (seam + 0.009, 0.5)])
    profiles.extend([(height - 0.045, 0.5), (height - 0.012, 0.49), (height, 0.47)])
    rings = []
    for z, half in profiles:
        cut = 0.035
        points = [(-half + cut, -half), (half - cut, -half), (half, -half + cut),
                  (half, half - cut), (half - cut, half), (-half + cut, half),
                  (-half, half - cut), (-half, -half + cut)]
        rings.append([(x, y, z) for x, y in points])
    p.add(loft(rings), by_normal("wall_top", "wall_side"), cull_bottom=True)
    # small square stud on top, as on the reference blocks
    p.add(box((-0.13, -0.05, height), (0.11, 0.17, height + 0.045), bevel=0.02, segments=1), by_normal("wall_top", "wall_side"), cull_bottom=True)
    return p.build()


# ---------------------------------------------------------------- continuous cover walls
WALL_HEIGHT = 1.0
WALL_LOW_HEIGHT = 0.5
WALL_CHAMFER = 0.035
DIRECTIONS = {"E": (1, 0), "N": (0, 1), "W": (-1, 0), "S": (0, -1)}


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
    # drop collinear vertices
    out = []
    n = len(loop)
    for k in range(n):
        a, b, c = Vector(loop[k - 1]), Vector(loop[k]), Vector(loop[(k + 1) % n])
        if abs((b - a).to_3d().cross((c - b).to_3d()).z) > 1e-9:
            out.append(loop[k])
    return out


def _wall_ring(outline, inset, chamfer, z, clamp):
    """Offset a rectilinear outline inward by `inset`, chamfer convex corners, clamp to the module."""
    n = len(outline)
    ring = []
    for k in range(n):
        prev, cur, nxt = Vector(outline[k - 1]), Vector(outline[k]), Vector(outline[(k + 1) % n])
        a, b = (cur - prev).normalized(), (nxt - cur).normalized()
        na, nb = Vector((-a.y, a.x)), Vector((-b.y, b.x))  # inward (left) normals of a CCW outline
        v = cur + (na + nb) * inset
        if a.x * b.y - a.y * b.x > 0:  # convex corner: chamfer
            points = [v - a * chamfer, v + b * chamfer]
        else:
            points = [v]
        for point in points:
            ring.append((min(max(point.x, -clamp), clamp), min(max(point.y, -clamp), clamp), z))
    return ring


def wall_module(name, open_sides="", height=WALL_HEIGHT):
    """One cell of continuous cover wall. `open_sides` (any of "ENWS") are flush joins to the next
    module, so walls of any length and shape assemble without seams, repeated folds or gaps:
    "" pillar, "E" end, "EW" straight, "EN" corner, "EWN" T-junction, "ENWS" cross.
    Light rounded top over brown sides that darken softly toward the base (contact shading)."""
    p = Piece(name)
    p.gradient_range = (0.0, height)
    p.sharp_angle = math.radians(50)
    cells = [(0, 0)] + [DIRECTIONS[d] for d in open_sides]
    outline = cell_outline(cells)
    # (height, inset) profile: soft foot, one shallow 3.5 mm stratum, rounded top edge.
    seam = 0.45 * height
    profile = [(0.0, 0.006), (0.03, 0.0), (seam - 0.01, 0.0), (seam, 0.0035), (seam + 0.01, 0.0),
               (height - 0.06, 0.0), (height - 0.022, 0.009), (height - 0.005, 0.026), (height, 0.04)]
    rings = [_wall_ring(outline, inset, WALL_CHAMFER, z, 0.5) for z, inset in profile]
    assert len({len(r) for r in rings}) == 1, "wall rings must match"
    bm = loft(rings)
    bmesh.ops.dissolve_degenerate(bm, edges=bm.edges, dist=1e-6)
    bm.normal_update()
    # Faces lying on an open side are hidden inside the joined wall.
    doomed = []
    for face in bm.faces:
        c = face.calc_center_median()
        for d in open_sides:
            dx, dy = DIRECTIONS[d]
            if abs(c.x * dx + c.y * dy - 0.5) < 1e-4 and (face.normal.x * dx + face.normal.y * dy) > 0.9:
                doomed.append(face)
                break
    bmesh.ops.delete(bm, geom=doomed, context="FACES")
    p.add(bm, by_normal("wall_top", "wall_side"), cull_bottom=True)
    return p.build()


WALL_MODULES = {"pillar": "", "end": "E", "straight": "EW", "corner": "EN", "t": "EWN", "cross": "ENWS"}


def _wall_builder(kind, low):
    def build():
        name = f"wall_{kind}" + ("_low" if low else "")
        return wall_module(name, WALL_MODULES[kind], WALL_LOW_HEIGHT if low else WALL_HEIGHT)

    build.__name__ = f"wall_{kind}" + ("_low" if low else "")
    return build


WALL_PIECES = [_wall_builder(kind, low) for low in (False, True) for kind in WALL_MODULES]


def wall_piece_for(cells, cell):
    """(module name suffix, quarter turns) for `cell` inside a set of wall cells.
    The base modules open toward E, EW, EN, EWN; rotate them to match the neighbours."""
    i, j = cell
    sides = "".join(d for d, (dx, dy) in DIRECTIONS.items() if (i + dx, j + dy) in cells)
    order = "ENWS"
    for kind, base in WALL_MODULES.items():
        if len(base) != len(sides):
            continue
        for turns in range(4):
            rotated_sides = {order[(order.index(c) + turns) % 4] for c in base}
            if rotated_sides == set(sides):
                return kind, turns
    raise ValueError(f"no wall module for {sides}")


def cliff_block_low():
    return cliff_block("cliff_block_low", height=0.5)


def rock_boulder():
    rng = random.Random(7)
    p = Piece("rock_boulder")
    p.sharp_angle = math.radians(25)
    base = ngon(7, 0.42, jitter=0.12, rng=rng, phase=0.3)
    p.add(prism(base, 0.0, 0.55, bevel=0.17, segments=1), by_normal("rock", "rock_dark"), cull_bottom=True)
    return p.build()


def rock_pebbles():
    rng = random.Random(3)
    p = Piece("rock_pebbles")
    p.sharp_angle = math.radians(25)
    for (x, y, r, h) in [(-0.1, 0.1, 0.21, 0.19), (0.24, -0.06, 0.14, 0.14), (-0.16, -0.25, 0.12, 0.11)]:
        shape = ngon(6, r, (x, y), jitter=0.15, rng=rng, phase=rng.uniform(0, 1))
        p.add(prism(shape, 0.0, h, bevel=r * 0.35, segments=1), by_normal("rock", "rock_dark"), cull_bottom=True)
    return p.build()


def _sand_base(p):
    p.add(box((-0.5, -0.5, -TILE_DEPTH), (0.5, 0.5, 0.0), bevel=0.03, segments=1, edges="top"), by_normal("sand", "sand_side"), cull_bottom=True)


def sand_tile():
    p = Piece("sand_tile")
    _sand_base(p)
    return p.build()


def sand_tile_cracked():
    p = Piece("sand_tile_cracked")
    _sand_base(p)
    # Shallow raised crack lines (2 mm) in a darker tone, branching like the reference.
    z = 0.002
    lines = [
        [(-0.32, 0.05), (-0.12, 0.0), (0.02, 0.08), (0.18, 0.02)],
        [(0.02, 0.08), (0.08, 0.26)],
        [(-0.12, 0.0), (-0.16, -0.18)],
        [(0.18, 0.02), (0.3, -0.08)],
    ]
    width = 0.022
    for line in lines:
        for (ax, ay), (bx, by) in zip(line, line[1:]):
            d = Vector((bx - ax, by - ay))
            n = Vector((-d.y, d.x)).normalized() * width / 2
            quad = [(ax - n.x, ay - n.y), (bx - n.x, by - n.y), (bx + n.x, by + n.y), (ax + n.x, ay + n.y)]
            # extend slightly past each end so joints overlap
            p.add(prism(quad, -0.005, z), "sand_side", cull_bottom=True)
    return p.build()


def sand_tile_pebbles():
    rng = random.Random(11)
    p = Piece("sand_tile_pebbles")
    _sand_base(p)
    for (x, y, r) in [(-0.12, 0.18, 0.06), (0.16, -0.08, 0.05)]:
        p.add(prism(ngon(6, r, (x, y), jitter=0.1, rng=rng), -0.01, 0.025, bevel=0.015), by_normal("rock", "rock_dark"), cull_bottom=True)
    return p.build()


def _water_base(p):
    p.add(box((-0.5, -0.5, -TILE_DEPTH), (0.5, 0.5, WATER_LEVEL)), by_normal("water", "water_side"), cull_bottom=True)


BANK_TOP = 0.03
BANK_WIDTH = 0.2


def _bank(p, points):
    p.add(prism(points, -TILE_DEPTH, BANK_TOP, bevel=0.035, segments=1), by_normal("bank", "bank_side"), cull_bottom=True)


def _arc(cx, cy, r, a0, a1, steps):
    return [(cx + r * math.cos(a0 + (a1 - a0) * i / steps), cy + r * math.sin(a0 + (a1 - a0) * i / steps)) for i in range(steps + 1)]


def water_tile():
    p = Piece("water_tile")
    _water_base(p)
    return p.build()


def water_edge():
    """Water with a sand bank along its +Y (north) edge; land goes on the north neighbour."""
    p = Piece("water_edge")
    _water_base(p)
    e = 0.5 - BANK_WIDTH
    _bank(p, [(-0.5, e), (0.5, e), (0.5, 0.5), (-0.5, 0.5)])
    return p.build()


def water_corner_outer():
    """Pond corner: bank along the north and east edges, curving round inside the tile."""
    p = Piece("water_corner_outer")
    _water_base(p)
    e = 0.5 - BANK_WIDTH
    r = 0.3
    inner = _arc(e - r, e - r, r, 0.0, math.pi / 2, 5)  # from (e, e-r) round to (e-r, e)
    pts = [(e, -0.5), (0.5, -0.5), (0.5, 0.5), (-0.5, 0.5), (-0.5, e)] + list(reversed(inner))
    # reversed(inner) runs (e-r, e) -> (e, e-r): counter-clockwise outline overall
    _bank(p, pts)
    return p.build()


def water_corner_inner():
    """Peninsula corner: water everywhere except a rounded bank patch in the north-east corner,
    which joins the banks of a north-facing edge tile to the west and an east-facing one to the south."""
    p = Piece("water_corner_inner")
    _water_base(p)
    e = 0.5 - BANK_WIDTH
    r = 0.12
    arc = _arc(e + r, e + r, r, math.pi, 1.5 * math.pi, 3)  # concave corner, (e, e+r) -> (e+r, e)
    _bank(p, [*arc, (0.5, e), (0.5, 0.5), (e, 0.5)][::1])
    return p.build()


def bush_1x1():
    """Dense tile of varied, overlapping oval leaves, following the grass GLB's layered silhouette."""
    rng = random.Random(5)
    p = Piece("bush_1x1")
    p.sharp_angle = math.radians(80)
    p.gradient_range = (0.0, 0.96)

    def leaf(x, y, radius, height, lean, heading):
        part = capsule((0, 0, -radius), radius, height + radius,
                       segments=8, rings=6, bulge=rng.uniform(0.08, 0.16))
        depth = rng.uniform(0.72, 0.92)
        bmesh.ops.transform(part, matrix=Matrix.Diagonal((1.0, depth, 1.0, 1.0)), verts=part.verts)
        rotated(part, angle_y=lean, angle_z=heading)
        normal_layer = part.verts.layers.float_vector.get("_leaf_normal")
        normal_rotation = Matrix.Rotation(heading, 3, "Z") @ Matrix.Rotation(lean, 3, "Y")
        for vertex in part.verts:
            normal = Vector(vertex[normal_layer])
            normal.y /= depth
            vertex[normal_layer] = (normal_rotation @ normal).normalized()
        bmesh.ops.translate(part, vec=Vector((x, y, 0)), verts=part.verts)
        # Preserve the authored one-metre footprint, even on the outward-leaning skirt.
        for axis in (0, 1):
            lo = min(v.co[axis] for v in part.verts)
            hi = max(v.co[axis] for v in part.verts)
            shift = min(0.0, 0.5 - hi) + max(0.0, -0.5 - lo)
            if shift:
                offset = Vector((0, 0, 0)); offset[axis] = shift
                bmesh.ops.translate(part, vec=offset, verts=part.verts)
        p.add(part, "leaf_grad", cull_below=0.0)

    heights = (0.81, 0.96, 0.73, 0.89, 0.77, 0.92, 0.75, 0.86, 0.94)
    for ix in range(3):
        for iy in range(3):
            x = (ix - 1) * 0.29 + rng.uniform(-0.045, 0.045)
            y = (iy - 1) * 0.29 + rng.uniform(-0.045, 0.045)
            leaf(x, y, rng.uniform(0.16, 0.20), heights[ix * 3 + iy],
                 math.radians(rng.uniform(4, 13)), rng.uniform(0, math.tau))
    # Unequal short leaves cover the roots and break the perimeter's repeated rhythm.
    skirt = [(t, -0.35) for t in (-0.17, 0.17)] + [(-0.34, -0.34)]
    for quarter in range(4):
        rot = Matrix.Rotation(quarter * math.pi / 2, 3, "Z")
        for sx, sy in skirt:
            pos = rot @ Vector((sx, sy, 0))
            leaf(pos.x + rng.uniform(-0.025, 0.025), pos.y + rng.uniform(-0.025, 0.025),
                 rng.uniform(0.11, 0.155), rng.uniform(0.39, 0.65),
                 math.radians(rng.uniform(12, 24)), math.atan2(pos.y, pos.x))
    return p.build()


def grass_tuft():
    rng = random.Random(2)
    p = Piece("grass_tuft")
    p.sharp_angle = math.radians(70)
    p.gradient_range = (0.0, 0.4)
    blades = 8
    for i in range(blades):
        a = 2 * math.pi * i / blades + rng.uniform(-0.2, 0.2)
        length = rng.uniform(0.3, 0.4)
        part = blob((0, 0, 0), 0.08, length, segments=6, rings=4)
        bmesh.ops.transform(part, matrix=Matrix.Diagonal((1.0, 0.5, 1.0, 1.0)), verts=part.verts)
        rotated(part, angle_y=math.radians(rng.uniform(35, 55)))
        rotated(part, angle_z=a)
        p.add(part, "leaf_grad", cull_below=0.0)
    p.add(blob((0, 0, -0.1), 0.08, 0.32, segments=6, rings=4), "leaf_grad", cull_below=0.0)
    return p.build()


# ---------------------------------------------------------------- foliage
GRASS_HEIGHT = 0.95  # top of the concealment canopy; the 1 m sensor box per cell stays fixed


def _blade(base, heading, lean, length, width, thickness=0.02, curl=0.12, normals_up=1.0):
    """One tapered, gently curved blade: a lens cross-section lofted along an arc that leans
    toward `heading`. Normals lean mostly upward so a patch shades as one coherent canopy."""
    out = Vector((math.cos(heading), math.sin(heading), 0.0))
    side = Vector((-out.y, out.x, 0.0))
    rings = []
    for t, w in ((0.0, 0.8), (0.42, 1.0), (0.8, 0.62), (1.0, 0.04)):
        a = lean * (0.55 + 0.45 * t) + curl * t * t
        centre = Vector(base) + out * (math.sin(a) * length * t) + Vector((0, 0, math.cos(a) * length * t))
        half_w, half_t = width * w / 2, thickness * max(w, 0.2) / 2
        rings.append([tuple(v) for v in (centre + side * half_w, centre + out * half_t, centre - side * half_w, centre - out * half_t)])
    bm = loft(rings)
    normals = bm.verts.layers.float_vector.new("_leaf_normal")
    for v in bm.verts:
        v[normals] = (Vector((0, 0, normals_up)) + out * 0.45).normalized()
    return bm


def _canopy_body(open_sides, height):
    """Soft rounded clump mass under the blades (no soil gaps), flush on open sides like walls."""
    cells = [(0, 0)] + [DIRECTIONS[d] for d in open_sides]
    outline = cell_outline(cells)
    profile = [(0.0, 0.1), (0.25 * height, 0.08), (0.6 * height, 0.1), (0.85 * height, 0.16), (height, 0.27)]
    rings = [_wall_ring(outline, inset, 0.12, z, 0.5) for z, inset in profile]
    bm = loft(rings)
    bmesh.ops.dissolve_degenerate(bm, edges=bm.edges, dist=1e-6)
    doomed = []
    bm.normal_update()
    for face in bm.faces:
        c = face.calc_center_median()
        for d in open_sides:
            dx, dy = DIRECTIONS[d]
            if abs(c.x * dx + c.y * dy - 0.5) < 1e-4 and (face.normal.x * dx + face.normal.y * dy) > 0.9:
                doomed.append(face)
                break
    bmesh.ops.delete(bm, geom=doomed, context="FACES")
    normals = bm.verts.layers.float_vector.new("_leaf_normal")
    for v in bm.verts:
        v[normals] = (Vector((0, 0, 1.0)) + Vector((v.co.x, v.co.y, 0)) * 0.6).normalized()
    return bm


def grass_module(name, open_sides="", seed=1):
    """Gameplay concealment grass for one cell: a rounded canopy mass with clumps of overlapping,
    slightly narrow tapered blades rising out of it, so the patch is full from above but keeps a
    broken leaf silhouette. A lower skirt is added only on closed (exposed) sides, so strips,
    corners, L-shapes and blocks read as one patch with no interior borders. Assemble with
    wall_piece_for() exactly like the wall modules; the concealment zone is always the cell."""
    rng = random.Random(seed * 7919 + sum(ord(c) for c in open_sides))
    p = Piece(name)
    p.sharp_angle = math.radians(89)
    p.gradient_range = (0.0, GRASS_HEIGHT)
    closed = [d for d in DIRECTIONS if d not in open_sides]
    p.add(_canopy_body(open_sides, 0.62), "leaf_grad", cull_bottom=True)
    # Canopy: clumps on a jittered grid, larger upper blades with varied height, lean and width.
    for cx, cy in ((-0.25, -0.25), (0.25, -0.25), (-0.25, 0.25), (0.25, 0.25), (0.0, 0.0)):
        cx += rng.uniform(-0.05, 0.05)
        cy += rng.uniform(-0.05, 0.05)
        clump_height = rng.uniform(0.8, 0.96)
        for k in range(rng.randint(5, 7)):
            heading = rng.uniform(0, math.tau)
            base = (cx + rng.uniform(-0.05, 0.05), cy + rng.uniform(-0.05, 0.05), 0.25)
            lean = math.radians(rng.uniform(8, 32))
            length = (clump_height - 0.25) * rng.uniform(0.9, 1.08) / math.cos(lean * 0.8)
            p.add(_blade(base, heading, lean, length, rng.uniform(0.13, 0.17)), "leaf_grad", cull_below=0.0)
    # Skirt on exposed sides: shorter blades leaning outward over the body's base.
    for d in closed:
        dx, dy = DIRECTIONS[d]
        edge = Vector((dx, dy, 0))
        along = Vector((-dy, dx, 0))
        for k in range(rng.randint(4, 5)):
            t = -0.38 + k * 0.19 + rng.uniform(-0.07, 0.07)
            base = along * t + edge * rng.uniform(0.22, 0.3)
            heading = math.atan2(dy, dx) + rng.uniform(-0.75, 0.75)
            p.add(_blade(tuple(base), heading, math.radians(rng.uniform(20, 38)), rng.uniform(0.4, 0.58),
                         rng.uniform(0.13, 0.18), normals_up=0.8), "leaf_grad", cull_below=0.0)
    return p.build()


def _grass_builder(kind):
    def build():
        return grass_module(f"grass_{kind}", WALL_MODULES[kind], seed=3)

    build.__name__ = f"grass_{kind}"
    return build


def outskirts_shrub(name="shrub_a", seed=1, size=1.0):
    """Decorative outskirts plant: broader oval leaves in a few large and small clusters, related
    to the concealment grass in palette and shading but clearly not a hiding zone."""
    rng = random.Random(seed)
    p = Piece(name)
    p.sharp_angle = math.radians(80)
    p.gradient_range = (0.0, 0.85 * size)
    clusters = [(0.0, 0.0, 1.0)] + [
        (math.cos(a) * rng.uniform(0.28, 0.42) * size, math.sin(a) * rng.uniform(0.28, 0.42) * size, rng.uniform(0.5, 0.75))
        for a in (rng.uniform(0, math.tau) + i * math.tau / 3 for i in range(rng.randint(2, 3)))
    ]
    for cx, cy, scale in clusters:
        for k in range(int(5 + 3 * scale)):
            heading = rng.uniform(0, math.tau)
            radius = rng.uniform(0.09, 0.14) * size * (0.7 + 0.3 * scale)
            height = rng.uniform(0.45, 0.8) * size * scale
            lean = math.radians(rng.uniform(12, 38))
            leaf = capsule((0, 0, -radius), radius, height + radius, segments=7, rings=5, bulge=rng.uniform(0.15, 0.3))
            depth = rng.uniform(0.45, 0.6)  # broad, flattened leaves
            bmesh.ops.transform(leaf, matrix=Matrix.Diagonal((1.0, depth, 1.0, 1.0)), verts=leaf.verts)
            rotated(leaf, angle_y=lean, angle_z=heading)
            layer = leaf.verts.layers.float_vector.get("_leaf_normal")
            out = Vector((math.cos(heading), math.sin(heading), 0))
            for v in leaf.verts:
                v[layer] = (Vector((0, 0, 1)) + out * 0.5 + Vector(v[layer]) * 0.3).normalized()
            bmesh.ops.translate(leaf, vec=Vector((cx, cy, 0)), verts=leaf.verts)
            p.add(leaf, "shrub_grad", cull_below=0.0)
    return p.build()


GRASS_PIECES = [_grass_builder(kind) for kind in WALL_MODULES]
SHRUB_PIECES = []
for _i, (_seed, _size) in enumerate(((11, 1.0), (23, 1.25), (37, 0.8))):
    def _shrub(seed=_seed, size=_size, name="shrub_" + "abc"[_i]):
        return outskirts_shrub(name, seed, size)

    _shrub.__name__ = "shrub_" + "abc"[_i]
    SHRUB_PIECES.append(_shrub)


CRATE = 0.9  # dynamic props are a little under a cell so they don't snag on neighbours


def crate():
    p = Piece("crate")
    h = CRATE / 2
    frame = 0.1
    # Outer shell (light wood) with each face inset into a recessed dark panel.
    bm = box((-h, -h, 0.0), (h, h, CRATE), bevel=0.03, segments=1)
    big = [f for f in bm.faces if len(f.verts) == 4 and f.calc_area() > 0.5]
    bmesh.ops.inset_individual(bm, faces=big, thickness=frame, depth=-0.035)
    panels = set(big)  # inset keeps the original faces as the recessed centres
    for f in bm.faces:
        f.tag = f in panels
    p.add(bm, lambda f: "wood_panel" if f.tag else ("wood_top" if f.normal.z > 0.6 else "wood"))
    # Plank seams: thin dark strips on the four side panels.
    inner = h - frame
    for i in range(4):
        for x in (-inner / 3, inner / 3):
            seam = box((x - 0.008, -h + 0.033, frame), (x + 0.008, -h + 0.037, CRATE - frame))
            rotated(seam, angle_z=math.pi / 2 * i)
            p.add(seam, "wood_dark")
    # Diagonal brace on the four sides.
    length = math.hypot(CRATE - 2 * frame, CRATE - 2 * frame)
    for i in range(4):
        brace = box((-length / 2, -0.012, -0.055), (length / 2, 0.025, 0.055), bevel=0.012)
        rotated(brace, angle_y=math.radians(45 if i % 2 else -45))
        bmesh.ops.translate(brace, vec=Vector((0, -h + 0.02, CRATE / 2)), verts=brace.verts)
        rotated(brace, angle_z=math.pi / 2 * i)
        p.add(brace, "wood")
    return p.build()


def crate_debris():
    rng = random.Random(9)
    p = Piece("crate_debris")
    planks = [(-0.15, 0.15, 0.62, 0.0, 25), (0.22, 0.05, 0.4, 0.0, -50), (0.15, -0.25, 0.25, 0.0, 10), (-0.25, -0.18, 0.16, 0.0, 70)]
    for x, y, length, _, yaw in planks:
        plank = box((-length / 2, -0.07, 0.0), (length / 2, 0.07, 0.07), bevel=0.012)
        rotated(plank, angle_z=math.radians(yaw), angle_x=math.radians(rng.uniform(-4, 4)))
        bmesh.ops.translate(plank, vec=Vector((x, y, 0.0)), verts=plank.verts)
        p.add(plank, by_normal("wood", "wood_dark"), cull_bottom=True)
    return p.build()


POST_HEIGHT = 0.72
RAIL_LEVELS = (0.24, 0.5)


def fence_post():
    p = Piece("fence_post")
    profiles = [(0.0, 0.125), (0.025, 0.146), (0.10, 0.151), (0.28, 0.154),
                (0.48, 0.147), (0.65, 0.156), (0.70, 0.149), (POST_HEIGHT, 0.128)]
    rings = [[(math.cos(i * math.tau / 12) * r, math.sin(i * math.tau / 12) * r, z)
              for i in range(12)] for z, r in profiles]
    p.sharp_angle = math.radians(65)
    p.gradient_range = (0.0, POST_HEIGHT)
    p.add(loft(rings), by_normal("wood_cap", "wood"), cull_bottom=True)
    # Real rounded rope turns, rather than a flat sleeve around the post.
    rope = []
    for i in range(33):
        t = i / 32
        angle = t * math.tau * 1.85
        rope.append((0.169 * math.cos(angle), 0.169 * math.sin(angle), 0.327 + t * 0.058))
    p.add(tube_path(rope, 0.018, sides=4), "rope")
    # Restrained end-grain on a rounded wooden cap; no dark filled disk.
    cap_ring = [(0.083 * math.cos(i * math.tau / 12),
                 0.083 * math.sin(i * math.tau / 12), POST_HEIGHT + 0.001) for i in range(13)]
    p.add(tube_path(cap_ring, 0.0035, sides=3), "wood_panel")
    return p.build()


def fence_rail():
    """Two rails from this cell's centre to the next cell's centre along +X (they tuck into the
    posts at both ends). Place fence_post on every fence cell and a rail between each pair;
    rotate the rail 90/180/270 degrees to run in the other directions (corners need no extra piece)."""
    p = Piece("fence_rail")
    p.sharp_angle = math.radians(65)
    for i, z in enumerate(RAIL_LEVELS):
        rail = box((0.0, -0.067, z - 0.073), (1.0, 0.067, z + 0.073),
                   bevel=0.026, segments=2, edges="all")
        for v in rail.verts:
            t = v.co.x
            v.co.z += math.sin(t * math.pi) * (0.009 if i else -0.006)
            v.co.y += math.sin(t * math.pi) * (0.008 if i else -0.006)
        p.add(rail, by_normal("wood", "wood", "wood_panel"))
        grain = [(x, -0.069 + math.sin(x * math.pi) * (0.008 if i else -0.006),
                  z - 0.01 + math.sin(x * 5 + i) * 0.008) for x in (0.17, 0.34, 0.55, 0.77, 0.84)]
        p.add(tube_path(grain, 0.0025), "wood_panel")
    return p.build()


PIECES = [
    cliff_block,
    cliff_block_low,
    rock_boulder,
    rock_pebbles,
    sand_tile,
    sand_tile_cracked,
    sand_tile_pebbles,
    water_tile,
    water_edge,
    water_corner_outer,
    water_corner_inner,
    bush_1x1,
    grass_tuft,
    crate,
    crate_debris,
    fence_post,
    fence_rail,
    *WALL_PIECES,
    *GRASS_PIECES,
    *SHRUB_PIECES,
]


def build_all(spacing=1.6):
    objects = []
    for i, builder in enumerate(PIECES):
        obj = builder()
        obj.location = ((i % 6) * spacing, -(i // 6) * spacing, 0)
        objects.append(obj)
    return objects


def stats():
    out = []
    for builder in PIECES:
        obj = bpy.data.objects.get(builder.__name__)
        if obj:
            tris = sum(len(p.vertices) - 2 for p in obj.data.polygons)
            out.append((obj.name, tris))
    return out


def render_preview(path=None):
    """Renders the sample layout to preview.png next to the kit."""
    scene = bpy.context.scene
    bpy.context.view_layer.update()
    objs = list(bpy.data.collections[SAMPLE].objects)
    pts = [o.matrix_world @ Vector(c) for o in objs for c in o.bound_box]
    lo = Vector([min(p[i] for p in pts) for i in range(3)])
    hi = Vector([max(p[i] for p in pts) for i in range(3)])
    centre, radius = (lo + hi) / 2, (hi - lo).length / 2
    cam_data = bpy.data.cameras.get("KitPreviewCam") or bpy.data.cameras.new("KitPreviewCam")
    cam = bpy.data.objects.get("KitPreviewCam") or bpy.data.objects.new("KitPreviewCam", cam_data)
    if cam.name not in scene.collection.objects:
        scene.collection.objects.link(cam)
    cam_data.type = "ORTHO"
    cam_data.clip_end = 500
    # The benchmark gameplay pitch (docs/artdirection.md section 1), not a studio angle.
    pitch = math.radians(57)
    direction = Vector((0.0, -math.cos(pitch), math.sin(pitch)))
    cam.location = centre + direction * (radius * 4 + 5)
    cam.rotation_euler = (-direction).to_track_quat("-Z", "Y").to_euler()
    camera_axes = cam.rotation_euler.to_matrix().transposed()
    framed = [camera_axes @ (point - centre) for point in pts]
    width = max(p.x for p in framed) - min(p.x for p in framed)
    height = max(p.y for p in framed) - min(p.y for p in framed)
    cam_data.ortho_scale = max(width, height * 1400 / 800) * 1.12
    hidden = [(o, o.hide_render) for o in bpy.data.collections[COLLECTION].objects]
    for o, _ in hidden:
        o.hide_render = True
    old_camera = scene.camera
    scene.camera = cam
    scene.render.resolution_x, scene.render.resolution_y = 1400, 800
    scene.render.filepath = path or os.path.join(KIT_DIR, "preview.png")
    try:
        bpy.ops.render.render(write_still=True)
    finally:
        scene.camera = old_camera
        for o, hide_render in hidden:
            o.hide_render = hide_render


def render_details(path=None):
    """Close view of the wall, varied bush, complete fence and light-blue water modules."""
    scene = bpy.context.scene
    collection = bpy.data.collections.new("_CanyonDetails")
    scene.collection.children.link(collection)
    copies = []

    def put(name, location, angle=0):
        obj = bpy.data.objects.new("_Detail_" + name, bpy.data.objects[name].data)
        obj.location = location
        obj.rotation_euler.z = angle
        collection.objects.link(obj)
        copies.append(obj)

    put("cliff_block", (0, 1.8, 0))
    put("cliff_block_low", (1.1, 1.8, 0))
    put("bush_1x1", (3.3, 1.8, 0))
    put("bush_1x1", (4.3, 1.8, 0), math.pi / 2)
    put("fence_post", (0, -0.2, 0))
    put("fence_post", (1, -0.2, 0))
    put("fence_rail", (0, -0.2, 0))
    for x in (3.1, 4.1):
        for y in (-1.1, -0.1):
            put("water_tile", (x, y, 0))
    put("water_edge", (3.1, -0.1, 0))
    put("water_corner_outer", (4.1, -0.1, 0))
    camera_data = bpy.data.cameras.new("_DetailCamera")
    camera = bpy.data.objects.new("_DetailCamera", camera_data)
    collection.objects.link(camera)
    centre = Vector((2.05, 0.55, 0.3))
    camera.location = centre + Vector((0.5, -7, 10))
    camera.rotation_euler = (centre - camera.location).to_track_quat("-Z", "Y").to_euler()
    camera_data.type = "ORTHO"
    camera_data.ortho_scale = 6.8
    hidden = [(obj, obj.hide_render) for obj in scene.objects if obj.type == "MESH" and obj not in copies]
    old_camera = scene.camera
    old_render = (scene.render.resolution_x, scene.render.resolution_y, scene.render.resolution_percentage, scene.render.filepath)
    try:
        for obj, _ in hidden:
            obj.hide_render = True
        scene.camera = camera
        scene.render.resolution_x, scene.render.resolution_y = 1600, 1100
        scene.render.resolution_percentage = 100
        scene.render.filepath = path or os.path.join(KIT_DIR, "details.png")
        bpy.ops.render.render(write_still=True)
    finally:
        scene.camera = old_camera
        scene.render.resolution_x, scene.render.resolution_y, scene.render.resolution_percentage, scene.render.filepath = old_render
        for obj, hidden_before in hidden:
            obj.hide_render = hidden_before
        for obj in list(collection.objects):
            bpy.data.objects.remove(obj, do_unlink=True)
        bpy.data.collections.remove(collection)
        bpy.data.cameras.remove(camera_data)


def export_all():
    models = os.path.join(KIT_DIR, "Models")
    os.makedirs(models, exist_ok=True)
    for o in bpy.context.selected_objects:
        o.select_set(False)
    for builder in PIECES:
        obj = bpy.data.objects[builder.__name__]
        saved = obj.location.copy()
        obj.location = (0, 0, 0)
        obj.select_set(True)
        bpy.context.view_layer.objects.active = obj
        bpy.ops.export_scene.gltf(
            filepath=os.path.join(models, obj.name + ".glb"),
            use_selection=True,
            export_yup=True,
            export_apply=True,
            export_cameras=False,
            export_lights=False,
            export_materials="EXPORT",
            export_image_format="AUTO",
        )
        obj.select_set(False)
        obj.location = saved


# ---------------------------------------------------------------- sample layout (snap test, not exported)
SAMPLE = "CanyonSample"


def build_sample(origin=(0.0, -15.0)):
    """An 8x8 corner of an arena built only from kit pieces, with linked copies of the meshes."""
    collection = bpy.data.collections.get(SAMPLE)
    if collection is None:
        collection = bpy.data.collections.new(SAMPLE)
        bpy.context.scene.collection.children.link(collection)
    for obj in list(collection.objects):
        bpy.data.objects.remove(obj)

    def put(piece, x, y, quarter_turns=0):
        obj = bpy.data.objects.new(f"{piece}_{x}_{y}", bpy.data.objects[piece].data)
        obj.location = (origin[0] + x + 0.5, origin[1] + y + 0.5, 0.0)
        obj.rotation_euler = (0, 0, quarter_turns * math.pi / 2)
        collection.objects.link(obj)

    pond = {
        (2, 2): ("water_corner_outer", 1), (3, 2): ("water_edge", 0), (4, 2): ("water_corner_outer", 0),
        (2, 1): ("water_corner_outer", 2), (3, 1): ("water_edge", 2), (4, 1): ("water_corner_outer", 3),
    }
    walls = {(x, 7) for x in range(8)} | {(0, y) for y in range(4, 7)} | {(7, 6)}
    low = {(1, 4), (2, 4)}
    rng = random.Random(4)
    for x in range(8):
        for y in range(8):
            if (x, y) in pond:
                put(*pond[(x, y)][:1], x, y, pond[(x, y)][1])
                continue
            r = rng.random()
            put("sand_tile_cracked" if r < 0.1 else "sand_tile_pebbles" if r < 0.18 else "sand_tile", x, y, rng.randrange(4))
            if (x, y) in walls:
                kind, turns = wall_piece_for(walls, (x, y))
                put("wall_" + kind, x, y, turns)
            elif (x, y) in low:
                kind, turns = wall_piece_for(low, (x, y))
                put("wall_" + kind + "_low", x, y, turns)
    grass = {(5, 4), (6, 4), (6, 5), (6, 3)}  # joined concealment patch
    for cell in grass:
        kind, turns = wall_piece_for(grass, cell)
        put("grass_" + kind, *cell, turns)
    for x in range(1, 5):  # fence: posts on each cell, rails linking neighbours, turning a corner
        put("fence_post", x, 0)
    put("fence_post", 1, 1)
    for x in range(1, 4):
        put("fence_rail", x, 0)
    put("fence_rail", 1, 0, 1)
    put("crate", 3, 5)
    put("crate", 4, 5)
    put("crate_debris", 2, 6)
    put("rock_boulder", 5, 6)
    put("rock_pebbles", 1, 2)
    put("grass_tuft", 5, 1)
    put("grass_tuft", 6, 1)

    # L-shaped pond (3x3 minus its north-east cell) to test the inner corner, next to the arena.
    l_pond = {
        (0, 0): ("water_corner_outer", 2), (1, 0): ("water_edge", 2), (2, 0): ("water_corner_outer", 3),
        (0, 1): ("water_edge", 1), (1, 1): ("water_corner_inner", 0), (2, 1): ("water_corner_outer", 0),
        (0, 2): ("water_corner_outer", 1), (1, 2): ("water_corner_outer", 0),
    }
    for x in range(9, 14):
        for y in range(0, 5):
            cell = (x - 10, y - 1)
            if cell in l_pond:
                put(l_pond[cell][0], x, y, l_pond[cell][1])
            else:
                put("sand_tile", x, y)
    return collection
