#!/usr/bin/env python3
"""Launch the native Melee bridge inside the 16-player Coop Deluxe host."""
from pathlib import Path
import argparse,json,os,shutil,subprocess,time
WINDOWS=os.name=='nt'
from native_lineup import BY_NAME
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2];RUNTIME=Path(os.environ.get('SMB_RUNTIME_DIR',ROOT/'work/melee-embedded'))
def environment(character='link',costume=0):
    settings=json.loads((ROOT/'work/install.json').read_text()) if (ROOT/'work/install.json').exists() else {}
    disc=settings.get('disc',str(Path.home()/'Downloads/Super Smash Bros. Melee (USA) (En,Ja) (Rev 2).iso'))
    files=RUNTIME/'assets'
    env={k:v for k,v in os.environ.items() if not k.startswith(('SMB_','SM64_MELEE_','MELEE_','MELEE3D_'))}
    env.update(SM64_MELEE_LIBRARY=str(RUNTIME/('melee_embedded.dll' if WINDOWS else 'libmelee_embedded.dylib')),SM64_MELEE_DISC=disc,
        SM64_MELEE_RESOURCES=str(RUNTIME/'resources'),MELEE_FILES_DIR=str(files),MELEE3D_AKANEIA_FILES=str(files),
        MELEE3D_NATIVE_DELFINO=str(ROOT/'work/castle-native-cache'),MELEE3D_NATIVE_DELFINO_LAYOUT='plaza',
        MELEE_BOOT_SCENE='vs',MELEE_DEBUG_VS_STAGE='32',MELEE3D_NATIVE_LAB='1',MELEE3D_NATIVE_LINEUP=str(BY_NAME[character][1]),
        MELEE3D_NATIVE_CHARACTER=character,MELEE3D_NATIVE_ROSTER='akaneia',MELEE3D_NATIVE_AKANEIA='1',MELEE3D_NATIVE_3D='1',
        MELEE3D_NATIVE_INPUTS='spatial',MELEE3D_NATIVE_CAMERA='combat',MELEE3D_NATIVE_CAMERA_TURN='snap',
        MELEE3D_NATIVE_HUMAN='1',MELEE3D_NATIVE_MOVE_SPEED='1.25',MELEE3D_NATIVE_GRAVITY='1',
        MELEE3D_NATIVE_ITEMS='off',MELEE3D_NATIVE_PARTY='battle',MELEE3D_NATIVE_ROUND_SECONDS='0',
        MELEE3D_NATIVE_STOCK_MINUTES='0',MELEE3D_NATIVE_RESULTS='off',MELEE_BACKEND='d3d12' if WINDOWS else 'metal',MELEE_VSYNC='0',MELEE_SEED='1',
        MELEE_CACHE_DIR=str(RUNTIME/'gpu-cache'),SMB_CAMERA='sm64',SMB_PLAYER_EXTERNAL=str(BY_NAME[character][1]),
        SMB_PLAYER_COSTUME=str(costume),SMB_DIFFICULTY='default',SMB_CPU_COUNT='0',SMB_COURSE_ENEMIES='1',SMB_QUICK_START='1',SMB_SAVE_MODE='continue',SMB_SELECTION_ART=str(ROOT/'work/character-select/cells.bin'))
    from build_selection_art import build
    env['SMB_SELECTION_ART']=str(build(course_enemies=True))
    support=Path(os.environ.get('APPDATA',str(Path.home())))/'Super Melee 64' if WINDOWS else Path.home()/'Library/Application Support/Super Melee 64'
    support.mkdir(parents=True,exist_ok=True);env['SMB_OPTIONS_PATH']=str(support/'Gameplay.options')
    return env
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--character',choices=BY_NAME,default='link');parser.add_argument('--costume',type=int,default=0)
    group=parser.add_mutually_exclusive_group();group.add_argument('--host',action='store_true');group.add_argument('--join')
    parser.add_argument('--difficulty',choices=['default','impossible','none'],default='default')
    parser.add_argument('--network-test',choices=['movement','combat','projectiles','campaign','partners','grab','rounds'],default='movement')
    parser.add_argument('--record',type=Path);parser.add_argument('--port',type=int,default=7777);parser.add_argument('--profile',type=Path)
    parser.add_argument('--all-stars',action='store_true');parser.add_argument('--scenario',choices=['basic','cannon','cannon-range','swim','swim-drown','shell-ride','wing-flight'],default='basic');parser.add_argument('--test-frames',type=int);parser.add_argument('--timeout',type=int,default=0)
    args=parser.parse_args();case=ROOT/'work/coop-runs'/str(time.time_ns());case.mkdir(parents=True)
    profile=args.profile or (case/'profile' if args.test_frames else (Path(os.environ.get('APPDATA',str(Path.home())))/'Super Melee 64/Coop' if WINDOWS else Path.home()/'Library/Application Support/Super Melee 64/Coop'))
    profile.mkdir(parents=True,exist_ok=True)
    if args.test_frames and args.profile is None:
        # Only the new isolated fixture profile receives packet diagnostics.
        # Never rewrite the player's persistent settings to investigate a join.
        (profile/'sm64config.txt').write_text('debug_info true\ndebug_error true\n')
    rom=ROOT/'work/sm64coopdx-melee/baserom.us.z64'
    if not (profile/'baserom.us.z64').exists():shutil.copy2(rom,profile/'baserom.us.z64')
    native=case/'native';native.mkdir()
    env=environment(args.character,args.costume);env['SMB_DIFFICULTY']=args.difficulty;env.update(SM64_MELEE_PROFILE=str(native),MELEE_LOG_FILE=str(case/'native.log'))
    if args.record:env.update(SMB_RECORD_AUTO='1',SMB_RECORD_PATH=str(args.record.resolve()))
    if args.all_stars:env['SMB_ALL_STARS']='1'
    if args.scenario!='basic':env.update(SMB_SCENARIO=args.scenario,SMB_INPUT_TRACE='1')
    if args.test_frames:env.update(SMB_SMOKE='1',SMB_SMOKE_FRAMES=str(args.test_frames),SMB_COOP_TEST=args.network_test,SMB_SCENE_CAPTURE=str(case/'scene.rgba'))
    executable=ROOT/('work/SuperMelee64-Windows/SuperMelee64Coop.exe' if WINDOWS else 'work/sm64coopdx-melee/build/us_pc/sm64coopdx')
    command=[str(executable),'--savepath',str(profile),'--windowed','--skip-intro','--skip-update-check','--no-discord','--disable-mods','--playername',args.character,'--playercount','16']
    if WINDOWS:command+=['--backend','0'] # The shared compositor uses OpenGL.
    if args.join:command+=['--client',args.join,str(args.port)]
    elif args.host or args.test_frames:command+=['--server',str(args.port)]
    print('Logs: '+str(case),flush=True)
    with (case/'game.log').open('w') as log:
        process=subprocess.Popen(command,cwd=executable.parent,env=env,stdout=log,stderr=subprocess.STDOUT)
        try:return process.wait(timeout=args.timeout or None)
        except subprocess.TimeoutExpired:
            process.terminate()
            try:process.wait(timeout=5)
            except subprocess.TimeoutExpired:process.kill();process.wait()
            return 124
if __name__=='__main__':raise SystemExit(main())
