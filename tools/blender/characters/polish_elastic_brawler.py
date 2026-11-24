"""Polish pass for the assembled elastic brawler (elastic-brawler-polished.blend).

Fixes garment fit (sandals, sash/jacket, buttons), the hairline texture, leaf-bone
length and skin tone against output/characters/elastic-brawler/character-sheet.png.

Run headless on the pre-polish backup, e.g.
  flatpak run org.blender.Blender -b <model>/elastic-brawler-polished.pre-polish.blend \
      --python tools/blender/characters/polish_elastic_brawler.py -- all

Every step is guarded by a custom property, so running it twice is harmless. Coordinates
are the mesh's local space (1.0 = 1.78 m after the root scale), so clearances are small.
"""
import json
import math
import sys
from pathlib import Path

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree
from mathutils.kdtree import KDTree

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'output/characters/elastic-brawler/model'
SHEET = ROOT / 'output/characters/elastic-brawler/character-sheet.png'
SKIN = (221 / 255, 160 / 255, 115 / 255)        # sheet "skin (base)" #DDA073


def obj():
    return bpy.data.objects['ElasticBrawler_Mesh']


def rig():
    return bpy.data.objects['ElasticBrawler_Rig']


def once(key):
    o = obj()
    if o.get(key):
        print('skip', key)
        return False
    o[key] = 1
    return True


def parts(o):
    names = {g.index: g.name for g in o.vertex_groups if g.name.startswith('Part_')}
    out = []
    for v in o.data.vertices:
        out.append(next((names[g.group] for g in v.groups if g.group in names and g.weight > .5), None))
    return out


def components(mesh, subset):
    subset = set(subset)
    adjacency = {i: [] for i in subset}
    for e in mesh.edges:
        a, b = e.vertices
        if a in subset and b in subset:
            adjacency[a].append(b)
            adjacency[b].append(a)
    seen, result = set(), []
    for start in subset:
        if start in seen:
            continue
        group, todo = set(), [start]
        while todo:
            i = todo.pop()
            if i in group:
                continue
            group.add(i)
            todo.extend(adjacency[i])
        seen |= group
        result.append(sorted(group))
    return sorted(result, key=len, reverse=True)


def bvh(mesh, mask):
    mesh.calc_loop_triangles()
    tris = [tuple(t.vertices) for t in mesh.loop_triangles if all(mask[i] for i in t.vertices)]
    return BVHTree.FromPolygons([v.co for v in mesh.vertices], tris, all_triangles=True)


def push_out(mesh, movers, obstacle, clearance, radius=.012, pull=False, reach=.05, rounds=3):
    """Move `movers` along the obstacle normal until their whole column clears it.

    Each mover takes the deepest signed distance of the mover vertices whose surface
    projection lies within `radius`, so a garment keeps its thickness instead of being
    flattened. Offsets are dilated with a linear falloff so the moved area blends in.
    With pull=True the garment is also drawn down onto the obstacle (snug fit).
    """
    moved = 0
    for _ in range(rounds):
        tree = bvh(mesh, obstacle)
        hits = {}
        for i in movers:
            loc, normal, _, dist = tree.find_nearest(mesh.vertices[i].co, reach)
            if loc is not None:
                hits[i] = (loc, normal, (mesh.vertices[i].co - loc).dot(normal))
        if not hits:
            return moved
        kd = KDTree(len(hits))
        keys = list(hits)
        for n, i in enumerate(keys):
            kd.insert(hits[i][0], n)
        kd.balance()
        need = {}
        for i in keys:
            deepest = min(hits[keys[n]][2] for _, n, _ in kd.find_range(hits[i][0], radius))
            offset = clearance - deepest
            need[i] = offset if pull else max(0.0, offset)
        smooth = {}
        for i in keys:
            best = 0.0
            for _, n, d in kd.find_range(hits[i][0], radius * 2):
                value = need[keys[n]] * (1 - d / (radius * 2))
                if abs(value) > abs(best):
                    best = value
            smooth[i] = best
        changed = 0
        for i in keys:
            if abs(smooth[i]) > 1e-6:
                mesh.vertices[i].co += hits[i][1] * smooth[i]
                changed += 1
        moved = max(moved, changed)
        mesh.update()
        if not changed:
            break
    return moved


