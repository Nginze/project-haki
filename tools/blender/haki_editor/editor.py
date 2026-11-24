"""Blender UI and export. Preview geometry is drawn, never added to the scene."""
import json
import math
import os
from pathlib import Path
import subprocess
import tempfile
import uuid

import bpy
from bpy.props import (BoolProperty, EnumProperty, FloatProperty, FloatVectorProperty,
                       PointerProperty, StringProperty, CollectionProperty, IntProperty)
from mathutils import Vector
from .metadata import validate_metadata, validate_transform, validate_script_path, MetadataError
from .processes import command_environment, failure_message


def ensure_id(obj):
    if not obj.haki_settings.stable_id:
        if obj.library:
            raise ValueError(f'{obj.name}: make the object local before authoring Haki settings')
        obj.haki_settings.stable_id = str(uuid.uuid4())
    return obj.haki_settings.stable_id


def mode_changed(self, context):
    if self.mode != 'NONE' and isinstance(self.id_data, bpy.types.Object):
        ensure_id(self.id_data)


def authored(obj):
    return obj.haki_settings.mode != 'NONE' or obj.haki_settings.script_enabled


def script_changed(self, context):
    if self.script_enabled:
        ensure_id(self.id_data)


class HakiScriptProperty(bpy.types.PropertyGroup):
    name: StringProperty(name='Name')
    kind: EnumProperty(name='Type',items=[('number','Number',''),('boolean','Boolean',''),
                                       ('string','String',''),('vector','Vector 3','')])
    number: FloatProperty(name='Value')
    boolean: BoolProperty(name='Value')
    string: StringProperty(name='Value')
    vector: FloatVectorProperty(name='Value',size=3)


class HakiObject(bpy.types.PropertyGroup):
    mode: EnumProperty(name='Type', items=[('NONE', 'None', ''), ('BODY', 'Rigid Body + Collider', ''),
                                          ('CHARACTER', 'Character Controller', ''), ('VISUAL', 'Attached Visual', '')], update=mode_changed)
    stable_id: StringProperty(name='Stable ID')
    script_enabled: BoolProperty(name='Entity Script', update=script_changed)
    script_path: StringProperty(name='Lua Script',subtype='FILE_PATH',description='Relative to Assets/Scripts, e.g. player.lua; file picker also supported')
    script_properties: CollectionProperty(type=HakiScriptProperty)
    visual_parent: PointerProperty(name='Runtime Parent', type=bpy.types.Object,
        description='Player marker or another authored visual; does not change armature parenting')
    locomotion: BoolProperty(name='Drive Idle / Movement', default=True)
    idle_clip: StringProperty(name='Idle Clip', default='Idle')
    move_clip: StringProperty(name='Movement Clip', default='Run')
    speed_threshold: FloatProperty(name='Movement Threshold (m/s)', default=0.1, min=0)
    use_blend: BoolProperty(name='Idle / Walk / Run Speed Blend', default=False,
        description='Blend three clips by the controller\'s actual speed instead of switching idle and movement')
    walk_clip: StringProperty(name='Walk Clip', default='Walk')
    walk_speed: FloatProperty(name='Walk Speed (m/s)', default=1.5, min=0.01,
        description='Speed at which the walk clip plays at its authored rate')
    run_speed: FloatProperty(name='Run Speed (m/s)', default=4.5, min=0.02,
        description='Speed at which the movement clip plays at its authored rate')
    motion: EnumProperty(name='Motion', items=[(v, v.title(), '') for v in ('static','kinematic','dynamic')])
    layer: EnumProperty(name='Layer', items=[('auto','Automatic','Derived from motion and sensor')]+
                       [(v,v.title(),'') for v in ('static','dynamic','character','trigger')])
    shape: EnumProperty(name='Shape', items=[(v,v.title(),'') for v in ('box','sphere','capsule')])
    size: FloatVectorProperty(name='Box Dimensions', size=3, default=(1,1,1), min=0.001, subtype='XYZ', unit='LENGTH')
    offset: FloatVectorProperty(name='Local Offset', size=3, default=(0,0,0), subtype='TRANSLATION', unit='LENGTH')
    radius: FloatProperty(name='Radius', default=0.35, min=0.001, unit='LENGTH')
    height: FloatProperty(name='Total Capsule Height', default=1.8, min=0.003, unit='LENGTH')
    mass: FloatProperty(name='Body Mass (0 = automatic)', default=0, min=0)
    friction: FloatProperty(name='Friction', default=0.5, min=0)
    restitution: FloatProperty(name='Restitution', default=0, min=0, max=1)
    linear_damping: FloatProperty(name='Linear Damping', default=0.05, min=0)
    angular_damping: FloatProperty(name='Angular Damping', default=0.05, min=0)
    gravity: FloatProperty(name='Gravity Factor', default=1)
    lock_rotation: BoolProperty(name='Lock Rotation')
    sensor: BoolProperty(name='Sensor')
    slope: FloatProperty(name='Maximum Slope (degrees)', default=45, min=0, max=89.9)
    step: FloatProperty(name='Step Height', default=0.3, min=0, unit='LENGTH')
    character_mass: FloatProperty(name='Character Mass', default=70, min=0.001)
    strength: FloatProperty(name='Maximum Push Strength', default=200, min=0)
    local_player: BoolProperty(name='Locally Controlled Player')
    speed: FloatProperty(name='Movement Speed (m/s)', default=5, min=0)
    preview: BoolProperty(name='Show Collider', default=True)


