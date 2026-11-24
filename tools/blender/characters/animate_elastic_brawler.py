"""Idle, walk and run clips for the Elastic Brawler, retargeted from Mixamo.

Run headless on the polished character file:

    blender -b output/characters/elastic-brawler/model/elastic-brawler-polished.blend \
        --python tools/blender/characters/animate_elastic_brawler.py -- all

Mixamo FBX clips go in output/characters/elastic-brawler/animations/ (Idle.fbx,
Walking.fbx, Running.fbx; "In Place", any skin setting). The rig shares Mixamo's
bone names but not its rest pose or proportions, so each frame is transferred as a
world-space rotation change from the rest pose, with hips motion scaled by leg
height. Each clip becomes an action (Idle, Walk, Run) on its own NLA track, which
both the character GLB and the Haki arena export pick up.
"""
import json
import math
import sys
from pathlib import Path

import bpy
from mathutils import Matrix, Quaternion, Vector

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'output/characters/elastic-brawler/model'
CLIPS = ROOT / 'output/characters/elastic-brawler/animations'
SOURCES = {'Idle': 'Idle.fbx', 'Walk': 'Walking.fbx', 'Run': 'Running.fbx'}
LOOPED = ('Walk', 'Run')          # locomotion cycles: speed is measured from the stride
FOOT_BONES = ('LeftToeBase', 'RightToeBase', 'LeftFoot', 'RightFoot')


def rig():
    return bpy.data.objects['ElasticBrawler_Rig']


def mesh():
    return bpy.data.objects['ElasticBrawler_Mesh']


def order(armature):
    """Bones parent first."""
    result = []

    def visit(bone):
        result.append(bone)
        for child in bone.children:
            visit(child)

    for bone in armature.data.bones:
        if bone.parent is None:
            visit(bone)
    return result


def import_clip(path):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=str(path), automatic_bone_orientation=False, use_anim=True)
    new = [o for o in bpy.data.objects if o not in before]
    source = next(o for o in new if o.type == 'ARMATURE')
    return source, new


def remove(objects):
    actions = {o.animation_data.action for o in objects if o.animation_data and o.animation_data.action}
    meshes = {o.data for o in objects if o.type == 'MESH'}
    armatures = {o.data for o in objects if o.type == 'ARMATURE'}
    for o in objects:
        bpy.data.objects.remove(o, do_unlink=True)
    for block in meshes:
        bpy.data.meshes.remove(block)
    for block in armatures:
        bpy.data.armatures.remove(block)
    for action in actions:
        bpy.data.actions.remove(action)


def curves(action):
    """F-curves of a (Blender 4.4+ slotted or legacy) action."""
    if hasattr(action, 'fcurves'):
        return list(action.fcurves)
    result = []
    for layer in action.layers:
        for strip in layer.strips:
            for bag in strip.channelbags:
                result.extend(bag.fcurves)
    return result


def world_head(armature, name, pose=True):
    bone = armature.pose.bones[name] if pose else armature.data.bones[name]
    return armature.matrix_world @ (bone.head if pose else bone.head_local)


