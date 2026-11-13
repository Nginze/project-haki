import json,tempfile,unittest,sys
from pathlib import Path
TOOLS=Path(__file__).resolve().parents[1];sys.path.insert(0,str(TOOLS))
import import_gltf_pack as importer
from package_assets import stage
class LayoutTests(unittest.TestCase):
 def test_scene_reimport_preserves_authored_files_and_packages_only_runtime(self):
  with tempfile.TemporaryDirectory() as temporary:
   root=Path(temporary);game=root/'Assets/Game';game.mkdir(parents=True)
   (game/'Project.json').write_text(json.dumps(dict(type='haki.scene-manifest',version=1,assetCatalog='../Engine/Catalog.json',entry='',scenes=[])))
   source=root/'triangle.gltf';source.write_bytes((TOOLS.parent/'src/Tests/Serialization/Fixtures/Triangle.gltf').read_bytes())
   args=importer.parse_args([str(source),'--pack','demo','--scene','demo','--project-root',str(root),'--set-entry'])
   importer.PackImporter(args).run()
   scene=game/'Scenes/demo';(scene/'demo.blend').write_bytes(b'authored blend');(scene/'Scripts').mkdir();(scene/'Scripts/behaviour.lua').write_text('return {}')
   importer.PackImporter(args).run()
   self.assertEqual((scene/'demo.blend').read_bytes(),b'authored blend')
   self.assertEqual((scene/'Scripts/behaviour.lua').read_text(),'return {}')
   self.assertEqual(json.loads((scene/'demo.scene.json').read_text())['assetCatalog'],'Catalog.json')
   (game/'Scenes/unregistered').mkdir();(game/'Scenes/unregistered/unused.glb').write_bytes(b'unused')
   destination=root/'package/Assets';stage(root/'Assets',destination)
   self.assertTrue((destination/'Game/Scenes/demo/demo.scene.json').is_file())
   self.assertFalse((destination/'Game/Scenes/demo/demo.blend').exists())
   self.assertFalse((destination/'Game/Scenes/demo/ImportReceipt.json').exists())
   self.assertFalse((destination/'Game/Scenes/unregistered').exists())
