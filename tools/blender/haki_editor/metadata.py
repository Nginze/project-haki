"""Version 1 Haki glTF extras contract. No Blender or third-party dependencies.

Components use Haki/glTF Y-up local axes, metres, and unscaled dimensions.
The exporter converts Blender Z-up authoring values exactly once.
"""
import copy
import math


class MetadataError(ValueError):
    pass


def number(value, label, minimum=None, maximum=None, exclusive=False):
    if isinstance(value, bool) or not isinstance(value, (float, int)) or not math.isfinite(value):
        raise MetadataError(f"{label}: expected a finite number")
    if minimum is not None and (value <= minimum if exclusive else value < minimum):
        raise MetadataError(f"{label}: must be {'>' if exclusive else '>='} {minimum}")
    if maximum is not None and value > maximum:
        raise MetadataError(f"{label}: must be <= {maximum}")
    return value


def fields(value, allowed, label):
    if not isinstance(value, dict):
        raise MetadataError(f"{label}: expected an object")
    unknown = set(value) - set(allowed)
    if unknown:
        raise MetadataError(f"{label}: unsupported fields {sorted(unknown)}")


def validate_metadata(value):
    fields(value, {'version', 'id', 'components'}, 'haki')
    if type(value.get('version')) is not int or value['version'] != 1:
        raise MetadataError('haki.version: only version 1 is supported')
    identity = value.get('id')
    if not isinstance(identity, str) or not identity.strip() or len(identity) > 128:
        raise MetadataError('haki.id: expected a nonempty stable ID, at most 128 characters')
    components = value.get('components')
    fields(components, {'rigidBody', 'collider', 'characterController', 'playerInput', 'parent', 'locomotion', 'script'}, 'haki.components')
    if ('rigidBody' in components) != ('collider' in components):
        raise MetadataError('rigidBody and collider must be authored together')
    if 'characterController' in components and 'rigidBody' in components:
        raise MetadataError('characterController cannot have a rigidBody/collider')
    if 'playerInput' in components and 'characterController' not in components:
        raise MetadataError('playerInput requires characterController')
    if 'parent' in components:
        if 'rigidBody' in components or 'characterController' in components:
            raise MetadataError('Physics entities must be world roots')
        parent = components['parent']
        fields(parent, {'entity', 'local'}, 'parent')
        if not isinstance(parent.get('entity'), str) or not parent['entity'].strip():
            raise MetadataError('parent.entity: expected stable parent ID')
        local = parent.get('local')
        fields(local, {'translation','rotation','scale'}, 'parent.local')
        for name, count in (('translation',3),('rotation',4),('scale',3)):
            v = local.get(name)
            if not isinstance(v,(list,tuple)) or len(v) != count:
                raise MetadataError(f'parent.local.{name}: expected {count} numbers')
            for x in v:
                number(x,f'parent.local.{name}',0 if name=='scale' else None,exclusive=name=='scale')
        if sum(x*x for x in local['rotation']) < 1e-8:
            raise MetadataError('parent.local.rotation: quaternion cannot be zero')
    if 'locomotion' in components:
        l = components['locomotion']
        fields(l, {'idleClip','moveClip','speedThreshold','blend','fade','smoothing','maxPlaybackScale'}, 'locomotion')
        if 'blend' in l:
            # Speed blend: [{"clip": "Idle", "speed": 0}, {"clip": "Run", "speed": 4.5}, ...]
            if any(name in l for name in ('idleClip','moveClip','speedThreshold')):
                raise MetadataError('locomotion: use either blend or idleClip/moveClip, not both')
            if not isinstance(l['blend'], list) or not l['blend']:
                raise MetadataError('locomotion.blend: expected a non-empty list of {clip, speed}')
            previous = None
            for index, sample in enumerate(l['blend']):
                label = f'locomotion.blend[{index}]'
                fields(sample, {'clip','speed'}, label)
                if not isinstance(sample.get('clip'),str) or not sample['clip'].strip():
                    raise MetadataError(f'{label}.clip: expected clip name')
                speed = number(sample.get('speed'), f'{label}.speed', 0)
                if previous is not None and speed <= previous:
                    raise MetadataError(f'{label}.speed: speeds must be strictly increasing')
                previous = speed
        else:
            for name in ('idleClip','moveClip'):
                if not isinstance(l.get(name),str) or not l[name].strip():
                    raise MetadataError(f'locomotion.{name}: expected clip name')
            if 'speedThreshold' in l:
                number(l['speedThreshold'],'locomotion.speedThreshold',0)
        for name, minimum in (('fade',0),('smoothing',0),('maxPlaybackScale',1)):
            if name in l:
                number(l[name], f'locomotion.{name}', minimum)
    if 'rigidBody' in components:
        b = components['rigidBody']
        fields(b, {'motion', 'layer', 'mass', 'friction', 'restitution', 'linearDamping',
                   'angularDamping', 'gravityFactor', 'lockRotation', 'sensor'}, 'rigidBody')
        if b.get('motion', 'static') not in ('static', 'kinematic', 'dynamic'):
            raise MetadataError('rigidBody.motion: invalid motion type')
        layer = b.get('layer', 'trigger' if b.get('sensor') else
                      'static' if b.get('motion', 'static') == 'static' else 'dynamic')
        if layer not in ('static', 'dynamic', 'character', 'trigger'):
            raise MetadataError('rigidBody.layer: invalid collision layer')
        if layer == 'static' and b.get('motion', 'static') != 'static':
            raise MetadataError('Only static bodies may use the static layer')
        for name in ('mass', 'friction', 'linearDamping', 'angularDamping'):
            if name in b:
                number(b[name], f'rigidBody.{name}', 0)
        if 'restitution' in b:
            number(b['restitution'], 'rigidBody.restitution', 0, 1)
        if 'gravityFactor' in b:
            number(b['gravityFactor'], 'rigidBody.gravityFactor')
        for name in ('lockRotation', 'sensor'):
            if name in b and type(b[name]) is not bool:
                raise MetadataError(f'rigidBody.{name}: expected boolean')
        c = components['collider']
        fields(c, {'shape', 'halfExtents', 'offset', 'radius', 'halfHeight'}, 'collider')
        if c.get('shape', 'box') not in ('box', 'sphere', 'capsule'):
            raise MetadataError('collider.shape: invalid shape')
        for name in ('halfExtents', 'offset'):
            if name in c:
                v = c[name]
                if not isinstance(v, (list, tuple)) or len(v) != 3:
                    raise MetadataError(f'collider.{name}: expected three numbers')
                for x in v:
                    number(x, f'collider.{name}', 0 if name == 'halfExtents' else None,
                           exclusive=name == 'halfExtents')
        for name in ('radius', 'halfHeight'):
            if name in c:
                number(c[name], f'collider.{name}', 0, exclusive=True)
    if 'characterController' in components:
        c = components['characterController']
        fields(c, {'radius', 'halfHeight', 'maxSlopeDegrees', 'stepHeight', 'mass',
                   'maxStrength', 'gravityFactor'}, 'characterController')
        for name in ('radius', 'halfHeight', 'mass'):
            if name in c:
                number(c[name], f'characterController.{name}', 0, exclusive=True)
        for name in ('stepHeight', 'maxStrength'):
            if name in c:
                number(c[name], f'characterController.{name}', 0)
        if 'gravityFactor' in c:
            number(c['gravityFactor'], 'characterController.gravityFactor')
        if 'maxSlopeDegrees' in c:
            number(c['maxSlopeDegrees'], 'characterController.maxSlopeDegrees', 0)
            if c['maxSlopeDegrees'] >= 90:
                raise MetadataError('characterController.maxSlopeDegrees: must be < 90')
    if 'playerInput' in components:
        fields(components['playerInput'], {'moveSpeed'}, 'playerInput')
        if 'moveSpeed' in components['playerInput']:
            number(components['playerInput']['moveSpeed'], 'playerInput.moveSpeed', 0)
    if 'script' in components:
        validate_script(components['script'])
    return copy.deepcopy(value)