class HakiProject(bpy.types.PropertyGroup):
    root: StringProperty(name='Haki Project', subtype='DIR_PATH')
    scene_directory: StringProperty(name='Scene Directory',subtype='DIR_PATH',description='Defaults to Assets/Game/Scenes/<Scene ID>')
    default_skybox: BoolProperty(name='Use Engine Default Skybox',default=True,
        description='Use the engine HDR skybox and image-based lighting; disable with no custom asset to opt out')
    skybox_asset: StringProperty(name='Scene Skybox Asset',description='Optional texture asset ID; takes precedence over engine default')
    scene_script: StringProperty(name='Scene Script',subtype='FILE_PATH',description='Relative to Assets/Scripts; blank means no scene script')
    python: StringProperty(name='Project Python', subtype='FILE_PATH', description='Use the project .venv/bin/python, not Blender Python')
    engine: StringProperty(name='Engine Executable', subtype='FILE_PATH')
    output: StringProperty(name='Export GLB', subtype='FILE_PATH', default='//exports/arena.glb')
    pack: StringProperty(name='Pack', default='arena-01')
    scene_id: StringProperty(name='Scene ID', default='arena-01')
    set_entry: BoolProperty(name='Make Startup Scene', default=True)
    import_after_export: BoolProperty(name='Import into Haki', default=True, description='Disable to export GLB only, then import from a host terminal')


def scene_directory(project):
    root = Path(bpy.path.abspath(project.root))
    from re import sub
    scene = sub(r'[^a-z0-9]+','-',project.scene_id.lower()).strip('-')
    if not scene:
        raise ValueError('Scene ID must contain letters or numbers')
    expected = (root/'Assets/Game/Scenes'/scene).resolve()
    if project.scene_directory and Path(bpy.path.abspath(project.scene_directory)).resolve() != expected:
        raise ValueError(f'Scene Directory must match Scene ID: {expected}')
    return expected


def script_path_for(path):
    if not path:
        raise ValueError('Choose a .lua script inside scene Scripts or Game/Shared/Scripts')
    project = bpy.context.scene.haki_project
    game = Path(bpy.path.abspath(project.root))/'Assets/Game'
    modern = (game/'Project.json').exists()
    root = (scene_directory(project)/'Scripts' if modern else game.parent/'Scripts').resolve()
    shared = (game/'Shared/Scripts').resolve()
    if path.startswith('//') or Path(path).is_absolute():
        resolved = Path(bpy.path.abspath(path)).resolve()
        if modern and resolved.is_relative_to(shared):
            path = 'shared/'+resolved.relative_to(shared).as_posix()
        elif resolved.is_relative_to(root):
            path = resolved.relative_to(root).as_posix()
        else:
            raise ValueError('Lua script must be in this scene Scripts or Game/Shared/Scripts')
    validate_script_path(path)
    base = shared if modern and path.startswith('shared/') else root
    resolved = (base/(path[7:] if base == shared else path)).resolve()
    if not resolved.is_relative_to(base) or not resolved.is_file():
        raise ValueError(f'Script not found inside scene Scripts or Game/Shared/Scripts: {path}')
    return path


