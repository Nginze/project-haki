"""Reusable canyon perimeter modules and flowering cacti. Execute in Blender via MCP."""
import math
from pathlib import Path
import runpy

import bpy
import bmesh
from mathutils import Matrix, Vector

ROOT = Path(__file__).resolve().parents[3]
kit = runpy.run_path(str(ROOT / 'tools/blender/kits/canyon_kit.py'))
Piece, loft, box, blob = (kit[n] for n in ('Piece', 'loft', 'box', 'blob'))


def boundary(name='canyon_boundary', height=2.8, corner=False):
    """Broad single-shell cliff mass; endpoints share a flush module cross-section."""
    p = Piece(name)
    p.gradient_range = (0, height)
    p.sharp_angle = math.radians(45)
    hx, hy = (1.5, 1.5) if corner else (2, 1)
    profiles = [(0, 0), (.03, 0), (.33, 0), (.335, .007), (.34, 0),
                (.65, 0), (.655, .009), (.66, 0), (.93, 0), (1, .10)]
    rings = []
    for t, inset in profiles:
        x, y = hx - inset, hy - inset
        cut = .19 if corner else .12
        xy = [(-x+cut,-y),(x-cut,-y),(x,-y+cut),(x,y-cut),
              (x-cut,y),(-x+cut,y),(-x,y-cut),(-x,-y+cut)]
        rings.append([(a,b,t*height) for a,b in xy])
    p.add(loft(rings),kit['by_normal']('wall_top','wall_side'),cull_bottom=True)
    # Broad, shallow cap forms read as a natural mesa rather than stacked cover cubes.
    for x,y,sx,sy,dz in [(-.65,.12,.62,.48,.09),(.55,.28,.45,.56,.13)]:
        p.add(box((x-sx/2,y-sy/2,height-.015),(x+sx/2,y+sy/2,height+dz),
                  bevel=.055,segments=1),kit['by_normal']('wall_top','wall_side'),cull_bottom=True)
    return p.build()


def cactus_materials():
    # Colours come from the cactus/flower roles in docs/palette.json.
    roles = kit['ROLES']
    colors = [('body', roles['cactus.base']), ('rib', roles['cactus.shade']), ('tip', roles['cactus.highlight']),
              ('petal', roles['flower.petal']), ('pollen', roles['flower.center']), ('spine', roles['cactus.thorn'])]
    result = []
    for name, value in colors:
        mat=bpy.data.materials.get('cactus_'+name) or bpy.data.materials.new('cactus_'+name)
        mat.use_nodes=True
        shader=next(n for n in mat.node_tree.nodes if n.type=='BSDF_PRINCIPLED')
        rgb=[int(value[i:i+2],16)/255 for i in (0,2,4)]
        rgb=[v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4 for v in rgb]
        shader.inputs['Base Color'].default_value=(*rgb,1)
        shader.inputs['Roughness'].default_value=.88
        mat.diffuse_color=(*rgb,1)
        result.append(mat)
    return result


def cactus(name='cactus_barrel', height=.95, radius=.34):
    """Rounded ribbed barrel, sparse cream thorns and a five-petal pink flower."""
    p=Piece(name)
    p.sharp_angle=math.radians(65)
    profile=[(0,.60),(.12,.92),(.35,1),(.64,.95),(.84,.72),(.94,.30),(.965,.12)]
    rings=[]
    for t,r in profile:
        rings.append([(math.cos(i*math.tau/16)*radius*r*(1.045 if i%2==0 else .965),
                       math.sin(i*math.tau/16)*radius*r*(1.045 if i%2==0 else .965),height*t)
                      for i in range(16)])
    body=loft(rings)
    for f in body.faces:
        a=math.atan2(f.calc_center_median().y,f.calc_center_median().x)
        f.material_index=2 if f.normal.z>.65 else int((a+math.pi)*8/math.pi)%2
    p.add(body,'leaf_grad',cull_bottom=True)

    def add(part,index):
        for f in part.faces:f.material_index=index
        p.add(part,'leaf_grad')

    for row,t in enumerate((.23,.49,.72)):
        r=next(r0+(r1-r0)*(t-t0)/(t1-t0) for (t0,r0),(t1,r1) in zip(profile,profile[1:]) if t0<=t<=t1)*radius
        for i in range(6):
            a=(i+row*.48)*math.tau/6
            outward=Vector((math.cos(a),math.sin(a),.16)).normalized()
            thorn=bmesh.new()
            bmesh.ops.create_cone(thorn,segments=4,radius1=.023,radius2=.002,depth=.065,cap_ends=True)
            bmesh.ops.transform(thorn,matrix=Vector((0,0,1)).rotation_difference(outward).to_matrix().to_4x4(),verts=thorn.verts)
            bmesh.ops.translate(thorn,vec=Vector((r*math.cos(a),r*math.sin(a),height*t))+outward*.022,verts=thorn.verts)
            add(thorn,5)
    top=height*.965
    for i in range(5):
        a=i*math.tau/5
        petal=blob((math.cos(a)*.075,math.sin(a)*.075,top-.005),.07,.055,segments=8,rings=4)
        add(petal,3)
    add(blob((0,0,top+.025),.052,.045,segments=8,rings=4),4)
    return p.build(materials=cactus_materials())


def build_assets():
    objects=[boundary(),boundary('canyon_boundary_low',1.2),
             boundary('canyon_corner',3.15,True),boundary('canyon_corner_low',1.35,True),
             cactus(),cactus('cactus_tall',1.25,.29)]
    for i,obj in enumerate(objects):obj.location=(i*4,-6,0)
    return objects


def export_assets(objects):
    directory=ROOT/'Assets/Game/Shared/Kits/canyon/Models'
    for obj in list(bpy.context.selected_objects):obj.select_set(False)
    for obj in objects:
        location=obj.location.copy();obj.location=(0,0,0)
        obj.hide_set(False);obj.select_set(True);bpy.context.view_layer.objects.active=obj
        bpy.ops.export_scene.gltf(filepath=str(directory/(obj.name+'.glb')),use_selection=True,
            export_yup=True,export_apply=True,export_cameras=False,export_lights=False)
        obj.select_set(False);obj.location=location


def write_library(objects):
    """Write an appendable collection and six individually marked Blender assets."""
    collection=bpy.data.collections.new('CanyonArenaAssets')
    labels=['Canyon_Rim_Straight','Canyon_Rim_Low','Canyon_Corner','Canyon_Corner_Low',
            'Cactus_Barrel','Cactus_Tall']
    try:
        for i,(source,label) in enumerate(zip(objects,labels)):
            obj=bpy.data.objects.new(label,source.data);obj.location=(i%3*5,-(i//3)*5,0)
            collection.objects.link(obj);obj.asset_mark()
            obj.asset_data.description='Reusable canyon arena module; metre units and ground-level origin.'
            obj.asset_data.tags.new('Canyon')
        bpy.data.libraries.write(str(ROOT/'Assets/Game/Shared/Kits/canyon/canyon_arena_assets.blend'),
                                 {collection},path_remap='RELATIVE_ALL',fake_user=True)
    finally:
        for obj in list(collection.objects):bpy.data.objects.remove(obj,do_unlink=True)
        bpy.data.collections.remove(collection)
