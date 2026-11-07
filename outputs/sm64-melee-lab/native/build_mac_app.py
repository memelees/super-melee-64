#!/usr/bin/env python3
"""Build a private, relocatable native app from the user's installed game data."""
from pathlib import Path
import argparse,hashlib,json,os,plistlib,re,shutil,subprocess,tempfile,time
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2];RUNTIME=ROOT/'work/melee-embedded'

def run(*args):subprocess.run([str(x) for x in args],check=True,capture_output=True)
def clone(source,dest):
    dest.parent.mkdir(parents=True,exist_ok=True)
    try:run('/bin/cp','-cR',source,dest)
    except subprocess.CalledProcessError as error:
        # Clone support depends on the destination filesystem. Preserve real
        # copy failures (including a full disk) rather than retrying blindly.
        reason=error.stderr or b''
        if not any(value in reason for value in (b'Operation not supported',b'Cross-device link',b'Invalid argument')):raise
        if source.is_dir():shutil.copytree(source,dest,symlinks=True,dirs_exist_ok=True)
        else:shutil.copy2(source,dest)
def macho_links(path):
    """Read actual load commands, excluding a dylib's own install-name ID."""
    text=subprocess.check_output(['otool','-l',str(path)],text=True)
    loads=[];rpaths=[]
    for block in re.split(r'\nLoad command \d+\n',text):
        command=re.search(r'^\s*cmd (LC_\w+)',block,re.M)
        if not command:continue
        if command[1]=='LC_RPATH':
            rpaths.append(re.search(r'^\s*path (.*?) \(offset',block,re.M)[1])
        elif command[1] in ('LC_LOAD_DYLIB','LC_LOAD_WEAK_DYLIB','LC_REEXPORT_DYLIB','LC_LOAD_UPWARD_DYLIB'):
            loads.append(re.search(r'^\s*name (.*?) \(offset',block,re.M)[1])
    return loads,rpaths
def dependencies(path):return macho_links(path)[0]
def system_library(path):return str(path).startswith(('/usr/lib/','/System/Library/'))
def expand_loader_path(path,loader,executable):
    for token,parent in (('@loader_path',loader.parent),('@executable_path',executable.parent)):
        if path==token or path.startswith(token+'/'):return parent/path[len(token):].lstrip('/')
    if Path(path).is_absolute():return Path(path)
    # Legacy build outputs use relative runpaths such as "." for sibling
    # libraries. Resolve those at the original build location, then relocate.
    if not path.startswith('@'):return loader.parent/path
    raise ValueError('Unsupported native library search path: '+path)
def resolve_library(name,loader,executable,search):
    if name.startswith('@rpath/'):
        candidates=[directory/name[len('@rpath/'):] for directory in search]
    else:candidates=[expand_loader_path(name,loader,executable)]
    for candidate in candidates:
        if candidate.is_file():return candidate.resolve()
    raise ValueError('Unresolved native library '+name+' required by '+str(loader))
def bundle_libraries(exe,frameworks,additional=(),origins=None):
    executables={exe,*additional}
    origins={Path(key):Path(value).resolve() for key,value in (origins or {}).items()}
    roots=[*executables,*[p for p in frameworks.iterdir() if not p.is_symlink()]]
    main_origin=origins.get(exe,exe)
    root_search=[]
    for executable in executables:
        original=origins.get(executable,executable)
        root_search.extend(expand_loader_path(path,original,original) for path in macho_links(original)[1])
    pending=[(path,origins.get(path,path),origins.get(path,path) if path in executables else main_origin,tuple(root_search)) for path in roots]
    seen=set();copied={path.name:origins.get(path,path).resolve() for path in roots if path not in executables}
    while pending:
        binary,original,executable,inherited=pending.pop()
        if binary in seen:continue
        seen.add(binary)
        loads,paths=macho_links(original)
        # Resolve relative dependencies at their original location, before
        # flattening them into Frameworks. A copied @loader_path is too late.
        search=tuple(expand_loader_path(path,original,executable) for path in paths)+inherited
        for dep in loads:
            if system_library(dep):continue
            source=resolve_library(dep,original,executable,search)
            if system_library(source):continue
            dest=frameworks/Path(dep).name
            if dest.name in copied and copied[dest.name]!=source:raise ValueError('Conflicting native library: '+dest.name)
            copied[dest.name]=source
            if not dest.exists():shutil.copy2(source,dest)
            pending.append((dest,source,executable,search))
            run('install_name_tool','-change',dep,'@rpath/'+dest.name,binary)
        for path in macho_links(binary)[1]:run('install_name_tool','-delete_rpath',path,binary)
        if binary not in executables:
            run('install_name_tool','-id','@rpath/'+binary.name,binary)
            run('install_name_tool','-add_rpath','@loader_path',binary)
    for executable in executables:run('install_name_tool','-add_rpath','@executable_path/../Frameworks',executable)
    # SDL2-compat dlopens this unversioned name before main(), so it isn't
    # visible in otool's dependency graph. Resolve it inside the same bundle.
    if (frameworks/'libSDL3.0.dylib').exists() and not (frameworks/'libSDL3.dylib').exists():(frameworks/'libSDL3.dylib').symlink_to('libSDL3.0.dylib')
    for binary in seen:
        for name in dependencies(binary):
            if system_library(name):continue
            if not name.startswith('@rpath/') or not (frameworks/name[len('@rpath/'):]).is_file():
                raise ValueError('Packaged native library is missing: '+name+' required by '+str(binary))
    for binary in frameworks.iterdir():
        if not binary.is_symlink():run('codesign','--force','--sign','-',binary)
    return sorted(x.name for x in frameworks.iterdir())

