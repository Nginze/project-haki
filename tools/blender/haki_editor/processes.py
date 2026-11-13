"""Launch project tools outside Blender's Python and, when necessary, Flatpak."""
import os
from pathlib import Path

# These must not redirect a project interpreter to Blender's bundled Python/libraries.
BLENDER_ENV = ('PYTHONHOME', 'PYTHONPATH', 'PYTHONUSERBASE', 'LD_LIBRARY_PATH', 'LD_PRELOAD')


def command_environment(args, directory, environ=None, flatpak=None):
    environment = dict(os.environ if environ is None else environ)
    if flatpak is None:
        flatpak = bool(environment.get('FLATPAK_ID')) or Path('/.flatpak-info').is_file()
    if flatpak:
        # Keep the sandbox environment for flatpak-spawn itself. Clean only the host child.
        clean = ['/usr/bin/env']
        for name in BLENDER_ENV:
            clean.extend(['-u', name])
        return ['flatpak-spawn', '--host', '--directory='+str(directory), *clean, *args], environment
    for name in BLENDER_ENV:
        environment.pop(name, None)
    return list(args), environment


def failure_message(command, output):
    prefix = 'Project command failed: ' + str(command[0])
    if command[0] == 'flatpak-spawn':
        prefix += ('\nBlender is running in Flatpak. Host tool execution requires the '
                   'org.freedesktop.Flatpak permission. Enable it explicitly, or turn off '
                   'Import into Haki and run the importer from a host terminal.\n'
                   'Terminal: flatpak override --user --talk-name=org.freedesktop.Flatpak org.blender.Blender')
    return prefix+'\n'+output[-3000:]