def relax(mesh, subset, iterations=4, factor=.5):
    """Laplacian smoothing restricted to `subset`, to iron out ripples from fitting."""
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bm.verts.ensure_lookup_table()
    verts = [bm.verts[i] for i in subset]
    for _ in range(iterations):
        bmesh.ops.smooth_vert(bm, verts=verts, factor=factor, use_axis_x=True, use_axis_y=True, use_axis_z=True)
    bm.to_mesh(mesh)
    bm.free()
    mesh.update()


def copy_weights(o, targets, donors):
    """Give each target vertex the bone weights of its nearest donor vertex."""
    mesh = o.data
    bones = {b.name for b in rig().data.bones}
    kd = KDTree(len(donors))
    for n, i in enumerate(donors):
        kd.insert(mesh.vertices[i].co, n)
    kd.balance()
    groups = {g.index: g for g in o.vertex_groups if g.name in bones}
    donor_weights = {}
    for i in donors:
        donor_weights[i] = [(g.group, g.weight) for g in mesh.vertices[i].groups if g.group in groups]
    for i in targets:
        _, n, _ = kd.find(mesh.vertices[i].co)
        for g in list(mesh.vertices[i].groups):
            if g.group in groups:
                groups[g.group].remove([i])
        for group, weight in donor_weights[donors[n]]:
            groups[group].add([i], weight, 'REPLACE')


def overlap(mesh, movers, obstacle, clearance=0.0):
    """Number of mover vertices inside the obstacle surface (signed distance < clearance)."""
    tree = bvh(mesh, obstacle)
    count = 0
    for i in movers:
        loc, normal, _, _ = tree.find_nearest(mesh.vertices[i].co, .02)
        if loc is not None and (mesh.vertices[i].co - loc).dot(normal) < clearance:
            count += 1
    return count


def masks(o):
    p = parts(o)
    return p, {name: [x == name for x in p] for name in set(p) if name}


# --------------------------------------------------------------------------- sandals
def fit_sandals():
    if not once('polish_sandals'):
        return
    o = obj()
    mesh = o.data
    p, m = masks(o)
    report = {}
    for side in (1, -1):
        foot = [i for i, v in enumerate(mesh.vertices) if p[i] == 'Part_Body' and v.co.z < .09 and v.co.x * side > 0]
        sandal = [i for i, v in enumerate(mesh.vertices) if p[i] == 'Part_Sandals' and v.co.x * side > 0]
        comps = components(mesh, sandal)
        strap = next(c for c in comps if max(mesh.vertices[i].co.z for i in c) > .05)
        base = [i for c in comps if c is not strap for i in c]
        fx = [mesh.vertices[i].co.x for i in foot]
        fy = [mesh.vertices[i].co.y for i in foot]
        sx = [mesh.vertices[i].co.x for i in base]
        sy = [mesh.vertices[i].co.y for i in base]
        margin = .007
        cx, hx = (min(fx) + max(fx)) / 2, (max(fx) - min(fx)) / 2 + margin
        cy, hy = (min(fy) + max(fy)) / 2 - .004, (max(fy) - min(fy)) / 2 + margin
        ox, kx = (min(sx) + max(sx)) / 2, (max(sx) - min(sx)) / 2
        oy, ky = (min(sy) + max(sy)) / 2, (max(sy) - min(sy)) / 2
        for i in base:
            v = mesh.vertices[i].co
            v.x = cx + (v.x - ox) * hx / kx
            v.y = cy + (v.y - oy) * hy / ky
            v.z *= .5
        # Strap: wider band across the forefoot, then shrink-wrapped onto the skin.
        centre = sum((mesh.vertices[i].co for i in strap), Vector()) / len(strap)
        for i in strap:
            v = mesh.vertices[i].co
            v.y = centre.y + (v.y - centre.y) * 1.35 + .004
            v.x = cx + (v.x - ox) * hx / kx
            v.z *= .5 if v.z < .02 else 1
        mesh.update()
        skin = [x == 'Part_Body' and mesh.vertices[i].co.z < .12 for i, x in enumerate(p)]
        report[side] = push_out(mesh, strap, skin, .0012, radius=.01, pull=True, reach=.04)
        relax(mesh, strap, 6)
        push_out(mesh, strap, skin, .0012, radius=.01, reach=.04)
        # Strap ends meet the sole instead of hanging in the air.
        top = max(mesh.vertices[i].co.z for i in base)
        for i in strap:
            v = mesh.vertices[i].co
            if v.z < top + .006:
                v.z = max(v.z, top - .002)
        # Sole sits under the foot: lift nothing, lower the insole beneath the skin.
        under = bvh(mesh, skin)
        for i in base:
            v = mesh.vertices[i].co
            hit, *_ = under.ray_cast(Vector((v.x, v.y, .2)), Vector((0, 0, -1)), .3)
            if hit is not None and v.z > hit.z - .0008 and v.z > .004:
                v.z = max(.004, hit.z - .0008)
        mesh.update()
    print('sandals fitted', report)