def retarget(source, name):
    """Bake the source armature's animation onto the rig as a new action."""
    scene = bpy.context.scene
    target = rig()
    start, end = (int(round(f)) for f in source.animation_data.action.frame_range)
    shared = [b for b in order(target) if b.name in source.data.bones]
    missing = sorted(b.name for b in target.data.bones if b.name not in source.data.bones)
    if missing:
        print('retarget %s: bones without source animation: %s' % (name, ', '.join(missing)))

    target_world = target.matrix_world.to_quaternion()
    source_world = source.matrix_world.to_quaternion()
    # World rest orientations and the fixed parent-to-child rest rotation for each target bone.
    rest_target = {b.name: target_world @ b.matrix_local.to_quaternion() for b in shared}
    rest_source = {b.name: source_world @ source.data.bones[b.name].matrix_local.to_quaternion() for b in shared}
    # The rig rests in an A-pose and Mixamo in a T-pose: copying rotations *from rest* would add
    # the arm-down rotation twice. Instead each target bone takes the source bone's absolute
    # direction: `align` turns the target rest axis onto the source rest axis (shortest arc).
    align = {}
    for b in shared:
        want = rest_source[b.name] @ Vector((0, 1, 0))
        have = rest_target[b.name] @ Vector((0, 1, 0))
        align[b.name] = have.rotation_difference(want)
    relative = {b.name: (b.parent.matrix_local.inverted() @ b.matrix_local).to_quaternion() if b.parent
                else b.matrix_local.to_quaternion() for b in shared}
    hips = 'mixamorig:Hips'
    ratio = world_head(target, hips, pose=False).z / world_head(source, hips, pose=False).z
    hips_rest_target = world_head(target, hips, pose=False)
    hips_rest_source = world_head(source, hips, pose=False)

    action = bpy.data.actions.new(name)
    action.use_fake_user = True
    target.animation_data_create()
    target.animation_data.action = action
    for bone in target.pose.bones:
        bone.rotation_mode = 'QUATERNION'
        bone.location, bone.rotation_quaternion, bone.scale = Vector(), Quaternion(), Vector((1, 1, 1))

    for frame in range(start, end + 1):
        scene.frame_set(frame)
        armature_space = {}
        for bone in shared:
            pose = source.pose.bones[bone.name]
            world = source_world @ pose.matrix.to_quaternion()
            wanted = world @ rest_source[bone.name].inverted() @ align[bone.name] @ rest_target[bone.name]
            in_armature = target_world.inverted() @ wanted
            armature_space[bone.name] = in_armature
            parent = armature_space.get(bone.parent.name) if bone.parent else None
            if bone.parent and parent is None:       # parent has no source bone: keep it at rest
                parent = bone.parent.matrix_local.to_quaternion()
                armature_space[bone.parent.name] = parent
            local = relative[bone.name].inverted() @ (parent.inverted() if parent else Quaternion()) @ in_armature
            target_pose = target.pose.bones[bone.name]
            if target_pose.rotation_quaternion.dot(local) < 0:   # keep keys on the short path
                local.negate()
            target_pose.rotation_quaternion = local
            target_pose.keyframe_insert('rotation_quaternion', frame=frame - start + 1, group=bone.name)
        # Hips travel, scaled to the shorter legs, expressed in the hips' rest frame.
        offset = (world_head(source, hips) - hips_rest_source) * ratio
        head = target.matrix_world.inverted() @ (hips_rest_target + offset)
        rest = target.data.bones[hips].matrix_local
        target.pose.bones[hips].location = rest.inverted() @ head
        target.pose.bones[hips].keyframe_insert('location', frame=frame - start + 1, group=hips)
    action.frame_range = (1, end - start + 1)
    for curve in curves(action):
        for key in curve.keyframe_points:
            key.interpolation = 'LINEAR'
    return action


def sample(action, names, step=1):
    """World positions of `names` for every frame of `action`."""
    target, scene = rig(), bpy.context.scene
    target.animation_data.action = action
    start, end = (int(round(f)) for f in action.frame_range)
    frames = []
    for frame in range(start, end + 1, step):
        scene.frame_set(frame)
        frames.append({n: world_head(target, 'mixamorig:' + n) for n in names})
    return frames


def ground(action):
    """Shift the hips so the lowest toe over the clip sits where it does at rest."""
    target = rig()
    rest = min(world_head(target, 'mixamorig:' + n, pose=False).z for n in ('LeftToeBase', 'RightToeBase'))
    low = min(min(f[n].z for n in ('LeftToeBase', 'RightToeBase')) for f in sample(action, FOOT_BONES))
    shift = rest - low
    hips = target.data.bones['mixamorig:Hips']
    local = (target.matrix_world.to_3x3() @ hips.matrix_local.to_3x3()).inverted() @ Vector((0, 0, shift))
    for curve in curves(action):
        if curve.data_path == 'pose.bones["mixamorig:Hips"].location':
            for key in curve.keyframe_points:
                key.co.y += local[curve.array_index]
                key.handle_left.y += local[curve.array_index]
                key.handle_right.y += local[curve.array_index]
    return shift


