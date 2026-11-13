import copy
import json
import math
from pathlib import Path
import sys
import tempfile
import unittest

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(TOOLS))
from blender.haki_editor.metadata import MetadataError, validate_metadata, validate_transform
import import_gltf_pack as importer


class HakiMetadataTests(unittest.TestCase):
    def body(self):
        return {'version':1,'id':'wall-stable-id','components':{
            'rigidBody':{'motion':'static'},
            'collider':{'shape':'box','halfExtents':[1,2,3],'offset':[0,1,0]}}}

    def make_plan(self, root, metadata, **node):
        document = json.loads((TOOLS.parent/'src/Tests/Serialization/Fixtures/Triangle.gltf').read_text())
        document['nodes'] = [{'name':'Collision','extras':{'haki':metadata},**node}]
        document['scenes'] = [{'nodes':[0]}]
        document['scene'] = 0
        source = root/'authored.gltf'
        source.write_text(json.dumps(document))
        pack = importer.PackImporter(importer.parse_args([str(source),'--pack','arena','--project-root',str(root)]))
        return pack,pack.plan_model(source,Path(source.name))

    def test_metadata_only_node_and_shared_mesh_ids(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            metadata = self.body()
            pack,plan = self.make_plan(root,metadata,mesh=0,translation=[3,4,5])
            entity = next(e for e in pack.build_scene([plan])['entities'] if e['id']==metadata['id'])
            self.assertEqual(entity['components']['collider'],metadata['components']['collider'])
            self.assertEqual(entity['components']['transform']['translation'],[3,4,5])
            self.assertIn('meshRenderer',entity['components'])
            player={'version':1,'id':'hero','components':{'characterController':{'radius':0.35,'halfHeight':0.55},
                                                        'playerInput':{'moveSpeed':5}}}
            pack,plan=self.make_plan(root,player)
            hero=next(e for e in pack.build_scene([plan])['entities'] if e['id']=='hero')
            self.assertNotIn('meshRenderer',hero['components'])
            self.assertIn('characterController',hero['components'])

    def test_reject_invalid_fields_and_runtime_state(self):
        cases=[]
        a=self.body();a['version']=2;cases.append(a)
        a=self.body();a['components']['collider']['radius']=float('nan');cases.append(a)
        a=self.body();a['components']['collider']['halfExtents'][1]=0;cases.append(a)
        a=self.body();a['components']['rigidBody']['BodyId']=42;cases.append(a)
        a=self.body();a['components']['characterController']={};cases.append(a)
        a=self.body();a['components']['rigidBody']['motion']='dynamic';a['components']['rigidBody']['layer']='static';cases.append(a)
        a=self.body();a['components']['collider']['shape']='mesh';cases.append(a)
        a=self.body();a['components']['rigidBody']['sensor']=1;cases.append(a)
        a={'version':1,'id':'p','components':{'playerInput':{}}};cases.append(a)
        for a in cases:
            with self.subTest(a=a),self.assertRaises(MetadataError):validate_metadata(a)

    def test_transform_rejection(self):
        matrix=importer.identity_matrix()
        validate_transform(matrix,self.body()['components'])
        for change,components in [({0:-1},self.body()['components']),
                ({4:0.5},self.body()['components']),
                ({0:2},{'characterController':{}}),
                ({0:2},{'collider':{'shape':'sphere'}}),
                ({0:2},{'collider':{'shape':'capsule'}})]:
            altered=matrix.copy()
            for index,value in change.items():altered[index]=value
            with self.subTest(change=change),self.assertRaises(MetadataError):validate_transform(altered,components)

    def test_invalid_import_leaves_existing_scene_untouched(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            existing=root/'Assets/Imported/arena/Scenes/arena.scene.json'
            existing.parent.mkdir(parents=True)
            existing.write_text('previous successful scene')
            a=self.body();a['components']['collider']['halfExtents']=[-1,1,1]
            with self.assertRaises(importer.ImportFailure):self.make_plan(root,a)
            self.assertEqual(existing.read_text(),'previous successful scene')

    def test_parent_and_locomotion_contract(self):
        local = {'translation':[0,1,0],'rotation':[0,0,0,1],'scale':[1,1,1]}
        visual = {'version':1,'id':'visual','components':{
            'parent':{'entity':'hero','local':local},
            'locomotion':{'idleClip':'Idle','moveClip':'Run','speedThreshold':0.1}}}
        self.assertEqual(validate_metadata(visual),visual)
        bad=copy.deepcopy(visual);bad['components']['rigidBody']={};bad['components']['collider']={}
        with self.assertRaisesRegex(MetadataError,'world roots'):validate_metadata(bad)
        bad=copy.deepcopy(visual);bad['components']['parent']['local']['rotation']=[0,0,0,0]
        with self.assertRaises(MetadataError):validate_metadata(bad)
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            pack,plan=self.make_plan(root,visual,mesh=0)
            with self.assertRaisesRegex(importer.ImportFailure,'missing runtime parent'):
                pack.build_scene([plan])

    def test_locomotion_speed_blend_contract(self):
        blend = {'version':1,'id':'visual','components':{'locomotion':{
            'blend':[{'clip':'Idle','speed':0},{'clip':'Walk','speed':1.5},{'clip':'Run','speed':4.5}],
            'fade':0.15,'smoothing':0.1,'maxPlaybackScale':1.5}}}
        self.assertEqual(validate_metadata(blend),blend)
        for change,message in (
                (lambda l:l['blend'][2].update(speed=1.0),'strictly increasing'),
                (lambda l:l.update(idleClip='Idle'),'not both'),
                (lambda l:l.update(blend=[]),'non-empty'),
                (lambda l:l['blend'][0].update(clip=''),'clip name'),
                (lambda l:l.update(maxPlaybackScale=0.5),'maxPlaybackScale')):
            bad=copy.deepcopy(blend);change(bad['components']['locomotion'])
            with self.assertRaisesRegex(MetadataError,message):validate_metadata(bad)

    def test_duplicate_authored_ids_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            pack,plan=self.make_plan(root,self.body(),mesh=0)
            source=root/'authored.gltf'
            document=json.loads(source.read_text())
            document['nodes'].append(copy.deepcopy(document['nodes'][0]))
            document['scenes'][0]['nodes'].append(1)
            source.write_text(json.dumps(document))
            with self.assertRaisesRegex(importer.ImportFailure,'duplicate authored ID'):
                pack.plan_model(source,Path(source.name))

if __name__=='__main__':unittest.main()
