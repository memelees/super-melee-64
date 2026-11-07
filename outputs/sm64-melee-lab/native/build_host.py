#!/usr/bin/env python3
"""Build the host for macOS or Windows with pinned SDL2/GLEW inputs."""
from pathlib import Path
import hashlib,os,shutil,subprocess,sys,tarfile,urllib.request
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2]
MAC_SOUND_TOOLS=('i686-w64-mingw32-as','i686-w64-mingw32-objcopy','i686-w64-mingw32-objdump')
DOWNLOADS={
 'SDL2-devel-2.32.10-mingw.tar.gz':('https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-devel-2.32.10-mingw.tar.gz','83a5d74012311edc3c0d40ea6faecbe57ad692aa033fa5dc273cc937e3938ff2'),
 'glew-2.2.0.tgz':('https://github.com/nigels-com/glew/releases/download/glew-2.2.0/glew-2.2.0.tgz','d4fc82893cfb00109578d0a1a2337fb8ca335b3ceccf97b97e5cc7f08e4353e1')}
def copy_build_source(source,target):
    """Stage source with a fresh timestamp, including reproducible ZIPs."""
    source,target=Path(source),Path(target)
    # copy2 would restore the ZIP's reproducible mtime and let Make reuse a
    # stale object, including one left by an older installer's partial build.
    shutil.copyfile(source,target)
def mac_build_environment(output):
    """Discard compiled objects from a prior deployment target, retaining assets."""
    output=Path(output);output.mkdir(parents=True,exist_ok=True)
    stamp=output/'.super-melee-macos-target'
    if not stamp.is_file() or stamp.read_text()!='15.0\n':
        for path in output.rglob('*'):
            if path.is_file() and (path.suffix in {'.o','.d','.a'} or path.name=='sm64.us.f3dex2e'):
                path.unlink()
        stamp.write_text('15.0\n')
    return dict(os.environ,MACOSX_DEPLOYMENT_TARGET='15.0')
def make_program():
    make=shutil.which('gmake') or (shutil.which('make') if sys.platform!='darwin' else None)
    if not make:
        raise RuntimeError('GNU Make is required. On Mac choose Prepare Build Tools in the latest installer, or run brew install make, then Build / Resume. Apple\'s bundled Make 3.81 cannot build these sources.')
    return make
def mac_sound_tools():
    # The pinned Mac hosts use GNU assembly/COFF tools for audio bytecode only.
    # The game itself is still compiled as a native Apple Silicon executable.
    if sys.platform!='darwin':return []
    paths={name:shutil.which(name) for name in MAC_SOUND_TOOLS}
    missing=[name for name,path in paths.items() if not path]
    if missing:
        raise RuntimeError('SM64 sound build tools are missing: '+', '.join(missing)+'. Choose Prepare Build Tools in the latest Mac installer, or run brew install mingw-w64, then Build / Resume in the same folder.')
    return [key+'='+paths[name] for key,name in zip(('AS','OBJCOPY','OBJDUMP'),MAC_SOUND_TOOLS)]
def prepare_windows():
    cache=ROOT/'work/release-deps';cache.mkdir(parents=True,exist_ok=True)
    for filename,(url,digest) in DOWNLOADS.items():
        path=cache/filename
        if not path.exists():
            temporary=path.with_suffix(path.suffix+'.part')
            with urllib.request.urlopen(url,timeout=90) as response,temporary.open('wb') as output:shutil.copyfileobj(response,output)
            temporary.rename(path)
        if hashlib.sha256(path.read_bytes()).hexdigest()!=digest:raise ValueError('Dependency checksum mismatch: '+filename)
        with tarfile.open(path) as archive:archive.extractall(cache,filter='data')
    sdl=cache/'SDL2-2.32.10/x86_64-w64-mingw32';glew=cache/'glew-2.2.0'
    lib=glew/'lib';lib.mkdir(exist_ok=True)
    cc=os.environ.get('SMB_WINDOWS_CC','x86_64-w64-mingw32-gcc');ar=cc.replace('gcc','ar')
    subprocess.run([cc,'-O2','-DGLEW_STATIC','-I'+str(glew/'include'),'-c',str(glew/'src/glew.c'),'-o',str(lib/'glew.o')],check=True)
    subprocess.run([ar,'rcs',str(lib/'libglew32.a'),str(lib/'glew.o')],check=True)
    return sdl,glew