def validate_transform(matrix, components):
    """Validate flattened column-major glTF world transform before TRS decomposition."""
    if len(matrix) != 16 or any(not math.isfinite(v) for v in matrix):
        raise MetadataError('Physics transform must contain finite values')
    cols = [[matrix[4 * i + j] for j in range(3)] for i in range(3)]
    scales = [math.sqrt(sum(x*x for x in col)) for col in cols]
    if min(scales) < 1e-6:
        raise MetadataError('Physics transform has zero scale')
    axes = [[x/s for x in col] for col, s in zip(cols, scales)]
    if any(abs(sum(a*b for a,b in zip(axes[i], axes[j]))) > 1e-5
           for i,j in ((0,1), (0,2), (1,2))):
        raise MetadataError('Physics transform contains shear; apply parent transforms')
    a,b,c = axes
    det = a[0]*(b[1]*c[2]-b[2]*c[1])-a[1]*(b[0]*c[2]-b[2]*c[0])+a[2]*(b[0]*c[1]-b[1]*c[0])
    if det < 0:
        raise MetadataError('Mirrored physics transforms are unsupported; apply scale')
    if 'characterController' in components:
        if any(abs(s-1) > 1e-5 for s in scales):
            raise MetadataError('Character controller needs unit world scale; apply scale')
        # Runtime CharacterVirtual capsules remain upright regardless of entity orientation.
        if abs(axes[1][1]-1) > 1e-5:
            raise MetadataError('Character controller must be upright (yaw rotation only)')
    shape = components.get('collider', {}).get('shape', 'box')
    if shape == 'sphere' and max(scales)-min(scales) > 1e-5:
        raise MetadataError('Sphere collider needs uniform world scale; apply scale')
    if shape == 'capsule' and abs(scales[0]-scales[2]) > 1e-5:
        raise MetadataError('Capsule collider needs equal radial scale; apply scale')