# --------------------------------------------------------------------------- sash
def fit_sash():
    if not once('polish_sash'):
        return
    o = obj()
    mesh = o.data
    p, m = masks(o)
    belt = [i for i, x in enumerate(p) if x == 'Part_Belt']
    comps = components(mesh, belt)
    band, knot, tail_a, tail_b = comps[:4]
    jacket = [i for i, x in enumerate(p) if x == 'Part_Jacket']
    band_mask = [False] * len(p)
    for i in band:
        band_mask[i] = True
    # 1. jacket lifted clear of the band (the sash is worn under the open jacket)
    near = [i for i in jacket if .40 < mesh.vertices[i].co.z < .56]
    lifted = push_out(mesh, near, band_mask, .0015, radius=.014, reach=.03)
    # 2. knot and tails sit in front of the jacket: the smallest rigid forward shift
    #    that puts every jacket vertex behind them, found by ray casts along +Y.
    shifts = {}
    for name, group in (('knot', knot), ('tail_a', tail_a), ('tail_b', tail_b)):
        mask = [False] * len(p)
        for i in group:
            mask[i] = True
        tree = bvh(mesh, mask)
        xs = [mesh.vertices[i].co.x for i in group]
        zs = [mesh.vertices[i].co.z for i in group]
        need = 0.0
        for i in jacket:
            co = mesh.vertices[i].co
            if mesh.vertices[i].normal.y > -.5:  # only the forward-facing front panel
                continue
            if not (min(xs) - .01 < co.x < max(xs) + .01 and min(zs) - .01 < co.z < max(zs) + .01):
                continue
            hit, normal, _, _ = tree.ray_cast(co, Vector((0, 1, 0)), .08)
            if hit is not None and normal.y > 0:  # the jacket vertex is inside the sash
                need = max(need, hit.y - co.y + .002)
        need = min(need, .012)  # don't let the knot float off the band
        for i in group:
            mesh.vertices[i].co.y -= need
        shifts[name] = round(need, 4)
        # whatever jacket is still inside the piece tucks behind it, with a soft falloff
        mesh.update()
        tree = bvh(mesh, mask)
        tuck = {}
        for i in jacket:
            co = mesh.vertices[i].co
            if not (min(xs) - .01 < co.x < max(xs) + .01 and min(zs) - .01 < co.z < max(zs) + .01):
                continue
            hit, normal, _, _ = tree.ray_cast(co, Vector((0, 1, 0)), .08)
            if hit is not None and normal.y > 0:
                tuck[i] = hit.y - co.y + .002
        if tuck:
            kd = KDTree(len(tuck))
            for n, i in enumerate(tuck):
                kd.insert(mesh.vertices[i].co, n)
            kd.balance()
            keys = list(tuck)
            for i in jacket:
                best = 0.0
                for _, n, d in kd.find_range(mesh.vertices[i].co, .03):
                    best = max(best, tuck[keys[n]] * (1 - d / .03) ** 2)
                best = max(best, tuck.get(i, 0))
                mesh.vertices[i].co.y += best
        shifts[name + '_tucked'] = len(tuck)
    mesh.update()
    # the tucked jacket must still clear the band, shorts and skin
    push_out(mesh, near, band_mask, .0015, radius=.014, reach=.03)
    push_out(mesh, near, [x in ('Part_Body', 'Part_Shorts') for x in p], .0015, radius=.014, reach=.03)
    # the band follows the torso rather than riding rigidly on the hips
    skin = [i for i, x in enumerate(p) if x == 'Part_Body' and .38 < mesh.vertices[i].co.z < .6]
    copy_weights(o, band, skin)
    print('sash fitted', lifted, shifts)