def build():
    windows=os.environ.get('SMB_BUILD_TARGET')=='windows';source=ROOT/'work/sm64ex-source'
    make=make_program()
    build_dir='build/us_melee_bridge_windows' if windows else 'build/us_melee_bridge'
    environment=mac_build_environment(source/build_dir) if not windows and sys.platform=='darwin' else None
    # Prepare the generated include's parent explicitly, including on resume.
    (source/build_dir).mkdir(parents=True,exist_ok=True)
    command=[make,'-C',str(source),'-j8','VERSION=us','BETTERCAMERA=1','BUILD_DIR='+build_dir]
    if not windows:command+=mac_sound_tools()
    if windows:
        sdl,glew=prepare_windows();cc=os.environ.get('SMB_WINDOWS_CC','x86_64-w64-mingw32-gcc')
        # Quoted flags are evaluated by make's shell; installation paths cannot contain spaces.
        wrapper=ROOT/'work/release-deps/sdl2-config'
        wrapper.write_text('#!/bin/sh\ncase "$1" in\n--prefix) echo "'+str(sdl)+'";;\n--cflags) echo "-I'+str(sdl/'include')+' -I'+str(sdl/'include/SDL2')+' -Dmain=SDL_main";;\n*) echo "-L'+str(sdl/'lib')+' -lmingw32 -lSDL2main -lSDL2 -lm -ldinput8 -ldxguid -ldxerr8 -luser32 -lgdi32 -lwinmm -limm32 -lole32 -loleaut32 -lshell32 -lversion -luuid -lsetupapi";;\nesac\n');wrapper.chmod(0o755)
        command += ['HOST_OS=Windows_NT','OSX_BUILD=0','WINDOWS_BUILD=1','TARGET_ARCH=x86-64','TARGET_BITS=64','NO_BZERO_BCOPY=1',
            'CC='+cc,'CXX='+cc.replace('gcc','g++'),'LD='+cc.replace('gcc','g++'),
            'CPP='+cc.replace('gcc','cpp')+' -P','AS='+cc.replace('gcc','as'),'OBJCOPY='+cc.replace('gcc','objcopy'),'OBJDUMP='+cc.replace('gcc','objdump'),
            'SDLCONFIG='+str(wrapper),'SMB_EXTRA_CFLAGS=-I'+str(glew/'include'),'SMB_EXTRA_LDFLAGS=-L'+str(glew/'lib')]
    subprocess.run(command,check=True,env=environment)
    if windows:
        destination=ROOT/'work/SuperMelee64-Windows';destination.mkdir(exist_ok=True)
        shutil.copy2(source/'build/us_melee_bridge_windows/sm64.us.f3dex2e.exe',destination/'SuperMelee64.exe')
        shutil.copy2(sdl/'bin/SDL2.dll',destination/'SDL2.dll')
        runtime=Path(os.environ.get('SMB_RUNTIME_DIR',ROOT/'work/melee-embedded'))
        shutil.copy2(runtime/'melee_embedded.dll',destination/'melee_embedded.dll')
        native_build=Path(os.environ.get('SMB_ENGINE_BUILD',str(Path(os.environ['SMB_UPSTREAM_ROOT'])/'build/native-proof/melee-pc/build/windows-proof')))
        for path in native_build.glob('*.dll'):shutil.copy2(path,destination/path.name)
        for name in ('libwinpthread-1.dll','libgcc_s_seh-1.dll','libstdc++-6.dll'):
            path=Path(subprocess.check_output([cc,'-print-file-name='+name],text=True).strip())
            if path.is_file():shutil.copy2(path,destination/name)
    return command
if __name__=='__main__':build()