class MacAppStartupError(RuntimeError):
    def __init__(self,detail):
        self.diagnostic=dict(code='mac-app-startup',title='The packaged game could not start',
            message='The app did not reach gameplay during its isolated startup check. Setup has kept the existing installed app.',
            recovery='Open Setup Log for the startup error below. Retry Build / Resume with the latest installer. Verified build outputs and your saves are kept.',detail=detail[-6000:])
        super().__init__(self.diagnostic['message']+'\n'+detail[-6000:])

def verify_app_startup(app):
    """Exercise the actual packaged loader and rendered gameplay before install."""
    work=ROOT/'work';work.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='app-startup-',dir=work) as folder:
        environment={key:value for key,value in os.environ.items() if not key.startswith(('SM64_','SMB_','MELEE_','MELEE3D_','DYLD_'))}
        environment['SMB_APP_SMOKE_ROOT']=folder
        log=Path(folder)/'Logs/latest.log'
        print('Checking packaged game startup with an isolated save...',flush=True)
        try:
            result=subprocess.run([str(app/'Contents/MacOS/SuperMelee64'),'--app-smoke'],env=environment,
                text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=90)
        except subprocess.TimeoutExpired as error:
            raise MacAppStartupError('Startup timed out.\n'+(log.read_text(errors='replace') if log.exists() else str(error))) from error
        text=(result.stdout or '')+'\n'+(log.read_text(errors='replace') if log.exists() else '')
        complete=re.search(r'SMB HOST EMBED COMPLETE pid=\d+ host_frames=(\d+) native_ticks=(\d+)',text)
        if (result.returncode or not complete or min(map(int,complete.groups()))<180 or
                'SMB HOST NATIVE RENDER ' not in text or 'SMB EMBED CONFIG host_owned=1 discovery=off' not in text):
            raise MacAppStartupError('Exit code: '+str(result.returncode)+'\n'+text)
        report=dict(frames=int(complete[1]),native_ticks=int(complete[2]),rendered_gameplay=True)
        print('Packaged game startup passed: '+json.dumps(report),flush=True)
        return report

def migrate_saves(support):
    for name,source in [('Unlocked',ROOT/'work/unlocked-profile'),('Continue',ROOT/'work/embedded-profile'),('Playthroughs',ROOT/'work/playthroughs')]:
        dest=support/name
        if source.is_dir() and not dest.exists():shutil.copytree(source,dest)
    latest=support/'Playthroughs/latest.txt'
    if latest.is_file():
        old=Path(latest.read_text().strip());prior=ROOT/'work/playthroughs'
        if old.is_relative_to(prior):latest.write_text(str(support/'Playthroughs'/old.relative_to(prior))+'\n')

def application_destination(shared=None,personal=None):
    shared=Path('/Applications') if shared is None else Path(shared)
    personal=Path.home()/'Applications' if personal is None else Path(personal)
    folder=shared if shared.is_dir() and os.access(shared,os.W_OK) else personal
    return folder/'Super Melee 64.app'

class MacAppInstallError(RuntimeError):
    def __init__(self,message):
        self.diagnostic=dict(code='mac-app-install',title='The app could not be installed in Applications',
            message=message,recovery='Check that your Applications folder is writable and has free space, then choose Build / Resume. Existing apps and verified build outputs are preserved. Do not run setup as root.')
        super().__init__(message)

