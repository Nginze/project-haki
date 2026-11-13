"""Run in Blender after tools/migrate_assets.py --apply to update saved arena settings."""
import bpy,sys,os
from pathlib import Path
root=Path(__file__).resolve().parents[2];sys.path.insert(0,str(root/'tools/blender'))
import haki_editor
haki_editor.register()
scene=root/'Assets/Game/Scenes/arena-01'
bpy.ops.wm.open_mainfile(filepath=str(scene/'arena-01.blend'))
p=bpy.context.scene.haki_project
p.root=str(root);p.scene_id='arena-01';p.pack='arena-01';p.scene_directory=str(scene);p.output=str(scene/'Models/arena.glb')
# Saved file pickers may still contain old absolute paths.
def remap_script(value):
 prefix=str(root/'Assets/Scripts')+'/'
 if value.startswith(prefix):return value.removeprefix(prefix)
 return value
p.scene_script=remap_script(p.scene_script)
for obj in bpy.context.scene.objects:
 obj.haki_settings.script_path=remap_script(obj.haki_settings.script_path)
for image in bpy.data.images:
 if image.packed_file or not image.filepath:continue
 if image.filepath.startswith(str(root/'Assets/Imported/arena-01/Source')):
  image.filepath=image.filepath.replace(str(root/'Assets/Imported/arena-01/Source'),str(scene/'Models'))
bpy.ops.wm.save_as_mainfile(filepath=str(scene/'arena-01.blend'))
print('Migrated Blender settings',p.scene_script,flush=True)
sys.stdout.flush();os._exit(0)