# --------------------------------------------------------------------------- buttons
def fit_buttons():
    if not once('polish_buttons'):
        return
    o = obj()
    mesh = o.data
    p, m = masks(o)
    buttons = components(mesh, [i for i, x in enumerate(p) if x == 'Part_Jacket_Buttons'])
    tree = bvh(mesh, m['Part_Jacket'])
    placed = []
    for group in buttons:
        co = [mesh.vertices[i].co.copy() for i in group]
        centre = sum(co, Vector()) / len(co)
        hit, normal, _, _ = tree.ray_cast(Vector((centre.x, -1, centre.z)), Vector((0, 1, 0)), 2)
        if hit is None:
            raise RuntimeError('button misses the jacket')
        if normal.y > 0:
            normal = -normal
        back = max(c.y for c in co)
        depth = back - min(c.y for c in co)
        rotation = Vector((0, -1, 0)).rotation_difference(normal).to_matrix()
        for i, c in zip(group, co):
            local = Vector((c.x - centre.x, (c.y - back) * (.011 / depth), c.z - centre.z))
            mesh.vertices[i].co = hit + rotation @ local - normal * .0012
        placed.append((round(hit.x, 3), round(hit.z, 3), round(normal.y, 2)))
    mesh.update()
    jacket = [i for i, x in enumerate(p) if x == 'Part_Jacket']
    copy_weights(o, [i for g in buttons for i in g], jacket)
    print('buttons seated', placed)


# --------------------------------------------------------------------------- rig
def tidy_rig():
    if not once('polish_rig'):
        return
    r = rig()
    o = obj()
    depsgraph = bpy.context.evaluated_depsgraph_get()
    mesh = o.evaluated_get(depsgraph).to_mesh()
    to_mesh = o.matrix_world.inverted() @ r.matrix_world
    tree = BVHTree.FromPolygons([v.co for v in mesh.vertices], [tuple(t.vertices) for t in mesh.loop_triangles] if mesh.loop_triangles else [tuple(f.vertices) for f in mesh.polygons])
    bpy.context.view_layer.objects.active = r
    bpy.ops.object.mode_set(mode='EDIT')
    changed = {}
    for bone in r.data.edit_bones:
        leaf = not bone.children and (bone.name.endswith('_End') or bone.name[-1] == '4')
        if not leaf:
            continue
        head = to_mesh @ bone.head
        tail = to_mesh @ bone.tail
        direction = (tail - head).normalized()
        hit, *_ = tree.ray_cast(head, direction, 10)
        inside = (hit - head).length if hit is not None else 0
        limit = inside / (to_mesh.to_scale().x) * .85
        target = min(bone.length, max(limit, bone.parent.length * .25 if bone.parent else .01))
        if target < bone.length - 1e-5:
            changed[bone.name] = (round(bone.length, 3), round(target, 3))
            bone.length = target
    bpy.ops.object.mode_set(mode='OBJECT')
    o.evaluated_get(depsgraph).to_mesh_clear()
    r.data.display_type = 'STICK'
    r.show_in_front = False
    print('leaf bones shortened', changed)


# --------------------------------------------------------------------------- textures
def image_of(material):
    return next(n for n in material.node_tree.nodes if n.type == 'TEX_IMAGE').image