def metadata_for(obj):
    h = obj.haki_settings
    components = {}
    if h.mode == 'BODY':
        body = dict(motion=h.motion, mass=h.mass, friction=h.friction, restitution=h.restitution,
                    linearDamping=h.linear_damping, angularDamping=h.angular_damping,
                    gravityFactor=h.gravity, lockRotation=h.lock_rotation, sensor=h.sensor)
        if h.layer != 'auto':
            body['layer'] = h.layer
        # Blender (X,Y,Z) -> glTF (X,Z,-Y); dimensions have no signed direction.
        collider = dict(shape=h.shape, offset=[h.offset[0],h.offset[2],-h.offset[1]])
        if h.shape == 'box':
            collider['halfExtents'] = [h.size[0]/2,h.size[2]/2,h.size[1]/2]
        else:
            collider['radius'] = h.radius
            if h.shape == 'capsule':
                collider['halfHeight'] = h.height/2-h.radius
        components.update(rigidBody=body, collider=collider)
    elif h.mode == 'CHARACTER':
        components['characterController'] = dict(radius=h.radius, halfHeight=h.height/2-h.radius,
            maxSlopeDegrees=h.slope, stepHeight=h.step, mass=h.character_mass,
            maxStrength=h.strength, gravityFactor=h.gravity)
        if h.local_player:
            components['playerInput'] = dict(moveSpeed=h.speed)
    elif h.mode == 'VISUAL':
        target = h.visual_parent
        if target is None or target == obj or target.haki_settings.mode == 'NONE':
            raise ValueError('Choose a different authored object as Runtime Parent')
        from mathutils import Matrix
        basis = Matrix(((1,0,0,0),(0,0,1,0),(0,-1,0,0),(0,0,0,1)))
        local = basis @ (target.matrix_world.inverted() @ obj.matrix_world) @ basis.inverted()
        validate_transform([local[r][c] for c in range(4) for r in range(4)], {})
        translation, rotation, scale = local.decompose()
        components['parent'] = dict(entity=ensure_id(target), local=dict(translation=list(translation),
            rotation=[rotation.x,rotation.y,rotation.z,rotation.w],scale=list(scale)))
        if h.locomotion and h.use_blend:
            if h.run_speed <= h.walk_speed:
                raise ValueError('Run speed must be greater than walk speed')
            components['locomotion'] = dict(blend=[dict(clip=h.idle_clip,speed=0.0),
                dict(clip=h.walk_clip,speed=h.walk_speed),dict(clip=h.move_clip,speed=h.run_speed)])
        elif h.locomotion:
            components['locomotion'] = dict(idleClip=h.idle_clip,moveClip=h.move_clip,
                                           speedThreshold=h.speed_threshold)
    if h.script_enabled:
        properties = {}
        for prop in h.script_properties:
            if not prop.name.strip() or prop.name in properties:
                raise ValueError('Script property names must be nonempty and unique')
            properties[prop.name] = list(prop.vector) if prop.kind == 'vector' else getattr(prop,prop.kind)
        components['script'] = dict(path=script_path_for(h.script_path),properties=properties)
    return validate_metadata(dict(version=1, id=ensure_id(obj), components=components))


def world_matrix_gltf(obj):
    from mathutils import Matrix
    basis = Matrix(((1,0,0,0),(0,0,1,0),(0,-1,0,0),(0,0,0,1)))
    m = basis @ obj.matrix_world @ basis.inverted()
    return [m[r][c] for c in range(4) for r in range(4)]