def validate_script_path(path):
    """Portable, scripts-root-relative Lua path (also used outside Blender)."""
    from pathlib import PurePosixPath
    if not isinstance(path,str) or not path or '\\' in path or ':' in path:
        raise MetadataError('Script path must be relative to the scene Scripts folder, with forward slashes')
    parts = PurePosixPath(path)
    if parts.is_absolute() or '..' in parts.parts or parts.suffix != '.lua':
        raise MetadataError('Script path must be relative to the scene Scripts folder and end in .lua')
    return path


def validate_script(script):
    fields(script, {'path','properties'}, 'script')
    validate_script_path(script.get('path'))
    properties = script.get('properties',{})
    if not isinstance(properties,dict):
        raise MetadataError('script.properties must be an object')
    for name,value in properties.items():
        if not isinstance(name,str) or not name.strip():
            raise MetadataError('Script property names must not be empty')
        if isinstance(value,(bool,str)):
            continue
        if isinstance(value,(int,float)):
            number(value, f'script.properties.{name}')
        elif isinstance(value,(list,tuple)) and len(value)==3:
            for v in value:
                number(v,f'script.properties.{name}')
        else:
            raise MetadataError(f'script.properties.{name}: expected number, bool, string or vec3')
    return copy.deepcopy(script)


def validate_scene_metadata(value):
    fields(value, {'version','script','environment'}, 'scene.haki')
    if type(value.get('version')) is not int or value['version'] != 1:
        raise MetadataError('scene.haki.version: only version 1 is supported')
    environment = value.get('environment')
    if environment is not None:
        fields(environment, {'texture','intensity','rotationDegrees'}, 'scene.environment')
        if not isinstance(environment.get('texture'),str) or not environment['texture']:
            raise MetadataError('Environment needs a texture asset ID')
        number(environment.get('intensity',1),'environment.intensity',0)
        number(environment.get('rotationDegrees',0),'environment.rotationDegrees')
    script = value.get('script','')
    if script:
        validate_script_path(script)
    elif not isinstance(script,str):
        raise MetadataError('scene.haki.script: expected string')
    return copy.deepcopy(value)