def publish_app(source,target):
    """Verify a destination-local copy before replacing an existing app.

    The build folder and Applications may be on different volumes. Only the
    final renames happen in Applications, with rollback and a retained backup.
    """
    source,target=Path(source),Path(target)
    if target.exists() or target.is_symlink():
        try:identity=plistlib.loads((target/'Contents/Info.plist').read_bytes()).get('CFBundleIdentifier')
        except (OSError,ValueError):identity=None
        if identity!='dev.supermelee64.launcher':
            raise MacAppInstallError('An unrecognized app already exists at '+str(target)+'. Move or rename it before retrying; setup has left it unchanged.')
    try:
        target.parent.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='.super-melee64-install-',dir=target.parent) as folder:
            candidate=Path(folder)/target.name
            clone(source,candidate)
            run('codesign','--verify','--deep','--strict',candidate)
            backup=None
            if target.exists() or target.is_symlink():
                backup=target.parent/('.previous-super-melee-app-'+str(time.time_ns()))
                target.rename(backup)
            try:
                candidate.rename(target)
                run('codesign','--verify','--deep','--strict',target)
            except BaseException:
                if target.exists() or target.is_symlink():target.rename(candidate)
                if backup is not None:backup.rename(target)
                raise
        return target
    except OSError as error:
        raise MacAppInstallError('Could not install '+str(target)+': '+str(error)) from error