def validate_scene(context):
    errors = []
    seen = set()
    local_players = 0
    if (Path(bpy.path.abspath(context.scene.haki_project.root))/'Assets/Game/Project.json').exists():
        try:
            scene_directory(context.scene.haki_project)
        except ValueError as error:
            errors.append(str(error))
    if context.scene.unit_settings.scale_length != 1:
        errors.append('Scene Unit Scale must be 1.0; author dimensions in metres')
    for obj in context.scene.objects:
        # Collection instances can hide authored objects from scene.objects.
        if obj.instance_type == 'COLLECTION' and obj.instance_collection:
            if any(authored(o) for o in obj.instance_collection.all_objects):
                errors.append(f'{obj.name}: make physics collection instances real before export, then repair IDs')
        if not authored(obj):
            continue
        try:
            if obj.library:
                raise ValueError('make linked objects local before authoring physics')
            if obj.haki_settings.mode in {'BODY','CHARACTER','VISUAL'} and obj.type not in {'MESH','EMPTY'}:
                raise ValueError('physics authoring supports Mesh or Empty objects; use an Empty character marker')
            if not obj.visible_get():
                raise ValueError('physics object is hidden/excluded from the active view layer')
            metadata = metadata_for(obj)
            if obj.haki_settings.mode == 'VISUAL':
                if obj.type != 'MESH':
                    raise ValueError('Attached Visual must be a mesh; select the skinned mesh, not the armature')
                target = obj.haki_settings.visual_parent
                if target.name not in context.scene.objects or not target.visible_get():
                    raise ValueError('Runtime Parent must be visible in the exported scene')
                chain = {obj.name}
                while target.haki_settings.mode == 'VISUAL':
                    if target.name in chain:
                        raise ValueError('Runtime Parent cycle')
                    chain.add(target.name)
                    target = target.haki_settings.visual_parent
                    if target is None:
                        raise ValueError('Runtime Parent chain is incomplete')
                if obj.haki_settings.locomotion and target.haki_settings.mode != 'CHARACTER':
                    raise ValueError('Locomotion needs a Character Controller ancestor')
            if metadata['id'] in seen:
                raise ValueError('duplicate stable ID; use Repair Duplicate IDs')
            seen.add(metadata['id'])
            validate_transform(world_matrix_gltf(obj), metadata['components'])
            if obj.haki_settings.mode == 'CHARACTER' and obj.haki_settings.local_player:
                local_players += 1
        except (ValueError, MetadataError) as error:
            errors.append(f'{obj.name}: {error}')
    if local_players > 1:
        errors.append('Only one character may be locally controlled in this prototype scene')
    if context.scene.haki_project.scene_script:
        try:
            script_path_for(context.scene.haki_project.scene_script)
        except ValueError as error:
            errors.append(f'Scene script: {error}')
    return errors


def report_errors(operator, errors):
    for error in errors:
        print('Haki:', error)
    operator.report({'ERROR'}, '\n'.join(errors))


class HAKI_OT_validate(bpy.types.Operator):
    bl_idname = 'haki.validate'
    bl_label = 'Validate Arena'
    def execute(self, context):
        errors = validate_scene(context)
        if errors:
            report_errors(self, errors)
            return {'CANCELLED'}
        self.report({'INFO'}, 'Arena metadata and physics transforms are valid')
        return {'FINISHED'}


class HAKI_OT_repair_ids(bpy.types.Operator):
    bl_idname = 'haki.repair_ids'
    bl_label = 'Repair Missing / Duplicate IDs'
    bl_options = {'REGISTER','UNDO'}
    def execute(self, context):
        seen = set()
        for obj in sorted(context.scene.objects, key=lambda o:o.name):
            if not authored(obj) or obj.library:
                continue
            if not obj.haki_settings.stable_id or obj.haki_settings.stable_id in seen:
                obj.haki_settings.stable_id = str(uuid.uuid4())
            seen.add(obj.haki_settings.stable_id)
        return {'FINISHED'}


class HAKI_OT_add_marker(bpy.types.Operator):
    bl_idname = 'haki.add_marker'
    bl_label = 'Add Invisible Collider'
    bl_options = {'REGISTER','UNDO'}
    character: BoolProperty(default=False)
    def execute(self, context):
        bpy.ops.object.empty_add(type='PLAIN_AXES', location=context.scene.cursor.location)
        obj = context.object
        obj.name = 'Player' if self.character else 'Collision'
        obj.haki_settings.mode = 'CHARACTER' if self.character else 'BODY'
        obj.haki_settings.local_player = self.character
        return {'FINISHED'}


