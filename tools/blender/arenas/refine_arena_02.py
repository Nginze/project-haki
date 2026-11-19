"""Refine the rough arena-02 in the live Blender session; export with the Haki operator."""
import math
from pathlib import Path
import random
import runpy
import sys

import bpy
import bmesh
from mathutils import Vector

ROOT=Path(__file__).resolve().parents[3]
DIRECTORY=ROOT/'Assets/Game/Scenes/arena-02'
OUTPUT=ROOT/'output/previews/arena-02'


def refine():
    if Path(bpy.data.filepath).resolve() != (DIRECTORY/'arena-02.blend').resolve():
        raise RuntimeError('Open arena-02.blend before refining this arena')
    scene=bpy.context.scene
    kit=runpy.run_path(str(ROOT/'tools/blender/kits/canyon_kit.py'))
    extras=runpy.run_path(str(ROOT/'tools/blender/kits/canyon_arena_assets.py'))
    prototypes=extras['build_assets']()
    library=bpy.data.collections['CanyonKit']
    library.hide_viewport=False
    library.hide_render=False
    extras['export_assets'](prototypes)
    palette=kit['palette_material']()
    root=bpy.data.collections['Arena_Map']

    def collection(name):
        result=bpy.data.collections.get(name) or bpy.data.collections.new(name)
        if result.name not in root.children:root.children.link(result)
        return result

    def clear(name):
        c=bpy.data.collections.get(name)
        if c:
            for obj in list(c.objects):bpy.data.objects.remove(obj,do_unlink=True)

    def put(piece,name,location,c,angle=0):
        obj=bpy.data.objects.new(name,bpy.data.objects[piece].data)
        obj.location=location;obj.rotation_euler.z=angle
        c.objects.link(obj)
        return obj

    def fit_body(obj,size=None,offset=None,sensor=False):
        h=obj.haki_settings
        h.mode='BODY';h.motion='static';h.shape='box';h.sensor=sensor
        h.layer='trigger' if sensor else 'static'
        h.friction=.65;h.restitution=0;h.preview=False
        if size is None:
            for other in list(bpy.context.selected_objects):other.select_set(False)
            obj.select_set(True);bpy.context.view_layer.objects.active=obj
            bpy.ops.haki.fit()
        else:h.size=size;h.offset=offset or (0,0,0)
        return obj

    # The original yellow shader is replaced by the kit's unchanged palette material.
    for obj in list(scene.objects):
        if obj.type=='MESH' and obj.data.name.startswith('bush_1x1'):
            obj.data.materials.clear();obj.data.materials.append(palette)
    clear('Arena_Spawns')
    clear('Arena_Fences')
    clear('Arena_Water')
    clear('Arena_Vegetation')
    for obj in list(bpy.data.collections['Arena_Walls'].objects):
        if max(abs(obj.location.x),abs(obj.location.y))>8.1 or obj.location.z>.1:
            bpy.data.objects.remove(obj,do_unlink=True)

    # A continuous welded floor removes the heavy checkerboard grooves between sand cells.
    clear('Arena_Floor')
    floor=collection('Arena_Floor')
    ponds=[(-3,-3),(3,-3),(-3,3),(3,3)]
    mesh=bpy.data.meshes.new('Arena02_ContinuousSand')
    bm=bmesh.new();verts={};uv=bm.loops.layers.uv.new('UVMap')
    for x in range(-15,15):
        for y in range(-15,15):
            if any(abs(x+.5-cx)<1 and abs(y+.5-cy)<1 for cx,cy in ponds):continue
            corners=[]
            for key in ((x,y),(x+1,y),(x+1,y+1),(x,y+1)):
                if key not in verts:verts[key]=bm.verts.new((*key,0))
                corners.append(verts[key])
            f=bm.faces.new(corners)
            for loop in f.loops:loop[uv].uv=kit['swatch_uv']('sand')
    bm.to_mesh(mesh);bm.free();mesh.materials.append(palette)
    ground=bpy.data.objects.new('Arena02_Ground',mesh);floor.objects.link(ground)
    fit_body(ground,(30,30,.4),(0,0,-.2))

    # Keep a few surface details, stripping the duplicate floor geometry from each decal.
    rng=random.Random(202)
    for prototype,count in [('sand_tile_cracked',18),('sand_tile_pebbles',14)]:
        detail=bpy.data.meshes.new('Arena02_'+prototype+'_detail')
        b=bmesh.new();b.from_mesh(bpy.data.objects[prototype].data)
        doomed=[f for f in b.faces if max(v.co.z for v in f.verts)<.0005]
        bmesh.ops.delete(b,geom=doomed,context='FACES')
        b.to_mesh(detail);b.free();detail.materials.append(palette)
        for i in range(count):
            x,y=rng.uniform(-11.7,11.7),rng.uniform(-11.7,11.7)
            if any(abs(x-cx)<1.4 and abs(y-cy)<1.4 for cx,cy in ponds):continue
            obj=bpy.data.objects.new(f'SandDetail_{prototype}_{i:02}',detail)
            obj.location=(x,y,.001);obj.rotation_euler.z=rng.randrange(4)*math.pi/2
            floor.objects.link(obj)

    boundary=collection('Arena_Bounds')
    clear('Arena_Bounds')
    for side in range(4):
        angle=side*math.pi/2
        for i,x in enumerate((-10,-6,-2,2,6,10)):
            point=Vector((x,14,0));point.rotate(__import__('mathutils').Matrix.Rotation(angle,3,'Z'))
            # The foreground rim is lower so it cannot hide the controlled character.
            piece='canyon_boundary_low' if side==2 else 'canyon_boundary'
            obj=put(piece,f'CanyonRim_{side}_{i}',point,boundary,angle)
            fit_body(obj)
    for x in (-13.5,13.5):
        for y in (-13.5,13.5):
            obj=put('canyon_corner_low' if y<0 else 'canyon_corner',f'CanyonCorner_{x}_{y}',(x,y,0),boundary)
            fit_body(obj)

    walls=bpy.data.collections['Arena_Walls']
    for obj in list(walls.objects):
        if any(abs(obj.location.x-cx)<1.499 and abs(obj.location.y-cy)<1.499 for cx,cy in ponds):
            bpy.data.objects.remove(obj,do_unlink=True)
        else:fit_body(obj)

    # Four compact ponds leave wide, connected north/south and east/west routes.
    water=collection('Arena_Water')
    physics=collection('Arena_Physics')
    clear('Arena_Physics')
    for i,(cx,cy) in enumerate(ponds):
        for dx,dy,turn in [(.5,.5,0),(-.5,.5,1),(-.5,-.5,2),(.5,-.5,3)]:
            put('water_corner_outer',f'Pond_{i}_{turn}',(cx+dx,cy+dy,0),water,turn*math.pi/2)
        blocker=bpy.data.objects.new(f'PondBlocker_{i}',None);blocker.location=(cx,cy,.4)
        blocker.empty_display_size=.25;physics.objects.link(blocker)
        fit_body(blocker,(2,2,.8))

    vegetation=collection('Arena_Vegetation')
    cells=[]
    for sx in (-1,1):
        for sy in (-1,1):
            cells.extend([(sx*4.5,sy*5.5),(sx*5.5,sy*5.5),(sx*5.5,sy*6.5)])
    cells.extend([(-1.5,2.5),(-.5,2.5),(1.5,-2.5),(.5,-2.5),(-2.5,-.5),(2.5,.5)])
    for i,(x,y) in enumerate(cells):
        obj=put('bush_1x1',f'ConcealmentBush_{i:02}',(x,y,0),vegetation,rng.randrange(4)*math.pi/2)
        fit_body(obj,(1,1,.85),(0,0,.425),sensor=True)

    # Keep the rough arena's four cover pockets, with spaced, non-overlapping crate groups.
    props=bpy.data.collections['Arena_Props']
    clear('Arena_Props')
    crates=[(-.9,.6),(.9,-.6),(-10.5,-4.5),(-10.5,-3.5),(10.5,3.5),(10.5,4.5),
            (-4.5,10.5),(-3.5,10.5),(3.5,-10.5),(4.5,-10.5),(-9.5,8.5),(-8.5,8.5),
            (9.5,10.5),(10.5,10.5),(-10.5,-10.5),(-9.5,-10.5),
            (5.5,-1.5),(-5.5,1.5),(1.5,5.5),(-1.5,-5.5)]
    for i,xy in enumerate(crates):
        put('crate',f'Crate_Reward_{i}',(*xy,0),props)
    for obj in props.objects:fit_body(obj)

    fences=collection('Arena_Fences')
    for i,(x,y,angle) in enumerate([(-8.5,-9.5,0),(6.5,9.5,0),(-9.5,6.5,math.pi/2),(9.5,-8.5,math.pi/2)]):
        direction=Vector((math.cos(angle),math.sin(angle),0))
        for j in range(3):
            point=Vector((x,y,0))+direction*j
            obj=put('fence_post',f'FencePost_{i}_{j}',point,fences)
            fit_body(obj,(.31,.31,.72),(0,0,.36))
            if j<2:
                obj=put('fence_rail',f'FenceRail_{i}_{j}',point,fences,angle)
                fit_body(obj)

    dress=collection('Arena_Dressing')
    clear('Arena_Dressing')
    for i,(x,y) in enumerate([(-11.4,-9),(11.5,9),(-10.7,1.2),(10.8,-.9),(-7.5,11.5),(7.8,-11.5),(-11.1,10.6),(11.2,-10.5)]):
        obj=put('cactus_tall' if i%2 else 'cactus_barrel',f'Cactus_{i:02}',(x,y,0),dress,rng.uniform(0,math.tau))
        fit_body(obj,(.6,.6,1.1 if i%2 else .8),(0,0,.55 if i%2 else .4))
    for i,(x,y) in enumerate([(-10.7,-10),(10.7,10),(-11.5,5),(11.5,-5),(-6,10.8),(6,-10.8)]):
        obj=put('rock_boulder',f'BoundaryRock_{i:02}',(x,y,0),dress,rng.uniform(0,math.tau))
        fit_body(obj)

    # Only one exported camera and sun; the original duplicated studio lights overexposed colors.
    for obj in list(scene.objects):
        if obj.type=='CAMERA' and obj.name!='Arena02_Camera':bpy.data.objects.remove(obj,do_unlink=True)
        elif obj.type=='LIGHT' and obj.name!='Arena02_Sun':bpy.data.objects.remove(obj,do_unlink=True)
    sun=bpy.data.objects['Arena02_Sun'];sun.data.energy=1;sun.data.color=(1,.98,.95)
    sun.rotation_euler=(math.radians(25),math.radians(-20),math.radians(-25))
    for n in scene.world.node_tree.nodes:
        if n.type=='BACKGROUND':n.inputs['Color'].default_value=(.65,.69,.74,1);n.inputs['Strength'].default_value=.65

    gameplay=collection('Arena_Gameplay')
    for obj in list(gameplay.objects):bpy.data.objects.remove(obj,do_unlink=True)
    scene.cursor.location=(0,-5.25,.03)
    bpy.ops.haki.add_marker(character=True)
    player=bpy.context.object;player.name='Player'
    for c in list(player.users_collection):c.objects.unlink(player)
    gameplay.objects.link(player)
    h=player.haki_settings;h.radius=.35;h.height=1.8;h.step=.3;h.speed=5;h.local_player=True;h.preview=True
    h.script_enabled=True;h.script_path='player.lua'
    camera=bpy.data.objects['Arena02_Camera']
    camera_types=[i.identifier for i in camera.data.bl_rna.properties['type'].enum_items]
    if 'ORTHO' not in camera_types:raise RuntimeError('Orthographic camera unsupported')
    camera.data.type='ORTHO';camera.data.ortho_scale=21.5;camera.data.clip_start=.1;camera.data.clip_end=150
    target=Vector((0,-3.8,0));camera.location=target+Vector((0,-22/math.tan(math.radians(58)),22))
    camera.rotation_euler=(target-camera.location).to_track_quat('-Z','Y').to_euler()
    scene.camera=camera;scene.render.resolution_x=1600;scene.render.resolution_y=900;scene.render.resolution_percentage=100

    library.hide_render=True;library.hide_viewport=True
    project=scene.haki_project
    project.root=str(ROOT);project.scene_id=project.pack='arena-02';project.scene_directory=str(DIRECTORY)
    project.python=str(ROOT/'.venv/bin/python');project.engine=str(ROOT/'build/Engine')
    project.scene_script='arena_rules.lua';project.default_skybox=True;project.skybox_asset=''
    project.import_after_export=True;project.set_entry=True
    bpy.ops.haki.repair_ids()
    editor=next(m for n,m in sys.modules.items() if n.endswith('haki_editor.editor'))
    errors=editor.validate_scene(bpy.context)
    if errors:raise RuntimeError('\n'.join(errors))
    print('Haki validation passed.',len(scene.objects),'scene objects;',len(walls.objects),'interior cover blocks.')
    return prototypes


def render_previews():
    scene=bpy.context.scene
    OUTPUT.mkdir(parents=True,exist_ok=True)
    scene.render.filepath=str(OUTPUT/'gameplay-camera.png')
    bpy.ops.render.render(write_still=True)
    gameplay=scene.camera
    data=bpy.data.cameras.new('_ArenaOverview')
    camera=bpy.data.objects.new('_ArenaOverview',data);scene.collection.objects.link(camera)
    data.type=gameplay.data.type;data.ortho_scale=54
    target=Vector((0,0,.3));camera.location=target+Vector((0,-27,43))
    camera.rotation_euler=(target-camera.location).to_track_quat('-Z','Y').to_euler()
    try:
        scene.camera=camera;scene.render.filepath=str(OUTPUT/'overview.png')
        bpy.ops.render.render(write_still=True)
    finally:
        scene.camera=gameplay;bpy.data.objects.remove(camera,do_unlink=True);bpy.data.cameras.remove(data)
