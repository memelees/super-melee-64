#!/usr/bin/env python3
"""Snapshot a completed local Melee3D build without changing its checkout.

Read-only upstream access; all compilation, assets and launcher state are local
to this SM64 experiment. Re-run after building a newer Melee3D version.
"""
from pathlib import Path
import os,sys,shutil
import hashlib,json,shlex,shutil,subprocess

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
OUT=Path(os.environ.get('SMB_RUNTIME_DIR',str(ROOT/'work/melee-embedded')))
UP=Path(os.environ.get('SMB_UPSTREAM_ROOT',str(ROOT/'work/deps/smash-royale')))
WINDOWS=os.environ.get('SMB_BUILD_TARGET')=='windows'
SOURCE=Path(os.environ.get('SMB_ENGINE_SOURCE',str(UP/'build/native-proof/melee-pc')))
BUILD=Path(os.environ.get('SMB_ENGINE_BUILD',str(SOURCE/('build/windows-proof' if WINDOWS else 'build/macos-proof'))))
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()

def host_configured_main(source):
    marker='SMB EMBED CONFIG host_owned=1 discovery=off'
    if marker in source:return source
    boundary='    pc_launcher_configure(&config);'
    if source.count(boundary)!=1:raise RuntimeError('Unexpected native launcher configuration boundary')
    # The host supplies a verified disc, assets, window and input settings.
    # The standalone donor launcher must not search unrelated user folders.
    return source.replace(boundary,'''    if (getenv("SM64_MELEE_LIBRARY")) {
        fprintf(stderr, "SMB EMBED CONFIG host_owned=1 discovery=off\\n");
    } else {
        pc_launcher_configure(&config);
    }''')

def clone(source,destination):
    destination.parent.mkdir(parents=True,exist_ok=True)
    if destination.exists():
        if destination.is_dir():shutil.rmtree(destination)
        else:destination.unlink()
    if sys.platform=='darwin':subprocess.run(['cp','-cR',str(source),str(destination)],check=True)
    elif source.is_dir():shutil.copytree(source,destination)
    else:shutil.copy2(source,destination)

def snapshot_external_link_inputs(link,source,build,output):
    """Freeze absolute static libraries outside the copied source/build trees."""
    link=list(link);inputs={}
    owned=(Path(source).resolve(),Path(build).resolve(),Path(output).resolve())
    for i,arg in enumerate(link):
        path=Path(arg)
        if arg.startswith('-') or not path.is_absolute() or path.suffix.lower() not in ('.a','.lib'):
            continue
        resolved=path.resolve()
        if any(resolved.is_relative_to(root) for root in owned):continue
        if not path.is_file():raise FileNotFoundError('External link input is missing: '+str(path))
        digest=sha(path)
        target=Path(output)/'pinned-link-inputs/external'/(digest[:16]+'-'+path.name)
        if not target.is_file() or sha(target)!=digest:clone(path,target)
        if sha(target)!=digest:raise RuntimeError('External link input changed while freezing: '+str(path))
        link[i]=str(target)
        inputs[str(path)]=dict(snapshot=str(target),sha256=digest)
    return link,inputs