class HAKI_OT_fit(bpy.types.Operator):
    bl_idname = 'haki.fit'
    bl_label = 'Fit to Mesh Bounds'
    bl_options = {'REGISTER','UNDO'}
    @classmethod
    def poll(cls, context):
        return context.object is not None and context.object.type == 'MESH'
    def execute(self, context):
        obj = context.object
        evaluated = obj.evaluated_get(context.evaluated_depsgraph_get())
        points = [Vector(p) for p in evaluated.bound_box]
        lo = Vector([min(p[i] for p in points) for i in range(3)])
        hi = Vector([max(p[i] for p in points) for i in range(3)])
        h = obj.haki_settings
        h.size = tuple(max(hi[i]-lo[i],0.001) for i in range(3))
        h.offset = (lo+hi)/2
        h.radius = Vector(h.size).length/2 if h.shape == 'sphere' else math.hypot(h.size[0],h.size[1])/2
        h.height = h.size[2]+2*h.radius
        if h.mode == 'CHARACTER':
            self.report({'INFO'}, 'Character origin must be at feet; controller ignores collider offset')
        return {'FINISHED'}


def run_import(project, glb, dry_run=False):
    root = Path(bpy.path.abspath(project.root))
    python = bpy.path.abspath(project.python) if project.python else str(root/'.venv/bin/python')
    script = root/'tools/import_gltf_pack.py'
    if not script.is_file():
        raise ValueError('Select the Haki project folder containing tools/import_gltf_pack.py')
    args = [python,'-E',str(script),str(glb),'--pack',project.pack,'--scene',project.scene_id,
            '--project-root',str(root)]
    if project.set_entry:
        args.append('--set-entry')
    if dry_run:
        args.append('--dry-run')
    command, environment = command_environment(args, root)
    try:
        result = subprocess.run(command,cwd=root,env=environment,capture_output=True,text=True)
    except OSError as error:
        raise ValueError(failure_message(command,str(error))) from error
    print(result.stdout, result.stderr)
    if result.returncode:
        raise ValueError(failure_message(command,result.stderr or result.stdout))


class HAKI_OT_export(bpy.types.Operator):
    bl_idname = 'haki.export'
    bl_label = 'Export Arena'
    play: BoolProperty(default=False)
    def execute(self, context):
        errors = validate_scene(context)
        if errors:
            report_errors(self,errors)
            return {'CANCELLED'}
        project = context.scene.haki_project
        modern = (Path(bpy.path.abspath(project.root))/'Assets/Game/Project.json').exists()
        destination = scene_directory(project)/'Models/arena.glb' if modern else Path(bpy.path.abspath(project.output))
        if destination.suffix.lower() != '.glb':
            self.report({'ERROR'}, 'Export path must end in .glb')
            return {'CANCELLED'}
        previous = []
        source_scene = context.scene
        had_scene_meta = 'haki' in source_scene
        scene_meta = source_scene.get('haki')
        scene_meta = scene_meta.to_dict() if hasattr(scene_meta,'to_dict') else scene_meta
        try:
            if self.play and not project.import_after_export:
                raise ValueError('Enable Import into Haki before Export and Play')
            engine = Path(bpy.path.abspath(project.engine))
            if self.play and not engine.is_file():
                raise ValueError('Choose a built Engine executable before Export and Play')
            for obj in context.scene.objects:
                previous.append((obj, 'haki' in obj, obj.get('haki').to_dict() if hasattr(obj.get('haki'),'to_dict') else obj.get('haki')))
                if authored(obj):
                    obj['haki'] = metadata_for(obj)
                elif 'haki' in obj:
                    del obj['haki']
            source_scene['haki'] = dict(version=1,script=script_path_for(project.scene_script) if project.scene_script else '')
            if modern:
                texture = project.skybox_asset or ('engine.texture.environment.qwantani-dusk' if project.default_skybox else '')
                source_scene['haki']['environment'] = dict(texture=texture,intensity=1.0,rotationDegrees=0.0) if texture else None
            destination.parent.mkdir(parents=True,exist_ok=True)
            # Stage beside destination so asset IDs/file selectors match the final export name.
            with tempfile.TemporaryDirectory(prefix='.haki_settings-',dir=destination.parent) as stage:
                glb = Path(stage)/destination.name
                result = bpy.ops.export_scene.gltf(filepath=str(glb),export_format='GLB',
                    export_extras=True,export_yup=True,export_cameras=True,export_lights=True,
                    export_import_convert_lighting_mode='RAW',
                    use_selection=False,use_visible=True,export_apply=True)
                if 'FINISHED' not in result:
                    raise ValueError('Blender GLB export did not finish')
                if project.import_after_export:
                    run_import(project,glb,dry_run=True)
                    run_import(project,glb)
                if not (modern and project.import_after_export):
                    os.replace(glb,destination)
            if self.play:
                # Use source-root Assets rather than a potentially stale build/Assets copy.
                command, environment = command_environment([str(engine)],bpy.path.abspath(project.root))
                subprocess.Popen(command,cwd=bpy.path.abspath(project.root),env=environment)
            self.report({'INFO'}, 'Exported and imported Haki arena' if project.import_after_export else 'Exported GLB; run the Haki importer from a host terminal')
        except (OSError,ValueError,RuntimeError) as error:
            self.report({'ERROR'},str(error))
            return {'CANCELLED'}
        finally:
            if had_scene_meta:
                source_scene['haki'] = scene_meta
            elif 'haki' in source_scene:
                del source_scene['haki']
            for obj,had,value in previous:
                if had:
                    obj['haki'] = value
                elif 'haki' in obj:
                    del obj['haki']
        return {'FINISHED'}


