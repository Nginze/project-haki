#!/usr/bin/env python3
"""Build an installable Blender extension without including caches or tests."""
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED

root = Path(__file__).resolve().parent
source = root/'haki_editor'
destination = root/'dist/haki_editor-0.2.0.zip'
destination.parent.mkdir(parents=True,exist_ok=True)
with ZipFile(destination,'w',ZIP_DEFLATED) as archive:
    for name in ('blender_manifest.toml','__init__.py','editor.py','metadata.py','processes.py'):
        archive.write(source/name,name)
print(destination)