def refresh():
    meta_path=Path(os.environ.get('SMB_NATIVE_METADATA',str(UP/('build/native-proof/native-windows-build.json' if WINDOWS else 'build/native-proof/native-mac-build.json'))))
    OUT.mkdir(parents=True,exist_ok=True)
    before=meta_path.read_bytes();meta=json.loads(before)
    if sha(BUILD/('melee.exe' if WINDOWS else 'melee'))!=meta['executable_sha256']:
        raise RuntimeError('Upstream build is changing; finish its build before refreshing')
    for name,digest in meta.get('modules',{}).items():
        if sha(UP/'native/mac'/name)!=digest:
            raise RuntimeError('Build the latest Melee3D sources first: '+name)
    clone(SOURCE/'src',OUT/'abi/src')
    clone(SOURCE.parent/'mac-runtime',OUT/'abi/mac-runtime')
    clone(SOURCE/'extern/aurora/include',OUT/'abi/aurora')
    clone(SOURCE/'extern/aurora/lib',OUT/'abi/aurora-lib')
    clone(SOURCE/'extern/monocypher',OUT/'abi/monocypher')
    clone(BUILD/'_deps',OUT/'abi/build-deps')
    clone(SOURCE/'resources',OUT/'resources')
    clone(UP/'build/native-proof/akaneia-world-files',OUT/'assets')
    clone(UP/'build/akaneia/disc/files/audio',OUT/'assets/audio')
    for f in (UP/'build/akaneia/disc/files').rglob('*'):
        target=OUT/'assets'/f.relative_to(UP/'build/akaneia/disc/files')
        if f.is_file() and not target.exists():
            target.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(f,target)
    if not os.environ.get('SMB_KEEP_LINEUP'):shutil.copy2(UP/'tools/melee3d/native_lineup.py',HERE/'native_lineup.py')
    # Any C/C++ dependency path in the compiler plans resolves to a snapshot.
    def remap(s):
        for a,b in ((str(SOURCE/'extern/monocypher'),str(OUT/'abi/monocypher')),(str(BUILD/'_deps'),str(OUT/'abi/build-deps')),
                    (str(SOURCE/'extern/aurora/include'),str(OUT/'abi/aurora')),
                    (str(SOURCE/'extern/aurora/lib'),str(OUT/'abi/aurora-lib')),
                    (str(SOURCE.parent/'mac-runtime'),str(OUT/'abi/mac-runtime')),
                    (str(SOURCE/'../mac-runtime'),str(OUT/'abi/mac-runtime')),
                    (str(SOURCE/'src'),str(OUT/'abi/src'))):s=s.replace(a,b)
        return s
    commands=json.loads((BUILD/'compile_commands.json').read_text())
    def compiler(ending):
        entry=next(c for c in commands if c['file'].endswith(ending))
        args=shlex.split(entry['command'])
        for flag in ('-o','-c'):
            i=args.index(flag);del args[i:i+2]
        return [remap(s).replace('-fPIE','-fPIC') for s in args]
    plan=dict(compiler=compiler('/pc/main.c'),game_compiler=compiler('/pc/m3d_lab.c'),
              gpu_compiler=compiler('/gx/gx.cpp'))
    plan['bridge_compiler']=compiler('/pc/m3d_lab.c')
    plan['archiver']=shutil.which('x86_64-w64-mingw32-ar') or 'ar' if WINDOWS else 'ar'
    # DAT scalars keep their original byte order; GCC implements that ABI.
    plan['game_compiler'][0]=shutil.which('x86_64-w64-mingw32-gcc') if WINDOWS else (os.environ.get('GCC_BIN') or shutil.which('gcc-16') or shutil.which('gcc-15') or 'gcc')
    plan['game_compiler'][1:1]=['-fexec-charset=CP932','-Wno-scalar-storage-order']
    plan['game_compiler']=[s for s in plan['game_compiler'] if s!='-fcolor-diagnostics']
    plan['gpu_compiler']+=['-I',str(OUT/'abi/aurora-lib'),'-I',str(HERE)]
    raw=subprocess.check_output(['ninja','-C',str(BUILD),'-t','commands','melee'],text=True).splitlines()[-1]
    link=shlex.split(raw.split(' && ')[1])
    for i,s in enumerate(link):
        if not s.startswith('-') and not Path(s).is_absolute() and s.endswith(('.o','.obj','.a','.res','.lib')):
            target=OUT/'pinned-link-inputs'/s;target.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(BUILD/s,target);link[i]=str(target)
    link,plan['external_link_inputs']=snapshot_external_link_inputs(link,SOURCE,BUILD,OUT)
    clone(BUILD/'libmelee_game.a',OUT/'libmelee_game.a')
    clone(BUILD/'extern/aurora/libaurora_os.a',OUT/'libaurora_os.a')
    link=[str(OUT/'libmelee_game.a') if s==str(OUT/'pinned-link-inputs/libmelee_game.a') else
          str(OUT/'libaurora_os.a') if s==str(OUT/'pinned-link-inputs/extern/aurora/libaurora_os.a') else remap(s) for s in link]
    library=OUT/('melee_embedded.dll' if WINDOWS else 'libmelee_embedded.dylib')
    link[link.index('-o')+1]=str(library);plan['library']=str(library);plan['target']='windows' if WINDOWS else 'mac'
    if WINDOWS:
        link=[s for s in link if s!='-Wl,-u,WinMain' and not s.startswith('-Wl,--out-implib,') and not s.endswith('/libaurora_main.a')]
        link+=['-shared','-Wl,--export-all-symbols']
    else:
        link+=['-dynamiclib','-Wl,-exported_symbols_list,'+str(OUT/'exports.txt'),'-Wl,-install_name,@rpath/libmelee_embedded.dylib']
    link += [str(OUT/'melee_embedded.c.o'),str(OUT/'melee_gpu.cpp.o')]
    plan['link']=link
    (OUT/'cached-build-plan.json').write_text(json.dumps(plan,indent=2)+'\n')
    # Keep pristine snapshot sources; prepare separate patched translation units.
    patched=OUT/'src';clone(OUT/'abi/src',patched)
    subprocess.run(['patch','-p1','-i',str(HERE/'embedded-bootstrap.patch')],cwd=patched,check=True)
    for name in ('pc/main.c','pc/vi.c','pc/os.c','pc/m3d_lab.c','melee/gm/gmscene.c','sysdolphin/baselib/video.c'):
        source=patched/name
        if name=='pc/main.c':source.write_text(host_configured_main(source.read_text()))
        if WINDOWS and name=='pc/main.c':
            source.write_text('#define SDL_MAIN_HANDLED 1\n'+source.read_text().replace('int smb_melee_main(int argc, char* argv[]) {','int smb_melee_main(int argc, char* argv[]) {\n    SDL_SetMainReady();\n    if(!SDL_RegisterApp("SuperMelee64.NativeFighter",0,NULL))return 1;\n    atexit(SDL_UnregisterApp);'))
        game=name not in ('pc/main.c','pc/vi.c','pc/os.c')
        cmd=plan['game_compiler'] if game else plan['compiler']
        extra=['-Dm3d_lab_tick=m3d_lab_tick_original','-Dm3d_lab_pad=m3d_lab_pad_original'] if name=='pc/m3d_lab.c' else []
        obj=Path(str(source)+('.obj' if WINDOWS else '.o'))
        subprocess.run(cmd+['-I',str(source.parent),*extra,'-c',str(source),'-o',str(obj)],check=True)
        if game:subprocess.run([plan.get('archiver','ar'),'r',str(OUT/'libmelee_game.a'),str(obj)],check=True)
        else:plan['link']=[str(obj) if s in [str(OUT/'pinned-link-inputs/CMakeFiles/melee.dir/src'/name)+ext for ext in ('.o','.obj')] else s for s in plan['link']]
    # Virtual time's real clock is renamed in our private OS archive only.
    os_source=(SOURCE/'extern/aurora/lib/dolphin/os/OSTime.cpp').read_text().replace('#include "internal.hpp"','#include "dolphin/os/internal.hpp"')
    os_path=OUT/'extern/aurora/OSTime.cpp';os_path.parent.mkdir(parents=True,exist_ok=True);os_path.write_text(os_source)
    subprocess.run(plan['gpu_compiler']+['-I',str(OUT/'abi/aurora-lib/dolphin/os'),'-DOSGetTime=smb_real_OSGetTime','-c',str(os_path),'-o',str(os_path)+('.obj' if WINDOWS else '.o')],check=True)
    subprocess.run([plan.get('archiver','ar'),'r',str(OUT/'libaurora_os.a'),str(os_path)+('.obj' if WINDOWS else '.o')],check=True)
    if meta_path.read_bytes()!=before or sha(BUILD/('melee.exe' if WINDOWS else 'melee'))!=meta['executable_sha256']:
        raise RuntimeError('Upstream build changed during snapshot; refresh again when it finishes')
    (OUT/'cached-build-plan.json').write_text(json.dumps(plan,indent=2)+'\n')
    pin=json.loads((OUT/'source-pin.json').read_text()) if (OUT/'source-pin.json').exists() else {}
    pin.update(original_executable=meta['executable_sha256'],upstream_revision=os.environ.get('SMB_UPSTREAM_REVISION') or subprocess.check_output(['git','rev-parse','HEAD'],cwd=UP,text=True).strip(),upstream_modules=meta.get('modules',{}),upstream_patch_sha256=meta['patch_sha256'],external_link_inputs=plan.get('external_link_inputs',{}))
    (OUT/'source-pin.json').write_text(json.dumps(pin,indent=2)+'\n')
    (OUT/'upstream-build.json').write_bytes(before)
    print('Frozen latest Melee3D build:',pin['upstream_revision'],flush=True)

if __name__=='__main__':refresh()