class HAKI_OT_script_property(bpy.types.Operator):
    bl_idname = 'haki.script_property'
    bl_label = 'Edit Script Property'
    bl_options = {'REGISTER','UNDO'}
    index: IntProperty(default=-1)
    def execute(self,context):
        if not context.object:
            return {'CANCELLED'}
        properties = context.object.haki_settings.script_properties
        if self.index < 0:
            item = properties.add()
            item.name = 'property_'+str(len(properties))
        elif self.index < len(properties):
            properties.remove(self.index)
        return {'FINISHED'}


class HAKI_PT_panel(bpy.types.Panel):
    bl_label = 'Haki Arena'
    bl_idname = 'HAKI_PT_arena'
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_category = 'Haki'
    def draw(self, context):
        layout = self.layout
        p = context.scene.haki_project
        project = layout.box()
        project.label(text='Project and Export')
        for name in ('root','python','engine','output','pack','scene_id','scene_directory','set_entry','import_after_export'):
            project.prop(p,name)
        layout.prop(p,'scene_script')
        layout.prop(p,'default_skybox')
        layout.prop(p,'skybox_asset')
        row = layout.row()
        row.operator('haki.add_marker',text='Add Collider')
        row.operator('haki.add_marker',text='Add Player').character = True
        obj = context.object
        if obj:
            h = obj.haki_settings
            box = layout.box()
            box.label(text=obj.name)
            box.prop(h,'mode')
            if h.mode != 'NONE':
                box.label(text='ID: '+(h.stable_id[:16] or '(assigned on export)'))
                box.prop(h,'preview')
                if h.mode == 'BODY':
                    for name in ('motion','layer','shape'):
                        box.prop(h,name)
                    if obj.type == 'MESH':
                        box.operator('haki.fit')
                    if h.shape == 'box':
                        box.prop(h,'size')
                    else:
                        box.prop(h,'radius')
                        if h.shape == 'capsule':
                            box.prop(h,'height')
                    box.prop(h,'offset')
                    for name in ('mass','friction','restitution','linear_damping','angular_damping',
                                 'gravity','lock_rotation','sensor'):
                        box.prop(h,name)
                elif h.mode == 'VISUAL':
                    box.prop(h,'visual_parent')
                    box.prop(h,'locomotion')
                    if h.locomotion:
                        box.prop(h,'idle_clip')
                        box.prop(h,'move_clip')
                        box.prop(h,'use_blend')
                        if h.use_blend:
                            box.prop(h,'walk_clip')
                            box.prop(h,'walk_speed')
                            box.prop(h,'run_speed')
                        else:
                            box.prop(h,'speed_threshold')
                else:
                    box.label(text='Origin at feet; unit scale; upright')
                    for name in ('radius','height','slope','step','character_mass','strength','gravity','local_player'):
                        box.prop(h,name)
                    if h.local_player:
                        box.prop(h,'speed')
            script_box = layout.box()
            script_box.prop(h,'script_enabled')
            if h.script_enabled:
                script_box.prop(h,'script_path')
                script_box.label(text='Paths relative to scene Scripts; shared/ for shared scripts')
                for i,prop in enumerate(h.script_properties):
                    row = script_box.row(align=True)
                    row.prop(prop,'name',text='')
                    row.prop(prop,'kind',text='')
                    row.operator('haki.script_property',text='',icon='X').index = i
                    script_box.prop(prop,prop.kind)
                script_box.operator('haki.script_property',text='Add Property Override',icon='ADD')
        layout.operator('haki.repair_ids')
        layout.operator('haki.validate')
        layout.operator('haki.export')
        layout.operator('haki.export',text='Export and Play').play = True