def read_pixels(image):
    w, h = image.size
    px = np.empty(w * h * 4, np.float32)
    image.pixels.foreach_get(px)
    return px.reshape(h, w, 4)[::-1].copy()          # top-down rows, like UV (u, 1-v)


def save_jpeg(image, path, quality=92):
    """Write `image` as JPEG with colour management neutralised (no exposure or look)."""
    scene = bpy.context.scene
    view, settings = scene.view_settings, scene.render.image_settings
    saved = (view.view_transform, view.look, view.exposure, view.gamma, settings.file_format, settings.quality)
    try:
        view.view_transform, view.look, view.exposure, view.gamma = 'Standard', 'None', 0.0, 1.0
        settings.file_format, settings.quality = 'JPEG', quality
        image.save_render(path, scene=scene)
    finally:
        view.view_transform, view.look, view.exposure, view.gamma, settings.file_format, settings.quality = saved


def write_pixels(material, px, name):
    """Store the edited texture as a new packed JPEG; the source image is left untouched."""
    h, w = px.shape[:2]
    old = bpy.data.images.get(name)
    if old is not None:          # writing over a packed image does not stick: replace it
        old.name = name + '_old'
    scratch = bpy.data.images.new(name + '_scratch', w, h, alpha=False)
    scratch.pixels.foreach_set(px[::-1].ravel())
    path = str(Path(bpy.app.tempdir) / (name + '.jpg'))
    save_jpeg(scratch, path)           # JPEG keeps the blend and GLB near their source size
    bpy.data.images.remove(scratch)
    image = bpy.data.images.load(path)
    image.pack()
    image.name = name
    node = next(n for n in material.node_tree.nodes if n.type == 'TEX_IMAGE')
    node.image = image
    if old is not None and not old.users:
        bpy.data.images.remove(old)


def face_labels(mesh, material_index, size, select=lambda poly: True):
    """Rasterise every selected polygon's UV triangles into a label map (-1 = none)."""
    w, h = size
    label = np.full((h, w), -1, np.int64)
    uv = mesh.uv_layers.active.data
    mesh.calc_loop_triangles()
    chosen = {p.index for p in mesh.polygons if p.material_index == material_index and select(p)}
    for tri in mesh.loop_triangles:
        if tri.polygon_index not in chosen:
            continue
        pts = np.array([(uv[l].uv.x * w, (1 - uv[l].uv.y) * h) for l in tri.loops])
        x0, y0 = np.floor(pts.min(0)).astype(int)
        x1, y1 = np.ceil(pts.max(0)).astype(int)
        x0, y0, x1, y1 = max(x0, 0), max(y0, 0), min(x1, w - 1), min(y1, h - 1)
        if x1 < x0 or y1 < y0:
            continue
        ys, xs = np.mgrid[y0:y1 + 1, x0:x1 + 1] + .5
        (ax, ay), (bx, by), (cx, cy) = pts
        det = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
        if abs(det) < 1e-12:
            continue
        l1 = ((by - cy) * (xs - cx) + (cx - bx) * (ys - cy)) / det
        l2 = ((cy - ay) * (xs - cx) + (ax - cx) * (ys - cy)) / det
        inside = (l1 >= -.02) & (l2 >= -.02) & (1 - l1 - l2 >= -.02)
        label[y0:y1 + 1, x0:x1 + 1][inside] = tri.polygon_index
    return label


def luminance(rgb):
    return rgb[..., 0] * .3 + rgb[..., 1] * .59 + rgb[..., 2] * .11


