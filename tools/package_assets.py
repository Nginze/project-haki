"""Stage runtime assets, excluding authoring files and experiment archives."""
import argparse,json,shutil
from pathlib import Path

def stage(source,destination):
 source=Path(source).resolve();destination=Path(destination).resolve()
 if source.resolve()==destination.resolve():raise ValueError('Source and destination must differ')
 if destination.exists():shutil.rmtree(destination)
 roots = [source/'Engine',source/'Game/Shared']
 project = source/'Game/Project.json'
 if project.exists():
  for scene in json.loads(project.read_text())['scenes']:
   directory=(project.parent/scene['source']).resolve().parent
   if not directory.is_relative_to(source.resolve()):raise ValueError('Scene escapes runtime assets')
   roots.append(directory)
 files=[project] if project.exists() else []
 for directory in roots:
  if directory.exists():files.extend(directory.rglob('*'))
 for file in files:
  if not file.is_file() or file.suffix.lower() in {'.blend','.blend1','.blend2'} or file.name in {'ImportReceipt.json','README.md'} or any(part.startswith('.') for part in file.relative_to(source).parts):continue
  target=destination/file.relative_to(source);target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(file,target)
 destination.mkdir(parents=True,exist_ok=True);(destination/'.haki-assets.stamp').touch()
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('source');p.add_argument('destination');a=p.parse_args();stage(a.source,a.destination)