def preview_lines(obj):
    h = obj.haki_settings
    if h.mode == 'CHARACTER':
        shape, center = 'capsule', Vector((0,0,h.height/2))
    else:
        shape, center = h.shape, Vector(h.offset)
    lines = []
    def segment(a,b):
        lines.extend((obj.matrix_world @ (Vector(a)+center),obj.matrix_world @ (Vector(b)+center)))
    if shape == 'box':
        half = Vector(h.size)/2
        corners = [Vector((x*half.x,y*half.y,z*half.z)) for x in (-1,1) for y in (-1,1) for z in (-1,1)]
        for i in range(8):
            for bit in (1,2,4):
                if not i & bit:
                    segment(corners[i],corners[i|bit])
    else:
        radius = h.radius
        cylinder_half = max(0,h.height/2-radius) if shape == 'capsule' else 0
        for z in set((-cylinder_half,cylinder_half)):
            for i in range(48):
                a,b = i*math.tau/48,(i+1)*math.tau/48
                segment((radius*math.cos(a),radius*math.sin(a),z),(radius*math.cos(b),radius*math.sin(b),z))
        for axis in (0,1):
            for i in range(48):
                pts=[]
                for a in (i*math.tau/48,(i+1)*math.tau/48):
                    pt=[0,0,radius*math.sin(a)+(cylinder_half if math.sin(a)>=0 else -cylinder_half)]
                    pt[axis]=radius*math.cos(a)
                    pts.append(pt)
                segment(*pts)
            if cylinder_half:
                for sign in (-1,1):
                    a,b=[0,0,-cylinder_half],[0,0,cylinder_half]
                    a[axis]=b[axis]=sign*radius
                    segment(a,b)
    return lines


def draw_previews():
    import gpu
    from gpu_extras.batch import batch_for_shader
    if not bpy.context.scene:
        return
    shader = gpu.shader.from_builtin('UNIFORM_COLOR')
    try:
        gpu.state.blend_set('ALPHA')
        gpu.state.depth_test_set('NONE')
        for obj in bpy.context.scene.objects:
            if obj.haki_settings.mode in {'NONE','VISUAL'} or not obj.haki_settings.preview or not obj.visible_get():
                continue
            lines = preview_lines(obj)
            color = (0.3,0.8,1,0.9) if obj.haki_settings.mode == 'CHARACTER' else (1,0.3,0.8,0.9) if obj.haki_settings.sensor else (0.3,1,0.4,0.9)
            shader.bind()
            shader.uniform_float('color',color)
            batch_for_shader(shader,'LINES',{'pos':lines}).draw(shader)
    finally:
        gpu.state.depth_test_set('NONE')
        gpu.state.blend_set('NONE')


CLASSES = (HakiScriptProperty,HakiObject,HakiProject,HAKI_OT_script_property,HAKI_OT_validate,HAKI_OT_repair_ids,HAKI_OT_add_marker,
           HAKI_OT_fit,HAKI_OT_export,HAKI_PT_panel)
_handle = None


def register():
    global _handle
    for cls in CLASSES:
        bpy.utils.register_class(cls)
    bpy.types.Object.haki_settings = PointerProperty(type=HakiObject)
    bpy.types.Scene.haki_project = PointerProperty(type=HakiProject)
    _handle = bpy.types.SpaceView3D.draw_handler_add(draw_previews,(),'WINDOW','POST_VIEW')


def unregister():
    global _handle
    if _handle is not None:
        bpy.types.SpaceView3D.draw_handler_remove(_handle,'WINDOW')
        _handle = None
    del bpy.types.Scene.haki_project
    del bpy.types.Object.haki_settings
    for cls in reversed(CLASSES):
        bpy.utils.unregister_class(cls)
