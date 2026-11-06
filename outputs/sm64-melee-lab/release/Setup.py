#!/usr/bin/env python3
"""Build Super Melee 64 locally from a player's own verified game files."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,os,platform,plistlib,shlex,shutil,subprocess,sys,time
from setup_support import Session,SetupCancelled,filesystem_report,require_filesystem,checkout_pinned,atomic_json
from mac_toolchain import MacToolchainError,mac_gcc,mac_build_environment,mac_toolchain_report
from setup_diagnostics import diagnose,format_diagnostic
SESSION=None
CURRENT_STAGE=None

def root_path():
    for path in (Path(__file__).resolve().parent,*Path(__file__).resolve().parents):
        if (path/'outputs/sm64-melee-lab/native/bridge_protocol.h').is_file():return path
    raise RuntimeError('Keep Setup.py with its complete extracted source package')
ROOT=root_path();NATIVE=ROOT/'outputs/sm64-melee-lab/native'
sys.path.insert(0,str(NATIVE))
from build_bootstrap import PIN as ADAPTER_PIN, verify as verify_bootstrap, extract as extract_bootstrap, configure_local_sources
from package_release import VERSION
BOOTSTRAP=Path(__file__).resolve().parent/'SmashRoyale-bootstrap.zip'
PINS={'smash-royale':('bundled-source-bootstrap',ADAPTER_PIN),
      'sm64ex-source':('https://github.com/sm64pc/sm64ex.git','d7ca2c04364a6dd0dac58b47151e04e26887e6f0'),
      'sm64coopdx-melee':('https://github.com/coop-deluxe/sm64coopdx.git','8cd6e5977d9f920d51ca71f2c61801d019ed79c6')}
ROM_SHA1='9bef1128717f958171a4afac3ed78ee2bb4e86ce'
MELEE_MD5='0e63d4223b01d9aba596259dc155a174'

def digest(path,algorithm):
    h=hashlib.new(algorithm)
    with path.open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''):h.update(block)
    return h.hexdigest()
def verify_inputs(rom,disc):
    if not rom.is_file() or digest(rom,'sha1')!=ROM_SHA1:raise ValueError('SM64: use the original US big-endian .z64 ROM (8 MiB). Other revisions/regions are unsupported.')
    if not disc.is_file() or digest(disc,'md5')!=MELEE_MD5:raise ValueError('Melee: use an unmodified USA Rev 2 GameCube disc image (GALE01 revision 2). RVZ/NKit/patched images are unsupported.')
    with disc.open('rb') as stream:header=stream.read(8)
    if header[:6]!=b'GALE01' or header[7]!=2:raise ValueError('Melee disc header is not USA Rev 2')
    return {'sm64_sha1':ROM_SHA1,'melee_md5':MELEE_MD5}
def choose(title,types):
    if sys.platform=='darwin':
        result=subprocess.run(['osascript','-e','POSIX path of (choose file with prompt '+json.dumps(title)+')'],text=True,capture_output=True)
        if result.returncode:raise SystemExit('Setup cancelled; no game files were changed.')
        return Path(result.stdout.strip()).resolve()
    try:
        import tkinter as tk
        from tkinter.filedialog import askopenfilename
        window=tk.Tk();window.withdraw();value=askopenfilename(title=title,filetypes=types);window.destroy()
        if not value:raise SystemExit('Setup cancelled; no game files were changed.')
        return Path(value).resolve()
    except ImportError:raise RuntimeError('Pass --rom /path/to/game.z64 --melee /path/to/game.iso (file picker unavailable).')
def run(command,**kw):
    if SESSION:return SESSION.run(command,**kw)
    print('\n'+subprocess.list2cmdline([str(x) for x in command]),flush=True)
    subprocess.run([str(x) for x in command],check=True,**kw)
def checkout(name,path):
    url,revision=PINS[name]
    return checkout_pinned(url,revision,path,run)

def preflight(target):
    required=['git','cmake','ninja','patch','xdelta3','curl']
    if target=='mac':required+=['clang','clang++','pkg-config','sdl2-config','gmake']
    else:required+=['x86_64-w64-mingw32-gcc','x86_64-w64-mingw32-g++','x86_64-w64-mingw32-ar','sh','make']
    missing=[x for x in required if not shutil.which(x)]
    if target=='mac' and not mac_gcc():missing.append('Homebrew GCC (Prepare Build Tools)')
    if not (shutil.which('7zz') or shutil.which('7z')):missing.append('7z/7zz')
    if not (shutil.which('gmake') or shutil.which('make')):missing.append('make')
    if not importlib.util.find_spec('PIL'):missing.append('Python Pillow')
    if sys.version_info<(3,12):missing.append('Python 3.12 or newer')
    return missing

def windows_environment():
    """Portable settings: tokens expand inside the native local launcher."""
    env=dict(SM64_MELEE_LIBRARY='${ROOT}/work/SuperMelee64-Windows/melee_embedded.dll',
        SM64_MELEE_RESOURCES='${ROOT}/work/melee-embedded/resources',
        SM64_MELEE_PROFILE='${SUPPORT}/Native',SM64_MELEE_DISC='${ROOT}/work/game-inputs/Melee.iso',
        SMB_SM64_ROM='${ROOT}/work/sm64ex-source/baserom.us.z64',SMB_COOP_EXECUTABLE='${ROOT}/work/SuperMelee64-Windows/SuperMelee64Coop.exe',
        SMB_PROFILE_COOP='${SUPPORT}/Coop',SMB_PROFILE_UNLOCKED='${SUPPORT}/Unlocked',SMB_PROFILE_CONTINUE='${CONTINUE_PROFILE}',
        SMB_PROFILE_NEW='${NEW_PROFILE}',SMB_PROFILE_RECOMMENDED='${SUPPORT}/Recommended',
        SMB_PLAYTHROUGH_LATEST='${SUPPORT}/Playthroughs/latest.txt',
        SMB_OPTIONS_PATH='${SUPPORT}/Gameplay.options',SMB_RECORD_DIR='${MOVIES}/Super Melee 64',
        SMB_QUICK_START='1',SMB_CHARACTER_SELECT='1',SMB_SELECTION_ART='${ROOT}/work/melee-selection-course/selection.bin',
        SMB_LOGO='${ROOT}/outputs/sm64-melee-lab/branding/logo.rgba',SMB_CAMERA='sm64',SMB_CPU_COUNT='3',
        SMB_CPU_CHARACTERS='fox,marth,wolf',SMB_COURSE_ENEMIES='1',SMB_DIFFICULTY='default',SMB_ALL_STARS='1',SMB_SAVE_MODE='unlocked',
        MELEE_BOOT_SCENE='vs',MELEE_DEBUG_VS_STAGE='32',MELEE3D_NATIVE_LAB='4',MELEE3D_NATIVE_ROSTER='akaneia',
        MELEE3D_NATIVE_LINEUP='6,2,9,34',MELEE3D_NATIVE_MOVE_SPEED='1.25',MELEE3D_NATIVE_GRAVITY='1',
        MELEE3D_NATIVE_HUMAN='1',MELEE3D_NATIVE_CHARACTER='link',MELEE_FILES_DIR='${ROOT}/work/melee-embedded/assets',
        MELEE3D_AKANEIA_FILES='${ROOT}/work/melee-embedded/assets',MELEE3D_NATIVE_AKANEIA='1',MELEE3D_NATIVE_3D='1',
        MELEE3D_NATIVE_INPUTS='spatial',MELEE3D_NATIVE_CAMERA='combat',MELEE3D_NATIVE_CAMERA_TURN='snap',
        MELEE3D_NATIVE_DELFINO='${ROOT}/work/castle-native-cache',MELEE3D_NATIVE_DELFINO_LAYOUT='plaza',
        MELEE3D_NATIVE_ITEMS='off',MELEE3D_NATIVE_PARTY='battle',MELEE3D_NATIVE_ROUND_SECONDS='0',
        MELEE3D_NATIVE_STOCK_MINUTES='0',MELEE3D_NATIVE_RESULTS='off',MELEE_BACKEND='d3d12',MELEE_VSYNC='0',
        MELEE_SEED='1',MELEE_CACHE_DIR='${SUPPORT}/Cache',MELEE_WINDOW_TITLE='Super Melee 64',MELEE_LOG_FILE='${SUPPORT}/native-game.log')
    assert all('\n' not in x and '\r' not in x for x in env.values())
    return env

def create_windows_launcher():
    """Compile the small native launcher and write only local runtime settings."""
    target=ROOT/'Super Melee 64.exe'
    run(['x86_64-w64-mingw32-g++','-std=c++17','-O2','-municode','-mwindows','-static',
         Path(__file__).parent/'WindowsLauncher.cpp','-o',target,'-lshell32','-lole32','-luuid'])
    env=windows_environment()
    (ROOT/'SuperMelee64.ini').write_text('[Environment]\n'+''.join(k+'='+v+'\n' for k,v in env.items()),encoding='utf-16')
    return target

def windows_engine(upstream):
    # Upstream's Windows command assumes these generators already ran on Mac.
    # Run all of them explicitly for a fresh Windows installation.
    code='''from native_mac import stage
from native_bridge import prepare_bridge
from native_roster import generate
from native_bindings import generate as bindings
from native_reference import generate as reference
from native_compatibility import WORK
stage();prepare_bridge(destination=WORK/'mac-runtime');reference();generate();bindings()
from native_windows import build
build()
'''
    run([sys.executable,'-c',code],cwd=upstream/'tools/melee3d')

def create_mac_launcher():
    run([sys.executable,NATIVE/'build_mac_app.py'])
    return ROOT/'Super Melee 64.app'

def copy_owned_input(source,target,algorithm,expected):
    if target.exists():
        if digest(target,algorithm)!=expected:raise RuntimeError('Preserving a different existing local input: '+str(target))
        return
    target.parent.mkdir(parents=True,exist_ok=True);partial=target.with_name(target.name+'.partial')
    with source.open('rb') as incoming,partial.open('wb') as outgoing:
        for block in iter(lambda:incoming.read(1024*1024),b''):
            if SESSION:SESSION.check_cancel()
            outgoing.write(block)
    if digest(partial,algorithm)!=expected:raise RuntimeError('Copied input verification failed; original input is unchanged.')
    partial.replace(target)

def main():
    global SESSION,CURRENT_STAGE
    CURRENT_STAGE=None
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--platform',choices=['mac','windows'],default='windows' if os.name=='nt' else 'mac')
    parser.add_argument('--rom',type=Path);parser.add_argument('--melee',type=Path)
    parser.add_argument('--check',action='store_true',help='Verify inputs and report prerequisites; do not download, build, or launch')
    parser.add_argument('--launch',action='store_true',help='Open the game after a successful build')
    parser.add_argument('--check-tools',action='store_true',help='Report prerequisites and disk space without reading game files or making changes')
    parser.add_argument('--cancel-file',type=Path,help=argparse.SUPPRESS)
    parser.add_argument('--minimum-free-gib',type=float,default=12,help='Minimum free build space (default 12 GiB; recommended 20 GiB)')
    args=parser.parse_args()
    if not 1<=args.minimum_free_gib<=1024:parser.error('minimum-free-gib must be between 1 and 1024')
    if args.check_tools:
        report=dict(platform=args.platform,missing=preflight(args.platform),filesystem=filesystem_report(ROOT,args.minimum_free_gib))
        if args.platform=='mac' and sys.platform=='darwin':report['mac_toolchain']=mac_toolchain_report()
        print(json.dumps(report,indent=2));return 0 if report.get('mac_toolchain',{}).get('ok',True) else 2
    rom=args.rom.expanduser().resolve() if args.rom else choose('Select your original US Super Mario 64 ROM',[('SM64 ROM','*.z64'),('All files','*')])
    disc=args.melee.expanduser().resolve() if args.melee else choose('Select your original Melee USA Rev 2 disc',[('GameCube disc','*.iso *.gcm'),('All files','*')])
    identities=verify_inputs(rom,disc);missing=preflight(args.platform)
    bootstrap=verify_bootstrap(BOOTSTRAP)
    report=dict(platform=args.platform,input_checks=identities,missing=missing,downloaded=False,played=False,
        source_bootstrap=dict(revision=bootstrap['revision'],files=len(bootstrap['files']),sha256=digest(BOOTSTRAP,'sha256')))
    if args.check:
        if args.platform=='mac' and sys.platform=='darwin':report['mac_toolchain']=mac_toolchain_report()
        print(json.dumps(report,indent=2));return 0 if not missing and report.get('mac_toolchain',{}).get('ok',True) else 2
    if missing:raise RuntimeError('Install the prerequisites in START HERE.txt: '+', '.join(missing))
    require_filesystem(ROOT,args.minimum_free_gib)
    if args.platform=='mac' and (sys.platform!='darwin' or platform.machine()!='arm64' or int(platform.mac_ver()[0].split('.')[0] or 0)<15):raise RuntimeError('The Mac target currently requires Apple Silicon and macOS 15+.')
    with Session(ROOT,dict(inputs=identities,pins=PINS,bootstrap=report['source_bootstrap'],version=VERSION),args.cancel_file) as session:
        SESSION=session
        try:
            print('Installer version: '+VERSION,flush=True)
            (ROOT/'work/setup-error.json').unlink(missing_ok=True)
            build_env=None
            if args.platform=='mac':
                print('Checking Apple SDK and Melee/Apple compiler headers before downloads...',flush=True)
                build_env,toolchain=mac_build_environment()
                print('Mac compiler checks passed. SDK: '+toolchain['sdk']+'; GCC: '+toolchain['gcc'],flush=True)
                session.check_cancel()
            return install(args,rom,disc,identities,build_env)
        except MacToolchainError as error:
            atomic_json(ROOT/'work/setup-error.json',error.diagnostic)
            raise
        finally:SESSION=None

def install(args,rom,disc,identities,build_env=None):
    work=ROOT/'work';work.mkdir(exist_ok=True)
    def stage(number,label):
        global CURRENT_STAGE
        CURRENT_STAGE=dict(number=number,total=12,label=label)
        SESSION.stage(number,12,label)
    stage(1,'Prepare pinned source dependencies')
    upstream=work/'deps/smash-royale';host=work/'sm64ex-source'
    extract_bootstrap(BOOTSTRAP,upstream)
    checkout('sm64ex-source',host)
    coop=work/'sm64coopdx-melee';checkout('sm64coopdx-melee',coop)
    coop_rom=coop/'baserom.us.z64'
    if coop_rom.exists() and digest(coop_rom,'sha1')!=ROM_SHA1:raise RuntimeError('Preserving a different existing co-op ROM: '+str(coop_rom))
    if not coop_rom.exists():copy_owned_input(rom,coop_rom,'sha1',ROM_SHA1)
    target=host/'baserom.us.z64'
    if target.exists() and digest(target,'sha1')!=ROM_SHA1:raise RuntimeError('Preserving a different existing host ROM: '+str(target))
    if not target.exists():copy_owned_input(rom,target,'sha1',ROM_SHA1)
    env=dict(os.environ if build_env is None else build_env,SMB_UPSTREAM_ROOT=str(upstream),SMB_BUILD_TARGET=args.platform,
        SMB_RUNTIME_DIR=str(work/'melee-embedded'),SM64_MELEE_DISC=str(disc),
        SMB_UPSTREAM_REVISION=PINS['smash-royale'][1])
    # Framework Python may have no bundled CA certificates. Keep HTTPS
    # verification enabled and use the macOS trust bundle for all child tools.
    if args.platform=='mac':env.setdefault('SSL_CERT_FILE','/etc/ssl/cert.pem')
    # Keep all game/reference inputs inside the installer-owned dependency.
    # Another Smash Royale installation must not influence this build.
    configure_local_sources(upstream);SESSION.done()
    stage(2,'Download official patches and prepare your local Melee assets')
    run([sys.executable,Path(__file__).parent/'prepare_installer_inputs.py','--adapter',upstream,'--melee',disc],env=env)
    SESSION.done();stage(3,'Compile the native fighter engine')
    if args.platform=='mac':run([sys.executable,upstream/'tools/melee3d/native_mac.py','build'],env=env)
    else:
        # Pass the same environment through the generator/build subprocesses.
        oldenv=dict(os.environ);os.environ.update(env)
        try:windows_engine(upstream)
        finally:os.environ.clear();os.environ.update(oldenv)
    # The upstream build generates the roster but its play command normally
    # stages these verified assets. Setup must do that without launching it.
    SESSION.done();stage(4,'Stage locally extracted fighters and sounds')
    run([sys.executable,'-c',"from native_roster import inventory; from native_live import stage_assets; stage_assets(inventory(),world=True)"],cwd=upstream/'tools/melee3d',env=env)
    SESSION.done();stage(5,'Prepare Mario64 geometry from your ROM')
    run([sys.executable,NATIVE/'compile_castle.py','--rom',rom,'--melee-root',upstream,'--output',work/'castle-native-cache'],env=env)
    SESSION.done();stage(6,'Prepare branding and stage encounters')
    run([sys.executable,NATIVE/'build_branding.py'],env=env)
    run([sys.executable,NATIVE/'build_campaign.py'],env=env)
    SESSION.done();stage(7,'Prepare the isolated native integration')
    run([sys.executable,NATIVE/'refresh_melee.py'],env=env)
    SESSION.done();stage(8,'Compile the single-player game')
    run([sys.executable,NATIVE/'build_adventure.py'],env=env)
    SESSION.done();stage(9,'Compile multiplayer')
    run([sys.executable,NATIVE/'build_coop.py'],env=env)
    SESSION.done();stage(10,'Prepare portraits and local launch settings')
    run([sys.executable,'-c','from build_selection_art import build; print(build(course_enemies=True))'],cwd=NATIVE,env=env)
    disc_path=subprocess.check_output(['wslpath','-w',str(disc)],text=True).strip() if args.platform=='windows' and os.environ.get('WSL_DISTRO_NAME') else str(disc)
    config=dict(platform=args.platform,disc=disc_path,rom=str(rom),upstream=str(upstream),pins=PINS,inputs=identities,version=VERSION)
    (work/'install.json').write_text(json.dumps(config,indent=2)+'\n')
    SESSION.done();stage(11,'Build the app launcher')
    if args.platform=='mac':print('Launcher: '+str(create_mac_launcher()))
    else:
        inputs=work/'game-inputs';inputs.mkdir(exist_ok=True);owned_disc=inputs/'Melee.iso'
        if not owned_disc.exists():copy_owned_input(disc,owned_disc,'md5',MELEE_MD5)
        elif digest(owned_disc,'md5')!=MELEE_MD5:raise RuntimeError('A different existing local disc copy was preserved: '+str(owned_disc))
        print('Launcher: '+str(create_windows_launcher()))
    SESSION.done();stage(12,'Complete')
    print('\nBuilt locally. Use Play Super Melee 64. Windows runtime remains an unverified release candidate.' if args.platform=='windows' else '\nBuilt locally. Use Play Super Melee 64.')
    if args.launch:
        run(['open',ROOT/'Super Melee 64.app'] if args.platform=='mac' else [ROOT/'Super Melee 64.exe'])
    SESSION.done()
    return 0
def entrypoint():
    try:return main()
    except SetupCancelled as error:print(str(error),file=sys.stderr);return 130
    except (ValueError,RuntimeError,OSError,subprocess.CalledProcessError) as error:
        diagnostic=diagnose(error,stage=CURRENT_STAGE,version=VERSION)
        # Early input/tool/disk failures happen before Session opens setup.log.
        # Save their explanation too, so either desktop helper can display it.
        if not any(flag in sys.argv for flag in ('--check','--check-tools')):
            try:atomic_json(ROOT/'work/setup-error.json',diagnostic)
            except OSError:pass # A read-only/full disk must not hide the error.
        print('\n'+format_diagnostic(diagnostic)+'\nLogs: setup-gui.log (Mac), setup-events.log (Windows), setup.log',file=sys.stderr)
        return 1

if __name__=='__main__':raise SystemExit(entrypoint())