def build(install=False,applications=None):
    from build_branding import build as branding
    from build_selection_art import build as selection
    branding();art=selection(course_enemies=True)
    settings=json.loads((ROOT/'work/install.json').read_text()) if (ROOT/'work/install.json').is_file() else {}
    disc=Path(settings.get('disc',Path.home()/'Downloads/Super Smash Bros. Melee (USA) (En,Ja) (Rev 2).iso'))
    if not disc.is_file():raise FileNotFoundError('The installed Melee disc is missing: '+str(disc))
    parent=ROOT/'work/app-bundle-build';parent.mkdir(exist_ok=True)
    stage=parent/('build-'+str(time.time_ns()))/'Super Melee 64.app';contents=stage/'Contents'
    resources=contents/'Resources';resources.mkdir(parents=True);frameworks=contents/'Frameworks';frameworks.mkdir()
    exe=contents/'MacOS/SuperMelee64';exe.parent.mkdir()
    host_source=ROOT/'work/sm64ex-source/build/us_melee_bridge/sm64.us.f3dex2e'
    shutil.copy2(host_source,exe)
    shutil.copy2(RUNTIME/'libmelee_embedded.dylib',frameworks/'libmelee_embedded.dylib')
    origins={exe:host_source,frameworks/'libmelee_embedded.dylib':RUNTIME/'libmelee_embedded.dylib'}
    for source,dest in [(RUNTIME/'resources',resources/'donor'),(RUNTIME/'assets',resources/'game/files'),(disc,resources/'game/Melee.iso'),(ROOT/'work/castle-native-cache',resources/'collision'),(art,resources/'selection.bin')]:clone(source,dest)
    for name in ('Super Melee 64.icns','Super Melee 64 Icon.png','logo.rgba'):shutil.copy2(HERE.parent/'branding'/name,resources/name)
    env=dict(SM64_MELEE_LIBRARY='${FRAMEWORKS}/libmelee_embedded.dylib',SM64_MELEE_DISC='${RESOURCES}/game/Melee.iso',
        SM64_MELEE_RESOURCES='${RESOURCES}/donor',SM64_MELEE_PROFILE='${SUPPORT}/Native',
        SMB_QUICK_START='1',SMB_CHARACTER_SELECT='1',SMB_SELECTION_ART='${RESOURCES}/selection.bin',SMB_LOGO='${RESOURCES}/logo.rgba',
        SMB_CAMERA='sm64',SMB_CPU_COUNT='3',SMB_CPU_CHARACTERS='fox,marth,wolf',SMB_COURSE_ENEMIES='1',SMB_DIFFICULTY='default',SMB_ALL_STARS='1',SMB_SAVE_MODE='unlocked',
        MELEE_BOOT_SCENE='vs',MELEE_DEBUG_VS_STAGE='32',MELEE3D_NATIVE_LAB='4',MELEE3D_NATIVE_ROSTER='akaneia',MELEE3D_NATIVE_LINEUP='6,2,9,34',
        MELEE3D_NATIVE_MOVE_SPEED='1.25',MELEE3D_NATIVE_GRAVITY='1',MELEE3D_NATIVE_HUMAN='1',MELEE3D_NATIVE_CHARACTER='link',
        MELEE_FILES_DIR='${RESOURCES}/game/files',MELEE3D_AKANEIA_FILES='${RESOURCES}/game/files',MELEE3D_NATIVE_AKANEIA='1',MELEE3D_NATIVE_3D='1',MELEE3D_NATIVE_INPUTS='spatial',
        MELEE3D_NATIVE_CAMERA='combat',MELEE3D_NATIVE_CAMERA_TURN='snap',MELEE3D_NATIVE_DELFINO='${RESOURCES}/collision',MELEE3D_NATIVE_DELFINO_LAYOUT='plaza',
        MELEE3D_NATIVE_ITEMS='off',MELEE3D_NATIVE_PARTY='battle',MELEE3D_NATIVE_ROUND_SECONDS='0',MELEE3D_NATIVE_STOCK_MINUTES='0',MELEE3D_NATIVE_RESULTS='off',
        MELEE_BACKEND='metal',MELEE_VSYNC='0',MELEE_SEED='1',MELEE_CACHE_DIR='${SUPPORT}/Cache',MELEE_WINDOW_TITLE='Super Melee 64',MELEE_LOG_FILE='${LOGS}/native-game.log')
    (resources/'Game.plist').write_bytes(plistlib.dumps(dict(Environment=env,PrivateGameData=True)))
    (contents/'Info.plist').write_bytes(plistlib.dumps(dict(CFBundleIdentifier='dev.supermelee64.launcher',CFBundleExecutable='SuperMelee64',CFBundleName='Super Melee 64',CFBundleDisplayName='Super Melee 64',CFBundlePackageType='APPL',CFBundleVersion='0.6.0',CFBundleShortVersionString='0.6.0',CFBundleIconFile='Super Melee 64.icns',NSHighResolutionCapable=True,LSMinimumSystemVersion='15.0',NSHumanReadableCopyright='Super Melee 64 — locally built from your game files.')))
    coop_source=ROOT/'work/sm64coopdx-melee/build/us_pc/sm64coopdx'
    if not coop_source.is_file():raise FileNotFoundError('Build the multiplayer host with native/build_coop.py first')
    coop=contents/'MacOS/SuperMelee64Coop';shutil.copy2(coop_source,coop)
    origins[coop]=coop_source
    for name in ('libcoopnet.dylib','libjuice.1.6.2.dylib'):
        shutil.copy2(coop_source.parent/name,frameworks/name)
        origins[frameworks/name]=coop_source.parent/name
    rom=ROOT/'work/sm64ex-source/baserom.us.z64';clone(rom,resources/'game/Mario64.z64')
    env.update(SMB_COOP_EXECUTABLE='${RESOURCES}/../MacOS/SuperMelee64Coop',SMB_PROFILE_COOP='${SUPPORT}/Coop',SMB_SM64_ROM='${RESOURCES}/game/Mario64.z64')
    (resources/'Game.plist').write_bytes(plistlib.dumps(dict(Environment=env,PrivateGameData=True)))
    libraries=bundle_libraries(exe,frameworks,(coop,),origins)
    run('codesign','--force','--sign','-',stage)
    run('codesign','--verify','--deep','--strict',stage)
    startup=verify_app_startup(stage)
    target=((Path(applications).expanduser()/'Super Melee 64.app') if applications is not None else application_destination()) if install else ROOT/'Super Melee 64.app'
    if install:
        publish_app(stage,target)
        shutil.rmtree(stage.parent)
    else:
        if target.exists() or target.is_symlink():
            backup=ROOT/'work'/('previous-super-melee-app-'+str(time.time_ns()))
            target.rename(backup)
        target.parent.mkdir(parents=True,exist_ok=True);stage.rename(target)
    if install:
        shortcut=ROOT/'Super Melee 64.app'
        if shortcut.exists() or shortcut.is_symlink():shortcut.rename(ROOT/'work'/('previous-super-melee-launcher-'+str(time.time_ns())))
        shortcut.symlink_to(target,target_is_directory=True)
    # Retain old development-host links while making a bare click open this app.
    relay=ROOT/'work/Super Melee 64.app/Contents/Resources/GameApp.path'
    relay.parent.mkdir(parents=True,exist_ok=True);relay.write_text(str(target)+'\n')
    development=relay.parents[2]
    if (development/'Contents/Info.plist').is_file():
        shutil.copy2(ROOT/'work/sm64ex-source/build/us_melee_bridge/sm64.us.f3dex2e',development/'Contents/MacOS/sm64')
        shutil.copy2(HERE.parent/'branding/Super Melee 64.icns',development/'Contents/Resources/Super Melee 64.icns')
        run('codesign','--force','--sign','-',development)
    support=Path.home()/'Library/Application Support/Super Melee 64'
    # A local build should not create or migrate the player's live profile.
    # Finder startup creates it; an explicit installation retains migration.
    if install:support.mkdir(parents=True,exist_ok=True);migrate_saves(support)
    run('touch',target)
    register=Path('/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister')
    if register.exists() and os.environ.get('SMB_APP_SKIP_REGISTER')!='1':run(register,'-f',target)
    report=dict(app=str(target),installed=install,startup_verified=True,startup=startup,native_executable=True,python_at_launch=False,shell_at_launch=False,game_data_bundled=True,external_library_paths=False,libraries=libraries,save_folder=str(support),icon_alpha=True,signature_verified=True,bytes=sum(p.stat().st_size for p in target.rglob('*') if p.is_file()))
    (ROOT/'work/native-app-build.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));return target
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--install',action='store_true')
    parser.add_argument('--applications-dir',type=Path,help='Applications folder override for --install')
    args=parser.parse_args();build(args.install,args.applications_dir)
