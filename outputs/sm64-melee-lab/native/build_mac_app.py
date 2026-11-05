#!/usr/bin/env python3
"""Build a private, relocatable native app from the user's installed game data."""
from pathlib import Path
import argparse,hashlib,json,os,plistlib,re,shutil,subprocess,time
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
def dependencies(path):
    return [line.strip().split(' (',1)[0] for line in subprocess.check_output(['otool','-L',str(path)],text=True).splitlines()[1:]]
def bundle_libraries(exe,frameworks,additional=()):
    executables={exe,*additional}
    pending=[*executables,*[p for p in frameworks.iterdir() if not p.is_symlink()]];seen=set();copied={}
    while pending:
        binary=pending.pop()
        if binary in seen:continue
        seen.add(binary)
        for dep in dependencies(binary):
            if not dep.startswith('/opt/') and not dep.startswith('/usr/local/'):continue
            source=Path(dep).resolve();dest=frameworks/source.name
            # Preserve the install-name basename, including version suffixes.
            dest=frameworks/Path(dep).name
            if dest.name in copied and copied[dest.name]!=source:raise ValueError('Conflicting native library: '+dest.name)
            copied[dest.name]=source
            if not dest.exists():shutil.copy2(source,dest);pending.append(dest)
            run('install_name_tool','-change',dep,'@rpath/'+dest.name,binary)
        commands=subprocess.check_output(['otool','-l',str(binary)],text=True)
        for path in re.findall(r'cmd LC_RPATH\n.*?\n\s*path (.*?) \(offset',commands):
            if path.startswith('/'):run('install_name_tool','-delete_rpath',path,binary)
        if binary not in executables:
            run('install_name_tool','-id','@rpath/'+binary.name,binary)
            run('install_name_tool','-add_rpath','@loader_path',binary)
    for executable in executables:run('install_name_tool','-add_rpath','@executable_path/../Frameworks',executable)
    # SDL2-compat dlopens this unversioned name before main(), so it isn't
    # visible in otool's dependency graph. Resolve it inside the same bundle.
    if (frameworks/'libSDL3.0.dylib').exists():(frameworks/'libSDL3.dylib').symlink_to('libSDL3.0.dylib')
    for binary in seen:
        assert not any(x.startswith(('/opt/','/usr/local/')) for x in dependencies(binary)),binary
    for binary in frameworks.iterdir():
        if not binary.is_symlink():run('codesign','--force','--sign','-',binary)
    return sorted(x.name for x in frameworks.iterdir())

def migrate_saves(support):
    for name,source in [('Unlocked',ROOT/'work/unlocked-profile'),('Continue',ROOT/'work/embedded-profile'),('Playthroughs',ROOT/'work/playthroughs')]:
        dest=support/name
        if source.is_dir() and not dest.exists():shutil.copytree(source,dest)
    latest=support/'Playthroughs/latest.txt'
    if latest.is_file():
        old=Path(latest.read_text().strip());prior=ROOT/'work/playthroughs'
        if old.is_relative_to(prior):latest.write_text(str(support/'Playthroughs'/old.relative_to(prior))+'\n')

def build(install=False):
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
    shutil.copy2(ROOT/'work/sm64ex-source/build/us_melee_bridge/sm64.us.f3dex2e',exe)
    shutil.copy2(RUNTIME/'libmelee_embedded.dylib',frameworks/'libmelee_embedded.dylib')
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
    for name in ('libcoopnet.dylib','libjuice.1.6.2.dylib'):
        shutil.copy2(coop_source.parent/name,frameworks/name)
    rom=ROOT/'work/sm64ex-source/baserom.us.z64';clone(rom,resources/'game/Mario64.z64')
    env.update(SMB_COOP_EXECUTABLE='${RESOURCES}/../MacOS/SuperMelee64Coop',SMB_PROFILE_COOP='${SUPPORT}/Coop',SMB_SM64_ROM='${RESOURCES}/game/Mario64.z64')
    (resources/'Game.plist').write_bytes(plistlib.dumps(dict(Environment=env,PrivateGameData=True)))
    libraries=bundle_libraries(exe,frameworks,(coop,))
    run('codesign','--force','--sign','-',stage)
    run('codesign','--verify','--deep','--strict',stage)
    target=Path('/Applications/Super Melee 64.app') if install else ROOT/'Super Melee 64.app'
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
    report=dict(app=str(target),native_executable=True,python_at_launch=False,shell_at_launch=False,game_data_bundled=True,external_library_paths=False,libraries=libraries,save_folder=str(support),icon_alpha=True,signature_verified=True,bytes=sum(p.stat().st_size for p in target.rglob('*') if p.is_file()))
    (ROOT/'work/native-app-build.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));return target
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--install',action='store_true');build(parser.parse_args().install)