def fix_hairline():
    """Clean the forehead/hairline paint of the generated body texture.

    Head polygons are classified by the colour of their own texels (hair is near
    #292434, skin is warm). The forehead and temple skin under the hair carries a baked,
    hard-edged dark patch and UV-seam tone jumps: those skin texels are evened out toward
    the face's skin tone. Light or orange specks inside hair polygons are pulled back to
    that polygon's hair colour. Eyes, brows and mouth are left alone.
    """
    if not once('polish_hairline'):
        return
    o = obj()
    mesh = o.data
    material = mesh.materials[0]
    px = read_pixels(image_of(material))
    h, w = px.shape[:2]

    # the head skin/hair shell only: eyes, brows and teeth are separate pieces
    heads = components(mesh, [v.index for v in mesh.vertices if v.co.z > .69])
    shell = set(next(c for c in heads if max(mesh.vertices[i].co.z for i in c) > .95))

    def region(p):
        return p.material_index == 0 and p.center.z > .80 and p.vertices[0] in shell

    polys = [p for p in mesh.polygons if region(p)]
    label = face_labels(mesh, 0, (w, h), region)
    rgb = px[..., :3].reshape(-1, 3)
    lum = luminance(rgb)
    warm = (rgb[:, 0] - rgb[:, 2] > .3) & (rgb[:, 0] > .45)
    flat = label.ravel()
    order = np.argsort(flat, kind='stable')
    keys, starts = np.unique(flat[order], return_index=True)
    ends = list(starts[1:]) + [len(order)]
    texels = {int(k): order[s0:s1] for k, s0, s1 in zip(keys, starts, ends) if k >= 0}
    # reference skin tone: the lit cheeks and nose bridge
    face = [i for p in mesh.polygons if p.material_index == 0 and .74 < p.center.z < .80 and p.center.y < -.08
            for i in [p.index]]
    face_label = face_labels(mesh, 0, (w, h), lambda p: p.index in set(face)).ravel()
    sample = (face_label >= 0) & warm
    tone = np.median(rgb[sample], axis=0)
    mask = np.zeros(len(rgb), bool)
    evened = specks = 0
    for p in polys:
        idx = texels.get(p.index)
        if idx is None or not len(idx):
            continue
        share = warm[idx].mean()
        c = p.center
        if share > .6 and c.z > .83 and abs(c.x) < .115 and c.y < -.02:
            skin = idx[warm[idx]]
            # keep a little of the painted variation, drop the dark patch and seams
            rgb[skin] = rgb[skin] * .2 + tone * .8
            mask[skin] = True
            evened += len(skin)
        elif share < .2:
            bad = idx[warm[idx] | (lum[idx] > .3)]
            if len(bad):
                rgb[bad] = np.median(rgb[idx[~warm[idx]]], axis=0)
                mask[bad] = True
                specks += len(bad)
    px[..., :3] = rgb.reshape(h, w, 3)
    px = soften(px, dilate(mask.reshape(h, w), 3), passes=3)
    write_pixels(material, px, 'ElasticBrawler_Body_color')
    o['polish_face_tone'] = [float(x) for x in tone]
    print('hairline: skin texels evened', evened, 'hair specks', specks, 'tone', tone.round(3))


def dilate(mask, steps):
    out = mask.copy()
    for _ in range(steps):
        out = out | np.roll(out, 1, 0) | np.roll(out, -1, 0) | np.roll(out, 1, 1) | np.roll(out, -1, 1)
    return out


def soften(px, mask, passes=2):
    """A light 3x3 box blur inside `mask`, so repainted texels don't look pixel-hard."""
    ys, xs = np.nonzero(mask)
    if not len(ys):
        return px
    y0, y1 = max(ys.min() - 2, 0), min(ys.max() + 3, px.shape[0])
    x0, x1 = max(xs.min() - 2, 0), min(xs.max() + 3, px.shape[1])
    crop = px[y0:y1, x0:x1, :3].copy()
    inside = mask[y0:y1, x0:x1]
    for _ in range(passes):
        padded = np.pad(crop, ((1, 1), (1, 1), (0, 0)), mode='edge')
        rows = padded[:-2] + padded[1:-1] + padded[2:]
        blur = (rows[:, :-2] + rows[:, 1:-1] + rows[:, 2:]) / 9
        crop[inside] = blur[inside]
    out = px.copy()
    out[y0:y1, x0:x1, :3] = crop
    return out


