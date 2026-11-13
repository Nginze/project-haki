import os
from pathlib import Path
import subprocess
import sys
import unittest

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(TOOLS))
from blender.haki_editor.processes import command_environment, failure_message


class BlenderProcessTests(unittest.TestCase):
    def test_native_interpreter_ignores_blender_environment(self):
        env = dict(os.environ, PYTHONHOME='/nonexistent/blender/python', PYTHONPATH='/blender/modules',
                   LD_LIBRARY_PATH='/app/lib', PYTHONUSERBASE='/blender/user')
        args = [sys.executable,'-E','-c',"import pygltflib; from PIL import Image; print('OK')"]
        command, child = command_environment(args,TOOLS,environ=env,flatpak=False)
        self.assertNotIn('PYTHONHOME',child)
        self.assertNotIn('LD_LIBRARY_PATH',child)
        self.assertIn('PYTHONHOME',env)  # no mutation of Blender's process
        result = subprocess.run(command,cwd=TOOLS,env=child,capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(result.stdout.strip(),'OK')

    def test_flatpak_host_command_preserves_argument_boundaries(self):
        args = ['/project with spaces/.venv/bin/python','-E','/project with spaces/tools/import_gltf_pack.py']
        env = {'FLATPAK_ID':'org.blender.Blender','LD_LIBRARY_PATH':'/app/lib'}
        command, child = command_environment(args,'/project with spaces',environ=env,flatpak=True)
        self.assertEqual(command[:3],['flatpak-spawn','--host','--directory=/project with spaces'])
        self.assertIn('/usr/bin/env',command)
        self.assertEqual(command[-len(args):],args)
        self.assertEqual(child,env)  # flatpak-spawn itself still needs its sandbox libraries
        self.assertIn('flatpak override --user',failure_message(command,'Access denied'))

    def test_flatpak_detected_from_environment(self):
        command,_ = command_environment(['engine'],'/project',environ={'FLATPAK_ID':'org.blender.Blender'})
        self.assertEqual(command[0],'flatpak-spawn')

if __name__=='__main__':unittest.main()
