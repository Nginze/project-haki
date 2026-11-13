import json
from pathlib import Path
import re
import sys
import tempfile
import unittest

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(TOOLS))
from blender.haki_editor.metadata import validate_metadata, validate_script_path, validate_scene_metadata, MetadataError
import import_gltf_pack as importer


class ScriptAuthoringTests(unittest.TestCase):
    def test_supported_properties_and_invalid_paths(self):
        meta={'version':1,'id':'script-only','components':{'script':{'path':'lib/behaviour.lua',
              'properties':{'speed':5,'active':True,'label':'a','position':[1,2,3]}}}}
        self.assertEqual(validate_metadata(meta),meta)
        for path in ('../outside.lua','/tmp/test.lua','C:/test.lua','x\\test.lua','test.txt',''):
            with self.subTest(path=path),self.assertRaises(MetadataError):validate_script_path(path)
        for value in (None,{},[1,2],[1,True,3],float('nan')):
            meta['components']['script']['properties']={'bad':value}
            with self.subTest(value=value),self.assertRaises(MetadataError):validate_metadata(meta)
        self.assertEqual(validate_scene_metadata({'version':1,'script':''})['script'],'')

    def test_scene_and_entity_import_reimport_and_missing_source(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)
            scripts=root/'Assets/Scripts'
            scripts.mkdir(parents=True)
            (scripts/'entity.lua').write_text('return {}')
            (scripts/'rules.lua').write_text('return {}')
            source=root/'arena.gltf'
            doc={'asset':{'version':'2.0'},'scene':0,'scenes':[{'nodes':[0],
                 'extras':{'haki':{'version':1,'script':'rules.lua'}}}],
                 'nodes':[{'name':'Manager','extras':{'haki':{'version':1,'id':'manager',
                    'components':{'script':{'path':'entity.lua'}}}}}]}
            source.write_text(json.dumps(doc))
            pack=importer.PackImporter(importer.parse_args([str(source),'--pack','test','--project-root',str(root)]))
            scene=pack.build_scene([pack.plan_model(source,Path(source.name))])
            self.assertEqual(scene['script'],'rules.lua')
            self.assertEqual(next(e for e in scene['entities'] if e['id']=='manager')['components']['script']['path'],'entity.lua')
            (scripts/'entity.lua').unlink()
            with self.assertRaisesRegex(importer.ImportFailure,'Script does not exist'):pack.build_scene([pack.plan_model(source,Path(source.name))])
            doc['nodes'][0]['extras']['haki']['components']={}
            doc['scenes'][0]['extras']['haki']['script']=''
            source.write_text(json.dumps(doc))
            scene=pack.build_scene([pack.plan_model(source,Path(source.name))])
            self.assertNotIn('script',scene)

    def test_editor_definitions_cover_bound_members(self):
        # Catch API drift when bindings are added/renamed in C++.
        cpp=(TOOLS.parent/'src/Haki/Scripting/ScriptBindings.cpp').read_text()
        meta=(TOOLS/'lua/library/haki.lua').read_text()
        owners={'vec3':('vec3','Vec3'),'quat':('quat','Quat'),'entity':('Entity',),
                'scene':('scene',),'physics':('physics',),'input':('input',),'log':('log',)}
        for variable,classes in owners.items():
            for member in set(re.findall(r'\b'+variable+r'\["([^"]+)"\]',cpp)):
                if member in ('x','y','z'):
                    self.assertIn('---@field '+member+' number',meta)
                else:
                    self.assertTrue(any(re.search(r'function '+owner+r'[:.]'+member+r'\(',meta) for owner in classes),f'Missing Lua definition: {variable}.{member}')

if __name__=='__main__':unittest.main()
