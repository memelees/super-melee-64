#!/usr/bin/env python3
"""Recompile the private adapter using frozen native linker inputs and ABI headers.

Use refresh_melee.py then build_adventure.py to import a newer completed local
Melee3D build. This fast path retains the current private snapshot.
"""
from pathlib import Path
import os,sys,shutil
import hashlib,json,subprocess
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
OUT=Path(os.environ.get('SMB_RUNTIME_DIR',str(ROOT/'work/melee-embedded')))

def build():
    plan=json.loads((OUT/'cached-build-plan.json').read_text())
    pin=json.loads((OUT/'source-pin.json').read_text())
    subprocess.run(plan.get('bridge_compiler',plan['compiler'])+['-I',str(HERE),'-Wno-deprecated-declarations',
        '-c',str(HERE/'melee_embedded.c'),'-o',str(OUT/'melee_embedded.c.o')],check=True)
    if 'gpu_compiler' in plan:
        subprocess.run(plan['gpu_compiler']+['-c',str(HERE/'melee_gpu.cpp'),'-o',str(OUT/'melee_gpu.cpp.o')],check=True)
    subprocess.run(plan['link'],check=True)
    pin['library_sha256']=hashlib.sha256(Path(plan.get('library',str(OUT/'libmelee_embedded.dylib'))).read_bytes()).hexdigest()
    (OUT/'source-pin.json').write_text(json.dumps(pin,indent=2)+'\n')
    print(plan.get('library',str(OUT/'libmelee_embedded.dylib')))

if __name__=='__main__':build()