def fix_specks():
    """Single-colour parts (sash, buttons): pull off-hue specks back to the part colour."""
    if not once('polish_specks'):
        return
    o = obj()
    mesh = o.data
    report = {}
    for index, material in enumerate(mesh.materials):
        if not any(k in material.name for k in ('Belt', 'Buttons')):
            continue
        px = read_pixels(image_of(material))
        h, w = px.shape[:2]
        used = face_labels(mesh, index, (w, h)) >= 0
        rgb = px[..., :3]
        ref = np.median(rgb[used], axis=0)
        saturation = lambda c: (c[..., 0] - c[..., 2]) / np.maximum(c[..., 0], 1e-3)
        off = used & (saturation(rgb) < .3 * saturation(ref))   # grey or blue specks
        # keep the shading: rescale the reference colour to each speck's luminance
        scale = (luminance(rgb) / max(luminance(ref), 1e-4)).clip(.55, 1.2)
        rgb[off] = ref * scale[off][:, None]
        px[..., :3] = rgb
        write_pixels(material, soften(px, off), 'ElasticBrawler_' + material.name.split('_')[-1] + '_color')
        report[material.name] = int(off.sum())
    print('specks cleaned', report)


def skin_tone():
    """Shift the warm skin texels from the generated orange toward the sheet's tan.

    The per-channel ratio between the sheet colour and the measured face tone is applied
    to every warm texel of the body texture, weighted by how skin-like it is, so the
    painted shading survives and hair, eyes and teeth stay as they are.
    """
    if not once('polish_skin_tone'):
        return
    material = obj().data.materials[0]
    px = read_pixels(image_of(material))
    rgb = px[..., :3]
    tone = np.array(obj()['polish_face_tone']) if 'polish_face_tone' in obj() else None
    warmness = ((rgb[..., 0] - rgb[..., 2]) - .2) / .25
    weight = warmness.clip(0, 1) * (rgb[..., 0] > .3)
    if tone is None:
        tone = np.median(rgb[weight > .99], axis=0)
    ratio = np.array(SKIN) / tone
    shifted = (rgb * ratio).clip(0, 1)
    px[..., :3] = rgb + (shifted - rgb) * weight[..., None]
    write_pixels(material, px, 'ElasticBrawler_Body_color')
    print('skin tone', tone.round(3), '->', np.round(SKIN, 3))


# --------------------------------------------------------------------------- checks
def report():
    """Interpenetration counts per garment pair (vertices inside the other surface)."""
    o = obj()
    mesh = o.data
    p, m = masks(o)
    idx = lambda name: [i for i, x in enumerate(p) if x == name]
    result = {
        'sandal_strap_inside_foot': overlap(mesh, [i for i in idx('Part_Sandals') if mesh.vertices[i].co.z > .02],
                                            [x == 'Part_Body' and mesh.vertices[i].co.z < .12 for i, x in enumerate(p)]),
        'buttons_inside_jacket': overlap(mesh, idx('Part_Jacket_Buttons'), m['Part_Jacket'], -.002),
        'jacket_inside_sash_band': overlap(mesh, [i for i in idx('Part_Jacket') if .40 < mesh.vertices[i].co.z < .56],
                                           [i in set(components(mesh, idx('Part_Belt'))[0]) for i in range(len(p))]),
    }
    print('overlap', result)
    return result


def run_all():
    fit_sandals()
    fit_sash()
    fit_buttons()
    tidy_rig()
    fix_hairline()
    fix_specks()
    skin_tone()
    return report()


def save(path=None):
    o = obj()
    o['polish'] = 'tools/blender/characters/polish_elastic_brawler.py'
    bpy.ops.wm.save_as_mainfile(filepath=str(path or OUT / 'elastic-brawler-polished.blend'), compress=True)


def render(name, direction):
    scene = bpy.context.scene
    target = Vector((0, 0, .9))
    scene.camera.location = target + Vector(direction)
    scene.camera.rotation_euler = (target - scene.camera.location).to_track_quat('-Z', 'Y').to_euler()
    scene.render.filepath = str(OUT / (name + '.png'))
    bpy.ops.render.render(write_still=True)


