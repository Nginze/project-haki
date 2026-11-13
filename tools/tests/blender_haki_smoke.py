"""Run using Blender --background --factory-startup --python this_file.
Creates a disposable arena, exports/imports through the real operator, checks previews,
coordinate conversion, IDs and validation, then unregisters the add-on.
"""
import json
from pathlib import Path
import sys
import tempfile

import bpy
from mathutils import Vector

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(TOOLS/'blender'))
import haki_editor
from haki_editor import editor

haki_editor.register()
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
with tempfile.TemporaryDirectory(prefix='haki-blender-test-') as temporary:
    root = Path(temporary)
    (root/'tools').symlink_to(TOOLS,target_is_directory=True)
    (root/'.venv').symlink_to(TOOLS.parent/'.venv',target_is_directory=True)
    (root/'Assets/Scenes').mkdir(parents=True)
    (root/'Assets/AssetCatalog.json').write_text(json.dumps({'type':'haki.asset-catalog','version':1,'assets':[]}))
    (root/'Assets/Scenes/SceneManifest.json').write_text(json.dumps({'type':'haki.scene-manifest','version':1,
        'assetCatalog':'../AssetCatalog.json','entry':'','scenes':[]}))
    (root/'Assets/Game/Shared').mkdir(parents=True)
    (root/'Assets/Engine').mkdir(parents=True)
    for cat in ('Engine/Catalog.json','Game/Shared/Catalog.json'):
        (root/'Assets'/cat).write_text(json.dumps({'type':'haki.asset-catalog','version':1,'assets':[]}))
    (root/'Assets/Game/Project.json').write_text(json.dumps({'type':'haki.scene-manifest','version':1,
        'assetCatalog':'../Engine/Catalog.json','entry':'','scenes':[]}))
    p = bpy.context.scene.haki_project
    p.root = str(root)
    p.output = str(root/'exports/arena.glb')
    p.pack = p.scene_id = 'smoke'
    bpy.ops.mesh.primitive_cube_add(location=(2,3,4))
    wall = bpy.context.object
    wall.name = 'Wall'
    wall.rotation_euler.z = 0.4
    wall.scale = (2,3,4)
    wall.haki_settings.mode = 'BODY'
    wall.haki_settings.size = (2,4,6)
    wall.haki_settings.offset = (0.25,0.5,0.75)
    wall_id = wall.haki_settings.stable_id
    # Explicit previews use the same local dimensions and offset as exported metadata.
    assert len(editor.preview_lines(wall)) == 24
    bpy.ops.haki.add_marker(character=True)
    player = bpy.context.object
    player.name = 'Hero'
    player.location = (1,2,3)
    player_id = player.haki_settings.stable_id
    bpy.ops.haki.add_marker()
    trigger = bpy.context.object
    trigger.name = 'Sensor'
    trigger.haki_settings.sensor = True
    bpy.ops.haki.add_marker()
    sphere = bpy.context.object
    sphere.name = 'Sphere'
    sphere.haki_settings.shape = 'sphere'
    sphere.haki_settings.radius = 0.7
    sphere.scale = (2,2,2)
    sphere.haki_settings.motion = 'dynamic'
    sphere_id = sphere.haki_settings.stable_id
    bpy.ops.haki.add_marker()
    capsule = bpy.context.object
    capsule.name = 'Capsule'
    capsule.haki_settings.shape = 'capsule'
    capsule.haki_settings.radius = 0.4
    capsule.haki_settings.height = 2.4
    capsule_id = capsule.haki_settings.stable_id
    # A real camera must survive export as well.
    bpy.ops.object.camera_add(location=(0,-10,12))
    bpy.context.scene.camera = bpy.context.object
    bpy.ops.object.light_add(type='SUN')
    sun = bpy.context.object
    sun.data.energy = 1.0
    sun.data.exposure = 0
    bpy.ops.object.light_add(type='POINT')
    point = bpy.context.object
    point.data.energy = 2.0
    point.data.exposure = 0
    # Real skinned mesh + two actions, with runtime parenting independent of armature parenting.
    bpy.ops.import_scene.gltf(filepath=str(TOOLS.parent/'src/Tests/Serialization/Fixtures/SkinnedTriangle.gltf'))
    skinned = next(o for o in bpy.context.scene.objects if o.type == 'MESH' and o.find_armature())
    rig = skinned.find_armature()
    original = next(a for a in bpy.data.actions)
    for track in list(rig.animation_data.nla_tracks):
        rig.animation_data.nla_tracks.remove(track)
    original.name = 'Idle'
    run = original.copy()
    run.name = 'Run'
    track = rig.animation_data.nla_tracks.new()
    track.name = 'Run'
    strip = track.strips.new('Run',1,run)
    if hasattr(strip,'action_slot') and run.slots:
        strip.action_slot = run.slots[0]
    track.mute = True
    rig.animation_data.action = original
    if original.slots:
        rig.animation_data.action_slot = original.slots[0]
    skinned.haki_settings.mode = 'VISUAL'
    skinned.haki_settings.visual_parent = player
    skinned.haki_settings.idle_clip = 'Idle'
    skinned.haki_settings.move_clip = 'Run'
    visual_id = skinned.haki_settings.stable_id
    assert skinned.parent == rig
    scripts = root/'Assets/Game/Scenes/smoke/Scripts'
    scripts.mkdir(parents=True)
    (scripts/'entity.lua').write_text('return { on_update = function(self, dt) end }')
    (scripts/'scene.lua').write_text('return { on_start = function(self) end }')
    p.scene_script = str(scripts/'scene.lua')
    player.haki_settings.script_enabled = True
    player.haki_settings.script_path = 'entity.lua'
    for name,kind,value in [('speed','number',7.5),('active','boolean',True),('label','string','hero'),('spawn','vector',(1,2,3))]:
        prop = player.haki_settings.script_properties.add()
        prop.name, prop.kind = name,kind
        setattr(prop,kind,value)
    bpy.ops.object.empty_add()
    script_only = bpy.context.object
    script_only.name = 'ScriptOnly'
    script_only.haki_settings.script_enabled = True
    script_only.haki_settings.script_path = str(scripts/'entity.lua')
    script_only_id = script_only.haki_settings.stable_id
    # Shared visual data retains independently authored IDs.
    duplicate = wall.copy()
    bpy.context.collection.objects.link(duplicate)
    assert any('duplicate stable ID' in e for e in editor.validate_scene(bpy.context))
    bpy.ops.haki.repair_ids()
    assert duplicate.haki_settings.stable_id != wall.haki_settings.stable_id
    bpy.data.objects.remove(duplicate,do_unlink=True)
    wall_id = wall.haki_settings.stable_id
    assert editor.validate_scene(bpy.context) == [], editor.validate_scene(bpy.context)
    assert bpy.ops.haki.export() == {'FINISHED'}
    scene = json.loads((root/'Assets/Game/Scenes/smoke/smoke.scene.json').read_text())
    entities = {e['id']:e for e in scene['entities']}
    assert scene['script'] == 'scene.lua',scene
    script = entities[player_id]['components']['script']
    assert script['path'] == 'entity.lua',script
    assert script['properties'] == dict(speed=7.5,active=True,label='hero',spawn=[1,2,3]),script
    assert entities[script_only_id]['components']['script']['path'] == 'entity.lua'
    assert 'rigidBody' not in entities[script_only_id]['components']
    assert 'haki' not in bpy.context.scene
    visual = entities[visual_id]['components']
    assert visual['parent']['entity'] == player_id,visual
    assert 'skinnedMeshRenderer' in visual and 'animator' in visual,visual
    assert set(('Idle','Run')).issubset(visual['animator']['clips']),visual
    assert visual['locomotion']['moveClip'] == 'Run',visual
    assert skinned.parent == rig
    w = entities[wall_id]['components']
    assert w['collider']['halfExtents'] == [1,3,2], w
    assert w['collider']['offset'] == [0.25,0.75,-0.5], w
    assert all(abs(a-b)<1e-5 for a,b in zip(w['transform']['translation'],[2,4,-3])),w
    assert all(abs(a-b)<1e-5 for a,b in zip(w['transform']['scale'],[2,4,3])),w
    h = entities[player_id]['components']
    assert 'characterController' in h and 'playerInput' in h and 'meshRenderer' not in h,h
    assert abs(h['characterController']['halfHeight']-0.55)<1e-5
    assert any(e['components'].get('rigidBody',{}).get('sensor') for e in entities.values())
    # Reimport is stable and temporary extras never pollute authoring objects.
    sun_values = [e['components']['directionalLight']['intensity'] for e in entities.values()
                  if 'directionalLight' in e['components']]
    point_values = [e['components']['pointLight']['intensity'] for e in entities.values()
                    if 'pointLight' in e['components']]
    assert sun_values == [1.0], sun_values
    assert point_values == [2.0], point_values
    assert 'import-camera' not in entities
    assert entities[sphere_id]['components']['collider']['shape'] == 'sphere'
    cap = entities[capsule_id]['components']['collider']
    assert cap['shape'] == 'capsule' and abs(cap['halfHeight']-0.8)<1e-5,cap
    assert 'haki' not in wall
    assert bpy.ops.haki.export() == {'FINISHED'}
    scene2 = json.loads((root/'Assets/Game/Scenes/smoke/smoke.scene.json').read_text())
    assert [e['id'] for e in scene2['entities']] == [e['id'] for e in scene['entities']]
    # Export-only mode must not invoke any host/project command.
    from unittest.mock import patch
    p.import_after_export = False
    with patch.object(editor, 'run_import', side_effect=AssertionError('unexpected importer')):
        assert bpy.ops.haki.export() == {'FINISHED'}
    p.import_after_export = True
    previous = (root/'Assets/Game/Scenes/smoke/smoke.scene.json').read_bytes()
    player.haki_settings.height = 0.1
    assert editor.validate_scene(bpy.context)
    try:
        bpy.ops.haki.export()
        raise AssertionError('Invalid export unexpectedly succeeded')
    except RuntimeError:
        pass
    assert (root/'Assets/Game/Scenes/smoke/smoke.scene.json').read_bytes() == previous
haki_editor.unregister()
assert not hasattr(bpy.types.Object,'haki_settings')
print('HAKI_BLENDER_SMOKE_OK')
