"""Run inside Blender: verify shoreline tops survive building/export with outward normals.

Checks actual GLB triangle winding, normals and palette UVs. The intersecting bank/slab
case previously looked correct in Blender but exported downward-facing shorelines.
"""
import json
from pathlib import Path
import runpy
import struct
import tempfile

import bpy
from mathutils import Vector

ROOT=Path(__file__).resolve().parents[2]
kit=runpy.run_path(str(ROOT/'tools/blender/kits/canyon_kit.py'))
collection=kit['kit_collection']()
visibility=(collection.hide_viewport,collection.hide_render)
selected=list(bpy.context.selected_objects)
active=bpy.context.view_layer.objects.active
collection.hide_viewport=False;collection.hide_render=False


def read_glb(path):
    data=path.read_bytes();offset=12;chunks={}
    while offset<len(data):
        length,kind=struct.unpack_from('<II',data,offset)
        chunks[kind]=data[offset+8:offset+8+length];offset+=8+length
    document=json.loads(chunks[0x4e4f534a]);binary=chunks[0x004e4942]

    def accessor(index):
        value=document['accessors'][index];view=document['bufferViews'][value['bufferView']]
        kind={5121:'B',5123:'H',5125:'I',5126:'f'}[value['componentType']]
        width={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4}[value['type']]
        fmt='<'+kind*width;size=struct.calcsize(fmt)
        start=view.get('byteOffset',0)+value.get('byteOffset',0)
        return [struct.unpack_from(fmt,binary,start+i*view.get('byteStride',size)) for i in range(value['count'])]
    return document,accessor


try:
    with tempfile.TemporaryDirectory(prefix='canyon-water-check-') as temporary:
        for name in ('water_edge','water_corner_outer','water_corner_inner'):
            existing=bpy.data.objects.get(name)
            location=existing.location.copy() if existing else Vector((0,0,0))
            obj=kit[name]()
            try:
                tops=[f for f in obj.data.polygons if all(abs(obj.data.vertices[i].co.z-kit['BANK_TOP'])<1e-5 for i in f.vertices)]
                assert tops and all(f.normal.z>.99 for f in tops),name+': bank top inverted after build'
                for other in list(bpy.context.selected_objects):other.select_set(False)
                obj.location=(0,0,0);obj.select_set(True);bpy.context.view_layer.objects.active=obj
                path=Path(temporary)/(name+'.glb')
                bpy.ops.export_scene.gltf(filepath=str(path),use_selection=True,export_yup=True,
                    export_apply=True,export_cameras=False,export_lights=False)
                document,accessor=read_glb(path);checked=0
                for mesh in document['meshes']:
                    for primitive in mesh['primitives']:
                        attributes=primitive['attributes']
                        positions=accessor(attributes['POSITION']);normals=accessor(attributes['NORMAL'])
                        uv=accessor(attributes['TEXCOORD_0']);indices=[v[0] for v in accessor(primitive['indices'])]
                        for i in range(0,len(indices),3):
                            tri=indices[i:i+3]
                            if not all(abs(positions[j][1]-kit['BANK_TOP'])<1e-5 for j in tri):continue
                            a,b,c=(Vector(positions[j]) for j in tri)
                            assert (b-a).cross(c-a).y>0,name+': GLB bank triangle faces down'
                            for j in tri:
                                assert normals[j][1]>.99,name+': GLB bank normal faces down'
                                u,v=kit['swatch_uv']('bank')
                                assert abs(uv[j][0]-u)<1e-5 and abs(uv[j][1]-(1-v))<1e-5,name+': bank palette UV changed'
                            checked+=1
                assert checked>0,name+': no exported bank-top triangles'
                print(name,checked,'bank-top triangles: winding, normals and palette UV passed')
            finally:
                obj.location=location
finally:
    for obj in list(bpy.context.selected_objects):obj.select_set(False)
    for obj in selected:
        if obj.name in bpy.data.objects:obj.select_set(True)
    if active and active.name in bpy.data.objects:bpy.context.view_layer.objects.active=active
    collection.hide_viewport,collection.hide_render=visibility

print('CANYON_WATER_NORMALS_OK')