def previews():
    """Studio previews next to the model, framed like the assembly pass."""
    camera = bpy.context.scene.camera.data
    camera.type, camera.ortho_scale = 'ORTHO', 2.2
    for name, direction in (('preview-three-quarter', (2, -4, 1.5)), ('preview-front', (0, -4.5, .4)),
                            ('preview-back', (0, 4.5, .4)), ('preview-gameplay', (0, -2.4, 3.7))):
        render(name, direction)


def verify():
    """Skin weights still valid, rest pose untouched, and the rig still moves the mesh."""
    from mathutils import Quaternion
    o, r = obj(), rig()
    bones = {b.name for b in r.data.bones}
    names = {g.index: g.name for g in o.vertex_groups}
    for v in o.data.vertices:
        weights = [g.weight for g in v.groups if names[g.group] in bones and g.weight > 1e-5]
        if not weights or len(weights) > 4 or abs(sum(weights) - 1) > .0002:
            raise RuntimeError('invalid skin weights on vertex %d' % v.index)
    depsgraph = bpy.context.evaluated_depsgraph_get()
    rest = [v.co.copy() for v in o.evaluated_get(depsgraph).data.vertices]
    rest_error = max((a - b.co).length for a, b in zip(rest, o.data.vertices))
    tests = [('LeftArm', (0, 1, 0), -30), ('LeftForeArm', (0, 0, 1), -45), ('RightUpLeg', (1, 0, 0), -20),
             ('RightLeg', (1, 0, 0), 30), ('LeftFoot', (1, 0, 0), 20)]
    try:
        for name, axis, angle in tests:
            bone = r.pose.bones['mixamorig:' + name]
            basis = r.matrix_world.to_3x3() @ bone.bone.matrix_local.to_3x3()
            bone.rotation_mode = 'QUATERNION'
            bone.rotation_quaternion = Quaternion((basis.inverted() @ Vector(axis)).normalized(), math.radians(angle))
        bpy.context.view_layer.update()
        posed = [v.co.copy() for v in o.evaluated_get(bpy.context.evaluated_depsgraph_get()).data.vertices]
        travel = max((a - b).length for a, b in zip(rest, posed))
        render('preview-rig-check', (2, -4, 1.5))
    finally:
        for name, _, _ in tests:
            r.pose.bones['mixamorig:' + name].rotation_quaternion = Quaternion()
        bpy.context.view_layer.update()
    if rest_error > 1e-4 or travel < .02:
        raise RuntimeError('rig check failed: rest %.6f travel %.4f' % (rest_error, travel))
    result = {'rest_pose_error': rest_error, 'rig_check_max_vertex_motion': travel,
              'vertices': len(o.data.vertices)}
    print('verify', result)
    return result


def export():
    bpy.ops.object.select_all(action='DESELECT')
    collection = next(c for c in bpy.data.collections if obj().name in c.objects)
    for item in collection.objects:
        item.select_set(True)
    bpy.context.view_layer.objects.active = obj()
    result = bpy.ops.export_scene.gltf(filepath=str(OUT / 'elastic-brawler-polished.glb'), export_format='GLB',
                                       use_selection=True, export_yup=True, export_animations=False,
                                       export_skins=True, export_apply=False, export_extras=True)
    if 'FINISHED' not in result:
        raise RuntimeError('character GLB export failed')


def finish():
    """Save over the polished blend, check it, render previews and re-export the GLB."""
    checks = verify()
    previews()
    path = OUT / 'assembly.json'
    data = json.loads(path.read_text())
    data['polish'] = {'script': 'tools/blender/characters/polish_elastic_brawler.py',
                      'backup': 'elastic-brawler-polished.pre-polish.blend',
                      'overlaps': report(), **checks}
    path.write_text(json.dumps(data, indent=2) + '\n')
    for image in [i for i in bpy.data.images if i.name.endswith('_old') and not i.users]:
        bpy.data.images.remove(image)
    save()
    export()


if __name__ == '__main__' and '--' in sys.argv:
    command = sys.argv[sys.argv.index('--') + 1:]
    if command[:1] == ['all']:
        run_all()
        if len(command) > 1:
            save(command[1])
        else:
            finish()