def stride_speed(action):
    """Ground speed implied by the planted foot: how fast it slides backwards (+Y) in contact."""
    fps = bpy.context.scene.render.fps
    frames = sample(action, ('LeftToeBase', 'RightToeBase'))
    speeds = []
    for name in ('LeftToeBase', 'RightToeBase'):
        heights = [f[name].z for f in frames]
        low = min(heights)
        for a, b in zip(frames, frames[1:]):
            if a[name].z < low + .02 and b[name].z < low + .02:
                speeds.append((b[name].y - a[name].y) * fps)
    speeds.sort()
    return speeds[len(speeds) // 2] if speeds else 0.0


def loop_error(action):
    start, end = (int(round(f)) for f in action.frame_range)
    error = 0.0
    for curve in curves(action):
        error = max(error, abs(curve.evaluate(start) - curve.evaluate(end)))
    return error


def build():
    target = rig()
    for name in SOURCES:
        if name in bpy.data.actions:
            bpy.data.actions.remove(bpy.data.actions[name])
    if target.animation_data:
        for track in list(target.animation_data.nla_tracks):
            target.animation_data.nla_tracks.remove(track)
    bpy.context.scene.render.fps = 30
    report = {}
    for name, file in SOURCES.items():
        source, imported = import_clip(CLIPS / file)
        action = retarget(source, name)
        remove(imported)
        shift = ground(action)
        frames = sample(action, ('Hips',))
        drift = max(math.hypot(f['Hips'].x - frames[0]['Hips'].x, f['Hips'].y - frames[0]['Hips'].y) for f in frames)
        report[name] = {'source': file, 'frames': int(action.frame_range[1]),
                        'seconds': round((action.frame_range[1] - 1) / 30, 3),
                        'loop_error': round(loop_error(action), 6), 'hips_sway_m': round(drift, 3),
                        'ground_shift_m': round(shift, 4)}
        if name in LOOPED:
            report[name]['stride_speed_mps'] = round(stride_speed(action), 3)
        print('clip', name, report[name])
    target.animation_data.action = None
    for name in SOURCES:
        track = target.animation_data.nla_tracks.new()
        track.name = name
        action = bpy.data.actions[name]
        track.strips.new(name, 1, action)
        track.mute = True
    for bone in target.pose.bones:
        bone.location, bone.rotation_quaternion, bone.scale = Vector(), Quaternion(), Vector((1, 1, 1))
    bpy.context.scene.frame_set(1)
    return report


def posed_vertices(action, frame):
    target = rig()
    target.animation_data.action = action
    bpy.context.scene.frame_set(frame)
    evaluated = mesh().evaluated_get(bpy.context.evaluated_depsgraph_get())
    world = mesh().matrix_world
    return [world @ v.co for v in evaluated.data.vertices]


def normalize_transforms():
    """Bake the root scale and the rig's rotation into the rig and mesh data.

    The assembled file had the root scaled 1.78x, the rig rotated -90/-90 deg and the mesh
    counter-rotated under it. Blender's glTF exporter places a skinned mesh at its armature's
    transform, so engine-side parenting of the mesh (Haki attached visual) lost that rotation
    and the character fell flat as soon as the player turned. Afterwards every object in the
    character is identity apart from the root's placement, so Blender and glTF agree.
    Pose keys stay valid: bone-local rotations don't change under a rigid rotation and a
    uniform scale; hips translation keys scale with the bones.
    """
    root, target, body = bpy.data.objects['ElasticBrawler'], rig(), mesh()
    if all(o.matrix_world.is_identity for o in (target, body)):
        return 0.0
    probe = bpy.data.actions['Walk'] if 'Walk' in bpy.data.actions else None
    before = posed_vertices(probe, 9) if probe else None
    target.animation_data.action = None
    for bone in target.pose.bones:
        bone.location, bone.rotation_quaternion, bone.scale = Vector(), Quaternion(), Vector((1, 1, 1))
    bpy.context.view_layer.update()
    rig_world, mesh_world = target.matrix_world.copy(), body.matrix_world.copy()
    scale = rig_world.to_scale()
    if max(scale) - min(scale) > 1e-4:
        raise RuntimeError('rig scale is not uniform: %s' % (tuple(scale),))
    target.data.transform(rig_world)
    body.data.transform(mesh_world)
    for o in (root, target, body):
        o.rotation_mode = 'QUATERNION'
    root.location, root.rotation_quaternion, root.scale = Vector(), Quaternion(), Vector((1, 1, 1))
    for o in (target, body):
        o.matrix_parent_inverse = Matrix.Identity(4)
        o.location, o.rotation_quaternion, o.scale = Vector(), Quaternion(), Vector((1, 1, 1))
    for action in bpy.data.actions:
        for curve in curves(action):
            if curve.data_path.endswith('.location'):
                for key in curve.keyframe_points:
                    for point in (key.co, key.handle_left, key.handle_right):
                        point.y *= scale[0]
    bpy.context.view_layer.update()
    error = 0.0
    if probe:
        after = posed_vertices(probe, 9)
        error = max((a - b).length for a, b in zip(before, after))
        target.animation_data.action = None
        if error > 1e-3:
            raise RuntimeError('normalizing moved the posed mesh by %.4f m' % error)
    print('normalized transforms; posed mesh moved at most %.6f m' % error)
    return error


def contact_sheets(frames_dir=None):
    """Eight frames of each clip from the front and the side into one image per clip."""
    scene, target = bpy.context.scene, rig()
    camera = scene.camera
    camera.data.type, camera.data.ortho_scale = 'ORTHO', 2.3
    scene.render.resolution_x, scene.render.resolution_y = 360, 460
    if scene.render.engine == 'CYCLES':
        scene.cycles.samples = 16
    frames_dir = Path(frames_dir or OUT / 'anim-frames')
    frames_dir.mkdir(parents=True, exist_ok=True)
    for name in SOURCES:
        action = bpy.data.actions[name]
        target.animation_data.action = action
        last = int(action.frame_range[1])
        for view, direction in (('front', (0, -4.5, .3)), ('side', (4.5, 0, .3))):
            for index in range(8):
                frame = 1 + round(index * (last - 1) / 8)
                scene.frame_set(frame)
                look = Vector((0, 0, .9))
                camera.location = look + Vector(direction)
                camera.rotation_euler = (look - camera.location).to_track_quat('-Z', 'Y').to_euler()
                scene.render.filepath = str(frames_dir / ('%s-%s-%d.png' % (name, view, index)))
                bpy.ops.render.render(write_still=True)
    target.animation_data.action = None
    for bone in target.pose.bones:
        bone.location, bone.rotation_quaternion, bone.scale = Vector(), Quaternion(), Vector((1, 1, 1))


def export():
    bpy.ops.object.select_all(action='DESELECT')
    collection = bpy.data.collections['ElasticBrawler_Character']
    for item in collection.objects:
        item.select_set(True)
    bpy.context.view_layer.objects.active = mesh()
    result = bpy.ops.export_scene.gltf(filepath=str(OUT / 'elastic-brawler-polished.glb'), export_format='GLB',
                                       use_selection=True, export_yup=True, export_skins=True,
                                       export_apply=False, export_extras=True, export_animations=True,
                                       export_animation_mode='ACTIONS', export_force_sampling=True,
                                       export_frame_step=1, export_optimize_animation_size=False)
    if 'FINISHED' not in result:
        raise RuntimeError('character GLB export failed')


def check_glb():
    import struct
    data = (OUT / 'elastic-brawler-polished.glb').read_bytes()
    length = struct.unpack_from('<I', data, 12)[0]
    gltf = json.loads(data[20:20 + length])
    animations = {a['name']: a for a in gltf.get('animations', [])}
    interpolations = {s.get('interpolation', 'LINEAR') for a in animations.values() for s in a['samplers']}
    if set(animations) != set(SOURCES) or interpolations - {'LINEAR', 'STEP'}:
        raise RuntimeError('GLB animations wrong: %s %s' % (sorted(animations), interpolations))
    joints = len(gltf['skins'][0]['joints'])
    print('glb animations', sorted(animations), interpolations, 'joints', joints)
    return {'animations': sorted(animations), 'joints': joints}


def finish(report):
    path = OUT / 'assembly.json'
    data = json.loads(path.read_text())
    data['animation'] = {'script': 'tools/blender/characters/animate_elastic_brawler.py',
                         'source': 'Mixamo (In Place), retargeted', 'fps': 30, 'clips': report,
                         'glb': check_glb()}
    path.write_text(json.dumps(data, indent=2) + '\n')


if __name__ == '__main__' and '--' in sys.argv:
    command = sys.argv[sys.argv.index('--') + 1:]
    if command[:1] == ['all']:
        clips = build()
        normalize_transforms()
        bpy.ops.wm.save_as_mainfile(filepath=str(OUT / 'elastic-brawler-polished.blend'), compress=True)
        export()
        finish(clips)
        bpy.ops.wm.save_as_mainfile(filepath=str(OUT / 'elastic-brawler-polished.blend'), compress=True)
    elif command[:1] == ['build']:      # scratch run: build into another file
        clips = build()
        normalize_transforms()
        bpy.ops.wm.save_as_mainfile(filepath=command[1], compress=True)
        print('REPORT', json.dumps(clips))
    elif command[:1] == ['sheets']:
        contact_sheets(command[1] if len(command) > 1 else None)
