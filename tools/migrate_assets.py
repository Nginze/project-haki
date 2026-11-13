#!/usr/bin/env python3
"""One-time legacy asset migration. Dry-run by default; originals retained in Archive."""
import argparse,json,shutil,os
from pathlib import Path

def write(p,d):
 p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(d,indent=2)+'\n')
def main():
 parser=argparse.ArgumentParser();parser.add_argument('--apply',action='store_true');args=parser.parse_args()
 root=Path(__file__).resolve().parents[1];assets=root/'Assets';archive=root/'Archive/Experiments/Assets'
 if (assets/'Game/Project.json').exists(): print('Already migrated');return
 print('Archive all legacy assets; migrate active arena, engine defaults and game scripts.')
 if not args.apply:return
 if archive.exists():raise RuntimeError('Archive destination already exists')
 archive.parent.mkdir(parents=True,exist_ok=True);assets.rename(archive);assets.mkdir()
 catalog=json.loads((archive/'AssetCatalog.json').read_text())
 engine=assets/'Engine';scene=assets/'Game/Scenes/arena-01';shared=assets/'Game/Shared'
 for src,dst in [('Shaders','Shaders'),('Fonts','Fonts'),('HDR','Skyboxes'),('Engine/Primitives','Geometry')]:
  shutil.copytree(archive/src,engine/dst,dirs_exist_ok=True)
 (engine/'Materials').mkdir()
 for file in (engine/'Geometry').glob('*.material.json'):file.rename(engine/'Materials'/file.name)
 engine_entries=[];scene_entries=[]
 for e in catalog['assets']:
  e=dict(e);old=e['source']
  if e['id'].startswith('engine.'):
   e['source']=old.replace('Engine/Primitives/','Geometry/').replace('HDR/','Skyboxes/')
   if e['type']=='material':e['source']='Materials/'+Path(old).name
   engine_entries.append(e)
  elif old.startswith('Imported/arena-01/'):
   e['source']=old.removeprefix('Imported/arena-01/').replace('Source/','Models/',1);scene_entries.append(e)
 write(engine/'Catalog.json',dict(type='haki.asset-catalog',version=1,assets=engine_entries))
 write(shared/'Catalog.json',dict(type='haki.asset-catalog',version=1,assets=[]))
 for src,dst in [('Source','Models'),('Materials','Materials'),('Textures','Textures')]:
  old=archive/'Imported/arena-01'/src
  if old.exists():shutil.copytree(old,scene/dst,dirs_exist_ok=True)
 old_scene=archive/'Imported/arena-01/Scenes/arena-01.scene.json'
 document=json.loads(old_scene.read_text());document['assetCatalog']='Catalog.json'
 write(scene/'arena-01.scene.json',document)
 write(scene/'Catalog.json',dict(type='haki.asset-catalog',version=1,imports=['../../../Engine/Catalog.json','../../Shared/Catalog.json'],assets=scene_entries))
 generated=[p.relative_to(scene).as_posix() for p in scene.rglob('*') if p.is_file()]
 receipt=json.loads((archive/'Imported/arena-01/ImportReceipt.json').read_text());receipt['generatedFiles']=generated+['ImportReceipt.json']
 write(scene/'ImportReceipt.json',receipt)
 shutil.copytree(archive/'Scripts',scene/'Scripts')
 if (scene/'Scripts/lib').exists():
  shutil.move(str(scene/'Scripts/lib'),str(shared/'Scripts/lib'))
 for p in (scene/'Scripts').rglob('*.lua'):
  p.write_text(p.read_text().replace('require("lib.','require("shared.lib.'))
 for p in (archive/'Game').glob('*.blend'):
  shutil.copy2(p,scene/'arena-01.blend')
 write(assets/'Game/Project.json',dict(type='haki.scene-manifest',version=1,assetCatalog='../Engine/Catalog.json',entry='arena-01',scenes=[dict(id='arena-01',source='Scenes/arena-01/arena-01.scene.json')]))
 # Archived catalogs remain usable from their new location: copy already retained all dependencies.
 print('Migrated arena and engine. Original assets preserved at',archive)
if __name__=='__main__':main()
