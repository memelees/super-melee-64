#!/usr/bin/env python3
"""Run SM64 and native Melee in one game process.

SM64 hosts the adventure and camera. Native Melee supplies fighter simulation,
character/item rendering and combat capsules. This launcher tests the
single-player host; verify_coop_features.py exercises the network host.
A full adventure completion is not certified by these bounded checks.
"""
from pathlib import Path
import argparse,hashlib,json,os,plistlib,re,shutil,subprocess,sys,time
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
RUNTIME=ROOT/'work/melee-embedded'
WINDOWS=os.name=='nt'
INSTALL=json.loads((ROOT/'work/install.json').read_text()) if (ROOT/'work/install.json').exists() else {}
DISC=Path(INSTALL.get('disc',str(Path.home()/'Downloads/Super Smash Bros. Melee (USA) (En,Ja) (Rev 2).iso')))
LIBRARY=RUNTIME/('melee_embedded.dll' if WINDOWS else 'libmelee_embedded.dylib')
from native_lineup import BY_NAME, allowed_kinds
from session_lock import acquire, release
def verify_private_build():
    manifest=json.loads((RUNTIME/'source-pin.json').read_text())
    library=LIBRARY
    if hashlib.sha256(library.read_bytes()).hexdigest()!=manifest['library_sha256']:
        raise ValueError('Private embedded library differs from its build manifest')
    return {'executable_sha256':manifest['original_executable']}


def bundle_host():
    if WINDOWS:
        exe=ROOT/'work/SuperMelee64-Windows/SuperMelee64.exe'
        if not exe.is_file():raise RuntimeError('Run Setup for Windows before launching')
        return exe
    app=ROOT/'work/Super Melee 64.app';exe=app/'Contents/MacOS/sm64'
    exe.parent.mkdir(parents=True,exist_ok=True)
    shutil.copytree(RUNTIME/'resources',app/'Contents/Resources',dirs_exist_ok=True)
    shutil.copy2(HERE.parent/'branding/Super Melee 64.icns',app/'Contents/Resources/Super Melee 64.icns')
    (RUNTIME/'gpu-cache').mkdir(exist_ok=True)
    shutil.copy2(ROOT/'work/sm64ex-source/build/us_melee_bridge/sm64.us.f3dex2e',exe)
    (app/'Contents/Info.plist').write_bytes(plistlib.dumps(dict(
        CFBundleIdentifier='dev.sm64.melee-embedded',CFBundleExecutable='sm64',
        CFBundleName='Super Melee 64',CFBundleDisplayName='Super Melee 64',CFBundlePackageType='APPL',
        CFBundleVersion='0.4',CFBundleIconFile='Super Melee 64.icns',NSHighResolutionCapable=True)))
    subprocess.run(['codesign','--force','--sign','-',str(app)],check=True,capture_output=True)
    return exe


def run(args):
    course_enemies=args.course_enemies or args.difficulty is not None or (not args.smoke and args.enemies is None and args.cpus is None)
    select=args.select or (args.character is None and not args.smoke)
    if args.stage is not None:
        stage=json.loads((HERE/'campaign.json').read_text())['stages'][args.stage]
        args.character=args.character or stage['player']
        course_enemies=True
    args.character=args.character or 'link' 
    enemies=args.enemies.split(',') if args.enemies else None
    if enemies and (len(enemies)>3 or any(n not in BY_NAME for n in enemies)):
        raise ValueError('--enemies requires one to three comma-separated fighter names')
    args.cpus=0 if args.difficulty=='none' else 3 if course_enemies else args.cpus if args.cpus is not None else (len(enemies) if enemies else 0 if args.smoke else 3)
    if enemies and len(enemies)!=args.cpus:raise ValueError('--cpus must match the number of --enemies')
    enemies=enemies or [n for n in ('fox','marth','wolf','mario') if n!=args.character][:args.cpus]
    enemy_names=list(enemies)
    pin=verify_private_build();lock=RUNTIME/'.sm64-running'
    try:
        lock_inode=acquire(lock,(
            str(ROOT/'work/Super Melee 64.app/Contents/MacOS/sm64'),
        ))
    except FileExistsError:
        print('Super Melee 64 is already running. Close that adventure before changing its character.',flush=True)
        return 2
    game=None
    try:
        exe=bundle_host();files=RUNTIME/'assets'
        library=LIBRARY
        if not library.is_file():raise RuntimeError('Run native/build_embedded.py first')
        case=ROOT/'work/embedded-runs'/str(time.time_ns());case.mkdir(parents=True)
        playthroughs=ROOT/'work/playthroughs';playthroughs.mkdir(exist_ok=True)
        latest=playthroughs/'latest.txt'
        continued=Path(latest.read_text().strip()) if latest.exists() else ROOT/'work/embedded-profile'
        fresh=playthroughs/time.strftime('%Y-%m-%d_%H-%M-%S')
        if fresh.exists():fresh=playthroughs/(fresh.name+'-'+str(time.time_ns()))
        unlocked=ROOT/'work/unlocked-profile'
        recommended=ROOT/'work/recommended-stages'
        mode='recommended' if args.stage is not None else 'new' if args.new_game else 'continue' if args.story else 'unlocked'
        profile=args.save_profile.resolve() if args.save_profile else (case/'save' if args.smoke else fresh if args.new_game else continued if args.story else recommended if args.stage is not None else unlocked)
        if args.smoke or args.save_profile:recommended=profile
        if args.stage is not None:profile=recommended/f'stage-{args.stage:02d}'
        profile.mkdir(parents=True,exist_ok=True)
        recommended.mkdir(parents=True,exist_ok=True)
        if args.smoke or args.save_profile:unlocked=continued=fresh=profile
        else:unlocked.mkdir(parents=True,exist_ok=True);continued.mkdir(parents=True,exist_ok=True)
        native_profile=case/'native-profile';native_profile.mkdir()
        env={k:v for k,v in os.environ.items() if not k.startswith(('MELEE_','MELEE3D_','SMB_','SM64_MELEE_'))}
        env.update(SM64_MELEE_LIBRARY=str(library),SM64_MELEE_DISC=str(DISC),
            SM64_MELEE_RESOURCES=str(RUNTIME/'resources'),
            SM64_MELEE_PROFILE=str(native_profile),
            SM64_MELEE_MODEL=str(ROOT/'work/link-native-cache/link.smm'),SMB_QUICK_START='1',
            SMB_LOGO=str(HERE.parent/'branding/logo.rgba'),SMB_CAMERA=args.camera,SMB_CPU_COUNT=str(args.cpus),SMB_CPU_CHARACTERS=','.join(enemies),MELEE_BOOT_SCENE='vs',MELEE_DEBUG_VS_STAGE='32',MELEE3D_NATIVE_LAB=str(args.cpus+1),
            MELEE3D_NATIVE_ROSTER='akaneia',MELEE3D_NATIVE_LINEUP=','.join(str(BY_NAME[n][1]) for n in [args.character]+enemies),
            MELEE3D_NATIVE_MOVE_SPEED=str(args.speed),MELEE3D_NATIVE_GRAVITY='1',
            MELEE3D_NATIVE_HUMAN='1',MELEE3D_NATIVE_CHARACTER=args.character,
            MELEE_FILES_DIR=str(files),MELEE3D_AKANEIA_FILES=str(files),
            MELEE3D_NATIVE_AKANEIA='1',MELEE3D_NATIVE_3D='1',MELEE3D_NATIVE_INPUTS='spatial',
            MELEE3D_NATIVE_CAMERA='combat',MELEE3D_NATIVE_CAMERA_TURN='snap',MELEE3D_NATIVE_DELFINO=str(ROOT/'work/castle-native-cache'),
            MELEE3D_NATIVE_DELFINO_LAYOUT='plaza',MELEE3D_NATIVE_ITEMS='off',
            MELEE3D_NATIVE_PARTY='battle',MELEE3D_NATIVE_ROUND_SECONDS='0',MELEE3D_NATIVE_STOCK_MINUTES='0',MELEE3D_NATIVE_RESULTS='off',
            MELEE_BACKEND='d3d12' if WINDOWS else 'metal',MELEE_VSYNC='0',MELEE_SEED='1',MELEE_CACHE_DIR=str(ROOT/'work/melee-embedded/gpu-cache'),
            MELEE_WINDOW_TITLE='Loading embedded fighter',MELEE_LOG_FILE=str(case/'native-game.log'))
        if args.wall_jumps:env['SMB_UNIVERSAL_WALL_JUMP']='1'
        if args.scenario=='kirby-copy':
            env['SMB_COPY_TEST_KIND']=str(args.copy_kind)
            env['SMB_PLAYER_COSTUME']=str(args.copy_costume)
        if not args.smoke:
            options=Path(os.environ.get('APPDATA',str(Path.home())))/'Super Melee 64' if WINDOWS else Path.home()/'Library/Application Support/Super Melee 64'
            options.mkdir(parents=True,exist_ok=True);env['SMB_OPTIONS_PATH']=str(options/'Gameplay.options')
        if args.stage is not None:env['SMB_START_STAGE']=str(args.stage)
        if course_enemies:env.update(SMB_COURSE_ENEMIES='1',SMB_DIFFICULTY=args.difficulty or 'default')
        if args.stage is None and (args.all_stars or (not args.smoke and mode=='unlocked')):env['SMB_ALL_STARS']='1'
        recordings=(ROOT/'outputs/sm64-melee-lab/Recordings') if WINDOWS else Path.home()/'Movies/Super Melee 64';recordings.mkdir(parents=True,exist_ok=True)
        env.update(SMB_SAVE_MODE=mode,SMB_PROFILE_UNLOCKED=str(unlocked),SMB_PROFILE_RECOMMENDED=str(recommended),SMB_PROFILE_CONTINUE=str(continued),SMB_PROFILE_NEW=str(fresh),SMB_PLAYTHROUGH_LATEST=str(latest),SMB_RECORD_DIR=str(recordings))
        if args.record is not None:
            env['SMB_RECORD_AUTO']='1'
            if args.record:env['SMB_RECORD_PATH']=str(Path(args.record).expanduser().resolve())
        from build_selection_art import build
        env['SMB_SELECTION_ART']=str(build(course_enemies=course_enemies))
        if select:
            env['SMB_CHARACTER_SELECT']='1'
            if args.smoke:env['SMB_SELECT_AUTOSTART']='1'
        if args.title_preview:
            env.update(SMB_TITLE_PREVIEW='1',SMB_TITLE_CAPTURE=str(case/'title.rgba'),SMB_SELECT_AUTOSTART='1')
        if args.smoke and args.scenario=='character-switch':env.update(SMB_SELECT_AUTOSTART='1',SMB_MENU_CAPTURE=str(case/'change-menu.rgba'))
        if args.scenario!='basic':env['SMB_SCENARIO']=args.scenario
        if args.scenario=='stairs':env['SMB_STAIR_ORDINAL']=str(args.stair_ordinal)
        if args.sound_diagnostic:env.update(SMB_AUDIO_TRACE='1',SMB_SOUND_TRACE='1',MELEE_AUDIO_DUMP=str(case/'native-audio.f32'))
        if args.scenario=='ground-hud':env['SMB_HUD_OCCLUSION_PROBE']='1'
        if args.scenario=='effects':env['SMB_EFFECT_PROBE']='1'
        if args.scenario=='lighting':env['SMB_LIGHTING_PROBE']=str(case/'native-color.rgba')
        if args.legacy_lighting:env['SMB_LIGHTING_LEGACY']='1'
        if args.effect_depth_off:env['SMB_EFFECT_DEPTH_OFF']='1'
        if args.slope_jump:env['SMB_SLOPE_JUMP']='1'
        if args.slope_steer:env['SMB_SLOPE_STEER']='1'
        if args.input_diagnostic:env['SMB_INPUT_TRACE']='1'
        if args.grab_diagnostic and args.smoke:env['SMB_TEST_NETWORK_GRAB']='1'
        if args.remote_diagnostic and args.smoke:env['SMB_TEST_REMOTE_ARTICLES']='1'
        if args.water_diagnostic and args.smoke:env['SMB_TEST_WATER_PHYSICS']='1'
        if args.roster_test:env['SMB_ROSTER_TEST']='1'
        if args.render_diagnostic:env['SMB_RENDER_DIAGNOSTIC']='1'
        if args.model_diagnostic or args.scenario=='model-forms':env['SMB_MODEL_TRACE']='1'
        if args.legacy_model_visibility:env['SMB_MODEL_CACHE_LEGACY']='1'
        if args.smoke:env['SMB_SCENE_CAPTURE']=str(case/'scene.rgba')
        if args.smoke:env.update(SMB_SMOKE='1',SMB_SMOKE_FRAMES=str(args.frames))
        if args.smoke and not args.benchmark:env['SMB_EMBED_TEST_PAUSE']='1'
        if args.render_height is not None:env['SMB_RENDER_MAX_HEIGHT']=str(args.render_height)
        print('Super Melee 64 — native '+args.character+'.',flush=True)
        print('One visible SM64 window; native Melee rendering runs in the background.',flush=True)
        print('F6 / controller Back: change character and costume. F8 / L3: summon the next themed enemy round. V / LT / RT cycles costumes in the picker.',flush=True)
        print('WASD move; L jump; comma attack; K special; right Shift shield; left Shift grab; Q/E up/down move modifiers (L1/L2 also swim up/down in water); arrow keys control the camera. F9 starts/stops recording.',flush=True)
        print(f'Save: {profile}; movement speed: {args.speed:g}x; all stars: {"SMB_ALL_STARS" in env}',flush=True)
        print('Logs: '+str(case),flush=True)
        started=time.monotonic()
        with (case/'game.log').open('w') as log:
            command=[str(exe)]+([] if WINDOWS else ['-ApplePersistenceIgnoreState','YES','-NSQuitAlwaysKeepsWindows','NO'])+['--windowed','--savepath',str(profile)]
            if not args.smoke and not args.timeout and not WINDOWS:
                # A normal launcher becomes the game; there is no watcher or
                # second simulation process. The game releases its own guard.
                env.update(SMB_SESSION_LOCK=str(lock),SMB_SESSION_INODE=str(lock_inode))
                os.dup2(log.fileno(),1);os.dup2(log.fileno(),2)
                os.chdir(profile)
                os.execve(str(exe),command,env)
            game=subprocess.Popen(command,
                cwd=profile,env=env,stdout=log,stderr=subprocess.STDOUT)
            deadline=started+args.timeout if args.timeout else None
            samples=[]
            while game.poll() is None:
                if deadline and time.monotonic()>deadline:raise TimeoutError('Embedded test exceeded timeout; '+str(case))
                # Record direct children without treating pgrep's no-match exit as an error.
                children=[] if WINDOWS else subprocess.run(['pgrep','-P',str(game.pid)],text=True,capture_output=True).stdout.splitlines()
                if children:samples.append(children)
                time.sleep(.25)
            code=game.returncode
        elapsed=time.monotonic()-started
    finally:
        if game and game.poll() is None:
            game.terminate()
            try:game.wait(timeout=5)
            except subprocess.TimeoutExpired:game.kill();game.wait()
        release(lock,lock_inode)
    verify_private_build()
    text=(case/'game.log').read_text()
    if args.title_preview:
        result=dict(title_only=True,played=False,exit_code=code,passed=code==0 and 'original_sm64_scene=1' in text and 'SMB TITLE COMPLETE' in text and any(int(n)>1000 for n in re.findall(r'SMB TITLE FIGHTER pixels=(\d+)',text)) and (case/'title.rgba').is_file(),log=str(case/'game.log'))
        (case/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2));return 0 if result['passed'] else 1
    selected=re.search(r'SMB SELECT CONFIRM character=(\w+) external=\d+ cpus=(\d+)',text)
    if selected:
        args.character=selected[1];args.cpus=int(selected[2])
        enemy_names=[name for slot,name in re.findall(r'SMB SELECT SLOT slot=(\d+) character=(\w+)',text) if int(slot)>0]
    selected_mode=re.search(r'SMB SELECT MODE name=(\w+)',text)
    if selected_mode:args.difficulty=selected_mode[1]
    rows=[{k:float(v) for k,v in re.findall(r'(\w+)=(-?[\d.]+)',line)}
          for line in text.splitlines() if line.startswith('SMB HOST FRAME')]
    native_render=[{k:float(v) for k,v in re.findall(r'(\w+)=(-?[\d.]+)',line)} for line in text.splitlines() if line.startswith('SMB HOST NATIVE RENDER')]
    render=re.findall(r'SMB HOST RENDER frame=(\d+) triangles=(\d+) mapped_joints=(\d+)',text)
    native_pids=re.findall(r'SMB EMBED READY pid=(\d+)',text)
    host_pids=re.findall(r'SMB HOST EMBEDDED pid=(\d+)',text)
    failures=[line for line in text.splitlines() if 'SMB HOST FAIL' in line or 'SMB EMBED FAIL' in line]
    checks=dict(completed=code==0 and f'SMB HOST SMOKE COMPLETE frames={args.frames}' in text,
        one_game_process=native_pids==host_pids==[str(game.pid)] and not samples,
        native_map_render_disabled=bool(native_pids) and 'M3D DELFINO RENDER' not in text,
        two_native_ticks_per_host_frame=f'SMB HOST EMBED COMPLETE pid={game.pid} host_frames={args.frames} native_ticks={args.frames*2}' in text,
        pause_resume='SMB HOST PAUSE begin native_ticks=180' in text and 'SMB HOST PAUSE end native_ticks=180' in text if args.frames>=90 and not args.benchmark else True,
        fighter=bool(rows) and all(r['kind'] in allowed_kinds(BY_NAME[args.character][2]) for r in rows),
        native_render=any(r.get('pixels',0)>100 and r.get('rgb_pixels',0)>100 and 0<=r['depth_min']<.9999 and r['depth_min']<=r['depth_max']<=1 for r in native_render),
        live_collision='SMB WORLD REBUILD' in text,
        airborne=any(r['grounded']==0 for r in rows),active_attack=any(r['hitboxes']>0 for r in rows),
        world_movement=bool(rows) and max((r['x']-rows[0]['x'])**2+(r['z']-rows[0]['z'])**2 for r in rows)>300**2)
    if args.scenario=='kirby-copy':
        for key in ('airborne','active_attack','world_movement'):checks.pop(key,None)
        checks['copy_ability_and_hat']='SMB KIRBY COPY PASS ' in text and 'special=1 native_render=1' in text
    if args.scenario=='stage':
        for key in ('airborne','active_attack','world_movement'):checks.pop(key)
        starts=re.findall(r'SMB STAGE READY stage=(\d+) level=(\d+) kind=(\d+) frame=(\d+)',text)
        checks['selected_stage_runs']=args.stage is not None and any(int(stage)==args.stage and int(frame)>=150 for stage,level,kind,frame in starts)
        if args.difficulty!='none':checks['stage_enemy_active']='SMB NPC SPAWN slot=1' in text
        checks['stage_uses_earned_progress']='SMB SAVE PROFILE mode=recommended' in text and 'SMB PLAYTEST SAVE' not in text
    if args.scenario=='stage-stars':
        for key in ('airborne','active_attack','world_movement'):checks.pop(key,None)
        checks['starts_without_unlocks']='SMB PROGRESSION mode=recommended stars=0 flags=00000000 cannon_bob=0' in text and 'SMB PLAYTEST SAVE' not in text
        for flags,act in [('00',1),('01',2),('03',3),('43',3)]:
            checks[f'real_star_selector_{flags}']=f'SMB STAGE SELECTOR course=1 flags={flags} next={act}' in text
        checks['three_stars_saved_and_reentered']='SMB STAGE STAR REENTER round=2 act=3 flags=43 stars=3' in text and text.count('SMB STAGE STAR EXIT course=1')==3
        checks['victory_cleaned_up']=text.count('SMB VICTORY CLEANUP gameplay_preserved=1 demos_remaining=0')>=3
        controls=[{k:int(v) for k,v in re.findall(r'(tick|hits|grounded)=(\d+)',line)} for line in text.splitlines() if line.startswith('SMB STAGE STAR CONTROL')]
        checks['can_jump_after_selection']=any(r['grounded']==0 and 30<r['tick']<48 for r in controls)
        checks['can_attack_after_selection']=any(r['hits']>0 and r['tick']>=50 for r in controls)
    if args.scenario.startswith('entrance-'):
        for key in ('active_attack','world_movement'):checks.pop(key)
        checks['walked_into_painting']='SMB ENTRANCE ENTERED target=' in text
        checks['enemy_roster_survives']='SMB NPC SPAWN slot=1' in text
    if args.scenario=='wing-entrance':
        for key in ('airborne','active_attack','world_movement'):checks.pop(key)
        checks['sunlight_still_works']='SMB CAMERA SUNLIGHT native_warp=1' in text and 'SMB CAMERA WING ENTRY complete=1' in text
    if args.scenario=='camera-follow':
        for key in ('airborne','active_attack','world_movement'):checks.pop(key)
        samples=[(int(f),float(p),float(e)) for f,p,e in re.findall(r'SMB CAMERA FOLLOW frame=(\d+) pitch=([-\d.]+) focus_error=([-\d.]+)',text)]
        observed=[(f,p,e) for f,p,e in samples if 45<=f<=320]
        checks['stale_low_and_high_history_exercised']=text.count('SMB CAMERA FOLLOW FIXTURE')==3
        checks['camera_stays_above_player']=len(observed)>=30 and min(p for _,p,_ in observed)>=.05
        checks['no_extreme_overhead_pitch']=bool(observed) and max(p for _,p,_ in observed)<=1.05
        checks['player_vertical_tracking']=bool(observed) and max(abs(e) for _,_,e in observed)<=105
    if args.scenario=='camera-look':
        for key in ('airborne','active_attack','world_movement'):checks.pop(key)
        checks['look_returns_to_neutral']='SMB CAMERA LOOK neutral=1' in text
        checks['no_first_person']='first_person=1' not in text
        pitches=[float(v) for v in re.findall(r'SMB CAMERA LOOK peak=([-\d.]+)',text)]
        checks['looks_both_directions']=bool(pitches) and max(pitches)>.2 and min(pitches)<-.2
        views=[(int(f),float(p)) for f,p in re.findall(r'SMB CAMERA POSE frame=(\d+) offset=[-\d.]+ pitch=([-\d.]+)',text)]
        baseline=[p for f,p in views if f%180==15];released=[p for f,p in views if f%180==165]
        checks['view_returns_to_original_pitch']=bool(baseline and released) and abs(baseline[-1]-released[-1])<.08
    if args.scenario in ('doors' ,'stairs','progression','impact','bomb-impact','health','star','flight-low','flight-high'):
        for key in ('airborne','active_attack','world_movement'):checks.pop(key)
        checks['castle_entry']='SMB SCENARIO LEVEL name='+args.scenario+' level=6 ' in text
        if args.scenario in ('progression','impact','bomb-impact','health','star','flight-low','flight-high'):checks['painting_entry']='SMB SCENARIO LEVEL name='+args.scenario+' level=9 ' in text
    elif args.scenario=='model-forms':
        # Bowser's grounded up-special slides horizontally, so this model
        # regression should not require a character to become airborne.
        checks.pop('airborne',None)
    elif args.scenario=='projectiles':
        for key in ('airborne','active_attack','world_movement'):checks.pop(key)
        item_flags=[int(flag) for flag in re.findall(r'SMB ITEM .*?flags=(\d+)',text)]
        checks['native_arrow']=any(flag&4 for flag in item_flags)
        checks['native_bomb']=any(flag&2 for flag in item_flags)
    if 'SMB_ALL_STARS' in env:checks['all_stars_unlocked']=bool(re.search(r'SMB PLAYTEST SAVE .*stars=120 cannons=15 flags=1f10ffff',text))
    if args.roster_test:checks['neutral_special_state']=any(r['motion']>=341 for r in rows)
    if args.character=='iceclimbers':checks['both_climbers_rendered']=f'SMB NATIVE FIGHTERS count={2+args.cpus+enemy_names.count("iceclimbers")} primary=10 partner=11' in text
    checks['native_percentage_hud']='SMB NATIVE HUD damage_digits=1 emblem=1' in text
    checks['melee_music_muted']='SMB AUDIO music_gain=0 replacement_music=disabled' in text
    checks['melee_sfx_enabled']='SMB AUDIO sfx_gain=1.00 voices=enabled' in text
    checks['donor_window_never_shown']='SMB donor window hidden from creation' in text
    if select:checks['character_selector']='SMB SELECT OPEN native_portraits=32 forms=33 window=sm64' in text and 'SMB SELECT CONFIRM' in text
    panels=[{k:float(v) for k,v in re.findall(r'(\w+)=(-?[\d.]+)',line)} for line in text.splitlines() if line.startswith('SMB HUD SLOT')]
    checks['all_player_damage_panels']=len(panels)==args.cpus+1 and all(p['damage_digits']==1 and p['emblem']==1 for p in panels)
    checks['distinct_hud_positions']=len({round(p['x'],1) for p in panels})==args.cpus+1
    checks['all_player_portrait_icons']={int(n) for n in re.findall(r'SMB HUD ICON slot=(\d+)',text)}==set(range(args.cpus+1))
    npc_rows=[{k:float(v) for k,v in re.findall(r'(\w+)=(-?[\d.]+)',line)} for line in text.splitlines() if line.startswith('SMB NPC FRAME')]
    if args.cpus:
        checks['chosen_enemy_fighters']=all(any(r['slot']==slot for r in npc_rows) and all(int(r['kind']) in allowed_kinds(BY_NAME[name][2]) for r in npc_rows if r['slot']==slot) for slot,name in enumerate(enemy_names,1)) and len(enemy_names)==args.cpus
    if args.scenario=='character-switch':
        for key in ('fighter','airborne','active_attack','world_movement'):checks.pop(key)
        replacements=re.findall(r'SMB PLAYER REPLACED external=(\d+) costume=(\d+)',text)
        checks['exact_character_and_costume_swaps']=replacements==[('38','6'),('5','3'),('19','4'),('14','2'),('3','3'),('6','4'),('6','0')]
        checks['cancel_preserves_character']='SMB SWITCH CANCEL unchanged=1' in text and len(replacements)==7
        switch_progress=re.findall(r'SMB SWITCH CONFIRM .*level=(\d+) coins=(\d+) stars=(\d+) lives=(\d+)',text)
        checks['same_level_progress']=len(switch_progress)==7 and len(set(switch_progress))==1 and switch_progress[0][0:2]==('16','7') and switch_progress[0][3]=='4'
        checks['resume_movement_and_attacks']=any(r['hitboxes'] for r in rows) and max((r['x']-rows[0]['x'])**2+(r['z']-rows[0]['z'])**2 for r in rows)>200**2
        checks['menu_gl_state_restored']=text.count('SMB SELECT CLOSE gl_error=0')==8
        if course_enemies:checks['defeated_enemy_stays_defeated']=text.count('defeated=1 free_respawn=1')>=5 and text.count('SMB ENCOUNTER ENTER')==1
        checks['respawn_platform_each_swap']=text.count('SMB RESPAWN PLATFORM slot=0 sub=0')>=7
        checks['native_models_after_swap']={37,5,7,10,24,6}.issubset({int(r['kind']) for r in rows})
        checks['stock_icon_refresh']=all(f'SMB LIVES ICON character={i} ' in text for i in (38,5,19,14,3,6))
    if args.scenario=='npcs':
        for key in ('airborne','active_attack','world_movement'):checks.pop(key)
        enemies=[{k:float(v) for k,v in re.findall(r'(\w+)=(-?[\d.]+)',line)} for line in text.splitlines() if line.startswith('SMB NPC FRAME')]
        checks['native_level_9_enemies']=bool(enemies) and {int(r['slot']) for r in enemies}==set(range(1,args.cpus+1)) and all(r['level']==9 for r in enemies)
        checks['enemy_target_is_player']=all(r['target'] in (0,99) for r in enemies) and any(r['target']==0 for r in enemies)
        checks['enemies_damage_player']=any(r['player_percent']>0 for r in enemies)
        checks['player_damages_enemies']=any(r['percent']>0 for r in enemies)
        checks['enemies_across_courses']={16,9,11}.issubset({int(r['area']) for r in enemies if r['ready']==1})
        checks['enemies_rejoin_each_area']=text.count('SMB NPC SPAWN slot=1 sub=0')>=3
        checks['all_fighters_rendered']='SMB NATIVE FIGHTERS count=4 ' in text
        checks['sm64_combat_health_stays_full']=all(r['health']==0x880 for r in enemies)
    if args.scenario=='npc-follow':
        for key in ('airborne','active_attack','world_movement'):checks.pop(key)
        rejoined=[{k:float(v) for k,v in re.findall(r'(\w+)=(-?[\d.]+)',line)} for line in text.splitlines() if line.startswith('SMB NPC REJOIN')]
        expected={(16,1),(6,1),(9,1),(6,2),(6,3)}
        checks['all_enemies_follow_every_area']=all(expected.issubset({(int(r['level']),int(r['area'])) for r in rejoined if r['slot']==slot}) for slot in range(1,args.cpus+1)) and args.cpus>0
        door=re.search(r'SMB FOLLOW DOOR level=6 area=1 from=(\d+) to=(\d+)',text)
        checks['actual_room_door_crossed']=bool(door) and f'SMB FOLLOW ROOM ENTER' in text
        checks['all_enemies_in_destination_room']=bool(door) and all(any(r['slot']==slot and r['level']==6 and r['area']==1 and r['room']==int(door[2]) and r['spawn_room']==int(door[2]) and int(r['reason'])&6 for r in rejoined) for slot in range(1,args.cpus+1))
        checks['safe_spawn_in_entry_room']=bool(rejoined) and all(r['anchor_distance']<=281 and (r['room']==0 or r['spawn_room']==r['room']) for r in rejoined if r['catchup']==0)
        checks['three_second_headstart']=bool(rejoined) and all(r['wait_frames']>=90 for r in rejoined if int(r['reason'])&15)
        checks['waiting_enemies_suspended']=any(r['suspended']==1 and r['hits']==0 and r['ready']==0 for r in npc_rows)
        checks['door_handoff_deferred']='SMB NPC TRANSITION' in text and 'deferred=1' in text
        checks['painting_and_return']='SMB FOLLOW PAINTING' in text and 'SMB FOLLOW ROUTE COMPLETE' in text and text.count('SMB NPC REJOIN frame=')>=args.cpus*6
        checks['deferred_area_reset_survives_cutscene']=all(any(r['slot']==slot and r['level']==6 and r['area']==2 and int(r['reason'])&1 and int(r['reason'])&4 for r in rejoined) for slot in range(1,args.cpus+1))
        checks['same_level_different_area']='from_level=6 from_area=2 to_level=6 to_area=3' in text
    if args.scenario=='npc-catchup':
        for key in ('airborne','active_attack','world_movement'):checks.pop(key)
        catchups=[{k:float(v) for k,v in re.findall(r'(\w+)=(-?[\d.]+)',line)} for line in text.splitlines() if line.startswith('SMB NPC REJOIN') and 'catchup=1' in line]
        checks['all_enemies_catch_up']={int(r['slot']) for r in catchups}==set(range(1,args.cpus+1)) and args.cpus>0
        checks['teleport_stays_between_enemy_and_player']=bool(catchups) and all(r['old_distance']>4000 and max(600,r['old_distance']*.2)-1<=r['distance']<=r['old_distance']*.85+1 for r in catchups)
        checks['catchup_preserves_damage']=bool(catchups) and all(abs(r['percent_before']-r['percent_after'])<.01 for r in catchups)
    if args.camera=='sm64' and args.scenario!='basic':
        checks['sm64_camera_default']='SMB CAMERA provider=melee' not in text and 'SMB CAMERA provider=sm64 follow_scale=1.25' in text
        # The stock 1.25x follow distance can gain up to .45x combat framing.
        distances=[(float(base),float(preferred)) for base,preferred in re.findall(r'SMB CAMERA DISTANCE base=([\d.]+) preferred=([\d.]+)',text) if float(base) in (800,1000)]
        checks['increased_stock_follow_distance']=bool(distances) and all(base*1.25-.1<=preferred<=base*1.70+.1 for base,preferred in distances)
    if args.scenario=='lighting':
        for key in ('airborne','active_attack','world_movement'):checks.pop(key,None)
        checks['lighting_registered']=('SMB LIGHTS diffuse_mask=0' in text) if args.legacy_lighting else bool(re.search(r'SMB LIGHTS diffuse_mask=[1-9a-f]',text))
    if args.scenario in ('controls','controller'):
        checks.pop('world_movement',None)
        inputs=[{k:float(v) for k,v in re.findall(r'(seq|local_y|move|motion|y)=(-?[\d.]+)',line)} for line in text.splitlines() if line.startswith('SMB NATIVE INPUT')]
        checks['native_input_trace']=len(inputs)>=args.frames*2
        checks['up_special']=any(300<=r['seq']<440 and r['move']==20 for r in inputs)
        checks['down_special']=any(540<=r['seq']<680 and r['move']==21 for r in inputs)
        checks['up_modifier']=any(300<=r['seq']<360 and r['local_y']>.9 for r in inputs)
        checks['down_modifier']=any(540<=r['seq']<600 and r['local_y']<-.9 for r in inputs)
        events=[tuple(map(int,row)) for row in re.findall(r'M3D OWN SOUND tick=(\d+) kind=(\d+) state=(\d+) sound=(\d+) voice=(-?\d+)',text)]
        checks['jump_sound_requested']=any(240<=e[0]<290 and e[-1]>=0 for e in events)
        checks['native_sounds_resolved']=bool(events) and all(e[-1]>=0 for e in events)
        checks['no_missing_samples']='sfx MISS' not in text
        if args.scenario=='controller' and args.frames>=720:
            sep={int(phase):(int(combat,16),int(camera,16)) for phase,combat,camera in re.findall(r'SMB CONTROL SEPARATION phase=(\d+) physical=[0-9a-f]+ combat=([0-9a-f]+) host_camera=([0-9a-f]+)',text)}
            checks['grab_no_camera']=sep.get(3)==(0x10,0)
            checks['shield_no_camera']=sep.get(4)==(0x20,0)
            checks['dpad_camera_no_attack_modifier']=sep.get(5)==(0,8)

        yaw=[float(v) for v in re.findall(r'SMB CAMERA .* yaw=([-\d.]+)',text)]
        if args.camera=='melee':checks['melee_camera_orbits']=len(yaw)>3 and max(yaw)-min(yaw)>.3
    if args.scenario=='stairs':
        stair_rows=[{k:float(v) for k,v in re.findall(r'(\w+)=(-?[\d.]+)',line)} for line in text.splitlines() if line.startswith('SMB STAIR WALK')]
        up=[r for r in stair_rows if r['tick']>=8 and r['phase']==0]
        down=[r for r in stair_rows if r['phase']==1]
        checks['walks_up_stairs']=len(up)>8 and max(r['height'] for r in up)-min(r['height'] for r in up)>2
        checks['walks_down_stairs']=len(down)>8 and down[-1]['height']<max(r['height'] for r in down)-2
        checks['stairs_round_trip']=any(r['phase']==2 for r in stair_rows)
        checks['stairs_stay_grounded']=bool(up and down) and sum(r['grounded']==1 for r in up+down)/len(up+down)>.9
        checks['stairs_no_ledge_catch']=not any(252<=r['motion']<=263 for r in stair_rows)
        checks['stairs_no_death_warp']=not re.search(r'SMB WORLD WARP op=(?:18|19) ',text)
    if args.scenario=='barriers':
        for key in ('active_attack','world_movement','airborne'):checks.pop(key)
        clearances=[float(v) for v in re.findall(r'SMB BARRIER .*clearance=([-\d.]+)',text)]
        checks['barrier_containment']=len(clearances)>80 and 0<min(clearances)<6
    if args.scenario in ('flight-low','flight-high'):
        expected=108 if args.scenario=='flight-high' else 8
        flight_rows=[{k:float(v) for k,v in re.findall(r'(\w+)=(-?[\d.]+)',line)} for line in text.splitlines() if line.startswith('SMB SCENARIO FLIGHT')]
        checks['native_percent_damage']=any(r['percent']==expected for r in flight_rows)
        checks['no_regular_health_damage']=bool(flight_rows) and all(r['health']==0x880 and r['hurt']==0 for r in flight_rows)
        checks['native_damage_flight']=any(87<=r['motion']<=91 for r in flight_rows)
        checks['native_character_weight']=bool(re.search(r'SMB WORLD LAUNCH .*weight=104.0',text))
    if args.scenario=='ledges':
        for key in ('active_attack','world_movement'):checks.pop(key)
        checks['native_ledge_catch']=any(252<=r['motion']<=253 for r in rows)
        checks['native_ledge_jump']=any(260<=r['motion']<=263 for r in rows)
        checks['continuous_traversal']=all(sum((a[k]-b[k])**2 for k in ('x','y','z'))<250**2 for a,b in zip(rows,rows[1:]))
    if args.scenario=='platforms':
        for key in ('active_attack','world_movement'):checks.pop(key)
        platform_rows=[{k:float(v) for k,v in re.findall(r'(\w+)=(-?[\d.]+)',line)} for line in text.splitlines() if line.startswith('SMB PLATFORM')]
        ground=[r for r in platform_rows if 10<=r['frame']<40]
        hang=[r for r in platform_rows if 70<=r['frame']<100]
        checks['platform_carries_grounded']=len(ground)>20 and max(r['x'] for r in ground)-min(r['x'] for r in ground)>40 and max(r['radius'] for r in ground)<10
        checks['platform_carries_ledge']=len(hang)>20 and all(r['motion'] in (252,253) for r in hang) and max(r['radius'] for r in hang)-min(r['radius'] for r in hang)<30
    if args.scenario in ('walls','walls-all'):
        for key in ('fighter','airborne','active_attack','world_movement'):checks.pop(key)
        kicked={int(k) for k in re.findall(r'SMB WALL KICK kind=(\d+)',text)}
        checks['wall_jump']=bool(kicked)
        checks['initial_jump_animation']=all(int(a) in (25,26) for a in re.findall(r'SMB WALL KICK .*animation=(\d+)',text))
        trajectory=[(int(t),float(x),float(y)) for t,x,y in re.findall(r'SMB WALL TRAJECTORY .*tick=(\d+) vx=([\d.-]+) vy=([\d.-]+)',text)]
        checks['sm64_wall_kick_trajectory']=bool(trajectory) and all(abs(y-(3.1-.2*(t//2)))<.001 and abs(x-(1.2-.0175*(1+t//2)))<.001 for t,x,y in trajectory)
        if args.scenario=='walls-all':checks['all_33_forms_wall_jump']=kicked=={r[2] for r in BY_NAME.values()}
    if args.scenario=='ground-pound':
        for key in ('airborne','active_attack','world_movement'):checks.pop(key)
        checks['real_post_ground_pounded']=bool(re.search(r'SMB POUND POST .*offset=-[1-9]',text))
        checks['native_down_attack']='SMB GROUND POUND' in text and bool(re.search(r'SMB POUND POST .*down=1',text))
    if args.scenario=='no-fast-fall':
        for key in ('airborne','active_attack','world_movement'):checks.pop(key)
        fall_samples={(int(p),int(t)):(float(y),int(f)) for p,t,y,f in re.findall(r'SMB NO FASTFALL phase=(\d+) tick=(\d+) y=([\d.-]+) vy=[\d.-]+ fast=(\d+)',text)}
        checks['down_modifier_does_not_fast_fall']=len(fall_samples)>=300 and all(abs(fall_samples[(p,t)][0]-fall_samples[(0,t)][0])<.01 and fall_samples[(p,t)][1]==0 for p in (1,2) for t in range(30,80))
    if args.scenario=='powerups':
        for key in ('airborne','active_attack','world_movement'):checks.pop(key)
        checks['native_metal_applied']='metal=1 cloak=0 native_metal=1 native_cloak=0' in text
        checks['native_cloak_applied']='metal=0 cloak=1 native_metal=0 native_cloak=1' in text
        checks['combined_caps']='metal=1 cloak=1 native_metal=1 native_cloak=1' in text
        checks['caps_expire']='metal=0 cloak=0 native_metal=0 native_cloak=0' in text

    if args.scenario=='slope':
        for key in ('airborne','active_attack','world_movement'):checks.pop(key)
        slope_rows=[{k:float(v) for k,v in re.findall(r'(\w+)=(-?[\d.]+)',line)} for line in text.splitlines() if line.startswith('SMB SLOPE ')]
        checks['entered_vcutm']=bool(slope_rows)
        released=[r for r in slope_rows if r['tick']>=80]
        if args.slope_steer:
            steering=[r for r in slope_rows if 82<=r['tick']<150]
            checks['turns_while_sliding']=bool(steering) and max(r['x'] for r in steering)-min(r['x'] for r in steering)>50
        checks['slides_without_stick']=len(released)>20 and max(r['z'] for r in released)-min(r['z'] for r in released)>500
        checks['ramp_contact']=any(.5<r.get('normal_y',0)<.64 and r.get('grounded')==1 for r in slope_rows)
        feet={int(r['seq']):r['foot']*10 for r in ({k:float(v) for k,v in re.findall(r'(\w+)=(-?[\d.]+)',line)} for line in text.splitlines() if line.startswith('SMB NATIVE FOOT '))}
        contact=[r for r in slope_rows if r['tick']>0 and r['floor']>-10000]
        checks['native_foot_samples']=bool(contact) and all(int(r['seq']) in feet for r in contact)
        checks['ramp_not_penetrated']=bool(contact) and all(feet.get(int(r['seq']),r['y'])>=r['floor']-2 for r in contact)
        entry=[r for r in slope_rows if 1<=r['tick']<10]
        checks['level_entry_preserves_spawn']=bool(entry) and all(abs(r['x']+6143)<5 and abs(r['z']+6143)<5 and abs(r['y']-5734)<5 for r in entry)
        checks['no_sm64_death_on_ramp']=bool(slope_rows) and all(r['lives']==slope_rows[0]['lives'] for r in slope_rows)
        if args.slope_jump:checks['jump_out_of_slide']=any(r['motion'] in (25,26,27,28) and r['tick']>=100 for r in slope_rows)
        checks['no_native_stock_death']=not any(r['motion']<14 for r in slope_rows if r['tick']>30)
        poses=re.findall(r'SMB SLIDE POSE .*motion=(\d+) shield=(\d+)',text)
        checks['crouches_without_shield']=len(poses)>10 and all(motion=='40' and shield=='0' for motion,shield in poses)
    if args.scenario=='effects':
        for key in ('airborne','active_attack','world_movement'):checks.pop(key)
        checks['particle_colour_and_depth']=any(int(v)>3 for v in re.findall(r'SMB EFFECT DEPTH .*visible=(\d+)',text))
    if args.effect_depth_off and args.scenario=='effects':
        checks.pop('particle_colour_and_depth',None)
        checks['negative_control_missing_depth']=any(int(v)>3 for v in re.findall(r'SMB EFFECT DEPTH .*background_depth=(\d+)',text))
    if args.scenario=='respawn':
        for key in ('airborne','active_attack','world_movement'):checks.pop(key)
        checks['platform_drawn_and_removed']='SMB RESPAWN RENDER platform=1' in text and 'SMB RESPAWN RENDER platform=0' in text
        checks['native_respawn_platform']='SMB RESPAWN PLATFORM slot=0 sub=0 ' in text and 'accessory=1' in text
        checks['rebirth_states']={12,13}.issubset({int(r['motion']) for r in rows})
        checks['left_respawn_platform']=any(r['motion']>=14 for r in rows if r['frame']>120)
        checks['one_shared_death_and_rebirth']=text.count('SMB WORLD DEATH ')==1 and text.count('SMB WORLD REBIRTH ')==1
    if args.scenario in ('heave-ho','king-bobomb'):
        for key in ('airborne','active_attack','world_movement'):checks.pop(key)
        trace=[dict((k,float(v)) for k,v in re.findall(r'(tick|native_frames|active|y|vy|lives|health)=(-?[\d.]+)',line))|{'action':re.search(r'action=([0-9a-f]+)',line)[1]} for line in text.splitlines() if line.startswith('SMB ENEMY THROW')]
        checks['real_level_enemy']=f'SMB INTERACTION FIXTURE name={args.scenario} level={11 if args.scenario=="heave-ho" else 9}' in text
        checks['host_holds_then_releases']='SMB WORLD HANDOFF action=00020370' in text and 'SMB WORLD RELEASE source=enemy' in text
        checks['native_tumble_launch']='SMB NATIVE LAUNCH source=enemy motion=38' in text and any(r['motion']==38 and r['grounded']==0 for r in rows)
        checks['native_flight_moves']=bool(trace) and max(r['y'] for r in trace)-min(r['y'] for r in trace)>250
        checks['native_recovery_available']=args.scenario=='cannon-range' or bool(re.search(r'SMB NATIVE INPUT .*move=20 ',text))
        checks['no_second_health_or_life_loss']=bool(trace) and all(r['health']==0x880 and r['lives']==trace[0]['lives'] for r in trace)
    if args.scenario in ('thematic-rounds','air-meter','coop-capacity','npc-tall','swim-drown','swim','swim-cpus','swim-controls','npc-environment','npc-lava','shell-ride','wing-flight','directions'):
        for key in ('airborne','active_attack','world_movement'):checks.pop(key,None)
        if args.scenario=='thematic-rounds':
            checks['all_player_damage_panels']={int(p['slot']) for p in panels}=={0,1} and all(p['damage_digits']==1 and p['emblem']==1 for p in panels)
            checks['distinct_hud_positions']=len({round(p['x'],1) for p in panels})==2
            checks['all_player_portrait_icons']={int(n) for n in re.findall(r'SMB HUD ICON slot=(\d+)',text)}=={0,1}
            checks['four_rounds_from_real_shortcuts']='SMB ROUNDS PASS requests=4 wrap=1 held_once=1 idle=1 scene_reset=1' in text and text.count('SMB ROUNDS CHECK request=')==4
            checks['keyboard_and_controller_rounds']=text.count('source=F8')==2 and text.count('source=L3')==2
            checks['no_automatic_rounds']='SMB ROUNDS IDLE pass=1 serial=4' in text
            checks['paused_shortcuts_not_buffered']='SMB ROUNDS PAUSED INPUT pass=1 buffered=0' in text
            checks['rounds_reset_on_scene_change']='SMB ROUNDS SCENE RESET pass=1 serial=0 count=0' in text
        if args.scenario=='air-meter':
            checks['native_air_coin_and_damage_rules']='SMB AIR PASS cases=10' in text and text.count('SMB AIR CHECK case=')==10 and 'pass=0' not in text
        if args.scenario=='coop-capacity':
            checks['all_remote_slots']='SMB COOP CAPACITY players=16' in text and set(map(int,re.findall(r'SMB COOP FIGHTER seat=(\d+)',text)))==set(range(4,19))
            checks['sixteen_native_models']='SMB NATIVE FIGHTERS count=16' in text
            checks['sixteen_native_hud_icons']=len(set(re.findall(r'SMB HUD ICON slot=(\d+)',text)))==16
            checks['clean_peer_removal']='SMB COOP CAPACITY removed=15' in text and 'SMB COOP CAPACITY FAIL' not in text
            checks['all_player_damage_panels']=len(set(re.findall(r'SMB HUD SLOT slot=(\d+)',text)))==16
            checks['distinct_hud_positions']=len(set(re.findall(r'SMB HUD SLOT .*x=([-\d.]+)',text)))>=8
            checks['all_player_portrait_icons']=checks['sixteen_native_hud_icons']
        if args.scenario in ('npc-tall','swim-cpus','npc-environment','npc-lava'):
            for key in ('all_player_damage_panels','distinct_hud_positions','all_player_portrait_icons'):checks.pop(key,None)
        if args.scenario=='npc-tall':
            checks['all_player_damage_panels']=set(int(n) for n in re.findall(r'SMB HUD SLOT slot=(\d+)',text))=={0,1}
            checks['distinct_hud_positions']=len(set(re.findall(r'SMB HUD SLOT .*x=([-\d.]+)',text)))==2
            checks['all_player_portrait_icons']=set(int(n) for n in re.findall(r'SMB HUD ICON slot=(\d+)',text))=={0,1}
            for key in ('no_melee_enemies','one_fighter_hud'):checks.pop(key,None)
            checks['spawn_arbitrary']='SMB MANUAL ENEMY' in text and 'SMB HIGH NPC SPAWN code=0' in text
            checks['fight_at_high_altitude']=bool(re.search(r'SMB HIGH NPC .*y=[4-9][0-9]{3}\.[0-9] motion=\d+ hits=[1-9]',text))
        if args.scenario=='npc-lava':
            checks['real_lava_floor']='SMB NPC ENV TEST LAVA FLOOR' in text
            checks['live_lava_bounce']='SMB NPC ENV TEST LAVA BOUNCE pass=1' in text
            checks['survives_lava']='SMB NPC ENV TEST LAVA SURVIVES pass=1' in text
        if args.scenario=='npc-environment':
            checks['native_hazards_and_shields']=any(line.startswith('SMB NPC ENV TEST NATIVE PASS ') and all(word in line for word in ('contact=1','separated=1','shield=1','metal=1','fatal_metal=1','lava=1','nana=1')) for line in text.splitlines())
            checks['real_goomba_contact']='SMB NPC ENV TEST GOOMBA pass=1 cooldown_pass=1' in text
        if args.scenario in ('swim','swim-drown','swim-cpus'):
            checks['real_water']='SMB WATER FIXTURE' in text
            checks['buoyant_native_swim']='SMB WATER AIR' in text and (args.scenario in ('swim-drown','swim-cpus') or 'flags=512' in text)
            checks['underwater_specials']=args.scenario in ('swim-drown','swim-cpus') or bool(re.search(r'SMB NATIVE INPUT .*move=20 ',text))
        if args.scenario=='swim-controls':
            results=dict(re.findall(r'SMB SWIM CONTROL case=(\w+) pass=(\d)',text))
            for swim_case in ('jump_entry','forward','ascend','descend','cancel','surface_exit','special_recovery'):
                checks['swim_'+swim_case]=results.get(swim_case)=='1'
            checks['all_swim_controls']='SMB SWIM CONTROL PASS cases=7' in text
        if args.scenario=='swim-cpus':
            checks['cpu_and_partner_damage_lower_air']='SMB WATER CPU AIR DAMAGE pass=1 popo=744 nana=744' in text
            checks['cpu_and_nana_water']=all(f'slot=1 sub={sub} ' in text for sub in (0,1)) and 'SMB WATER CPU FIXTURE primary=1 nana=1' in text
            checks['nana_drowns_independently']='SMB WATER PARTNER DROWN popo_alive=1 nana_retired=1' in text
            checks['cpu_drowning_ko']='SMB WATER CPU LEDGER defeated=1' in text
            checks['both_drown']=len(re.findall(r'SMB NPC .*reason=drowning',text))>=2
        if args.scenario=='swim-drown':checks['native_drowning_and_death']='SMB WATER DROWN native_action=1' in text and 'SMB WORLD DEATH' in text
        if args.scenario=='shell-ride':checks['native_shell_riding']='riding=1 object=1' in text and 'SMB HOSTED POSE mode=shell' in text
        if args.scenario=='wing-flight':checks['native_wing_pose']='SMB HOSTED POSE mode=wing' in text and 'flying=1' in text
        if args.scenario=='directions':
            samples=re.findall(r'SMB DIRECTION phase=(\d+) frame=\d+ x=[-\d.]+ z=[-\d.]+ vx=([-\d.]+) vz=([-\d.]+) fx=([-\d.]+) fz=([-\d.]+) motion=(\d+)',text)
            for phase in range(4):
                attack=[r for r in samples if int(r[0])==phase and int(r[5])==351]
                checks['kirby_dash_direction_'+str(phase)]=bool(attack) and all(float(r[1])*float(r[3])+float(r[2])*float(r[4])>=-.05 for r in attack)
    if args.scenario in ('cannon','cannon-range'):
        for key in ('airborne','active_attack','world_movement'):checks.pop(key)
        checks['host_cannon_aim']='SMB WORLD HANDOFF action=00001371' in text
        checks['cannon_release_to_melee']='SMB WORLD RELEASE source=cannon' in text and 'SMB NATIVE LAUNCH source=cannon motion=38' in text
        airborne=[r for r in rows if r['motion']==38]
        checks['native_cannon_flight']=bool(airborne) and max(r['y'] for r in airborne)-min(r['y'] for r in airborne)>100
        checks['native_recovery_available']=args.scenario=='cannon-range' or bool(re.search(r'SMB NATIVE INPUT .*move=20 ',text))
    if args.scenario=='transform':
        for key in ('airborne','active_attack','world_movement'):checks.pop(key)
        checks['both_forms_simulated']={7,19}.issubset({int(r['kind']) for r in rows})
        checks['sleeping_form_not_rendered']='SMB NATIVE FIGHTERS count=2' not in text
    if args.scenario in ('grab','bowser','shield'):
        for key in ('airborne','active_attack','world_movement'):checks.pop(key)
        if args.scenario=='shield':
            checks['shield_blocks_damage']=bool(re.search(r'SMB INTERACTION TRACE name=shield tick=70 .*shield=1 percent=0.00',text))
            checks['release_allows_damage']=bool(re.search(r'SMB INTERACTION TRACE name=shield tick=1[345]0 .*percent=(?!0.00)',text))
        else:
            checks['native_catch_enters_sm64_grab']='SMB WORLD GRAB' in text and bool(re.search(r'SMB CAPSULE .*flags=16',text))
            checks['sm64_holds_object']=bool(re.search(r'SMB INTERACTION TRACE .*held=1',text))
            checks['object_thrown']='SMB WORLD THROW state=2 ' in text
            if args.scenario=='bowser':checks['bowser_spins']=bool(re.search(r'SMB INTERACTION TRACE .*spin=(?:4096|-4096)',text))
    if args.scenario=='victory-forms':
        for key in ('airborne','active_attack','world_movement'):checks.pop(key)
        checks['three_celebrations']=len(re.findall(r'SMB VICTORY PROBE ',text))==3
        checks['gameplay_entity_preserved']=len(re.findall(r'SMB VICTORY GAMEPLAY PRESERVED .*entity=1',text))>=1 and 'entity=0' not in text
        checks['native_poses_animate']=len(re.findall(r'SMB VICTORY ANIMATE .*frame=30.000 tree=1',text))==3
        if args.character in ('zelda','sheik'):checks['both_forms_celebrate']={7,19}.issubset({int(v) for v in re.findall(r'SMB VICTORY PROBE .*kind=(\d+)',text)})
    if args.scenario=='star':
        checks['native_victory_animation']='SMB VICTORY BEGIN' in text and bool(re.search(r'SMB VICTORY ANIMATE .*frame=(?!0.000)\d.*tree=1',text)) and 'SMB VICTORY END' in text
        checks['star_collected_and_saved']=bool(re.search(r'SMB SCENARIO SAVE stars=[1-9] flags=20',text))
    if args.scenario=='reload-save':
        for key in ('airborne','active_attack','world_movement'):checks.pop(key)
        checks['no_startup_death']=not re.search(r'SMB WORLD WARP op=(?:18|19) ',text)
        checks['saved_progress_reloaded']=bool(re.search(r'SMB SCENARIO SAVE stars=[1-9]\d* flags=[1-9a-f][0-9a-f]*',text))
    if args.scenario=='health':
        checks['world_damage_once']='SMB SCENARIO DAMAGE percent=8.00' in text
        checks['coin_heals_percent']='SMB COIN HEAL value=1 removed=10.0' in text and 'SMB WORLD HEALTH change=-10.00 percent=0.00' in text
    if args.scenario=='basic':checks['no_startup_death']=not re.search(r'SMB WORLD WARP op=(?:18|19) ',text)
    if args.scenario in ('projectiles','impact','bomb-impact'):
        draw_rows=[{k:float(v) for k,v in re.findall(r'(\w+)=(-?[\d.]+)',line)} for line in text.splitlines() if line.startswith('SMB NATIVE RENDER')]
        baseline=min((r['draws'] for r in draw_rows if r.get('items',0)==0),default=0)
        checks['native_item_render']=any(r.get('items',0)>0 and r['draws']>baseline for r in draw_rows)
    if args.scenario in ('impact','bomb-impact'):
        checks['projectile_enemy_impact']=bool(re.search(r'SMB WORLD HIT .*flags=[13579] .*type=00008000',text))
        checks['enemy_defeated']='SMB SCENARIO TARGET RESULT active=0 ' in text
    checks['selected_lives_icon']='SMB LIVES ICON' in text and any(int(x)>20 for x in re.findall(r'SMB LIVES PIXELS count=(\d+)',text))
    checks['drawable_resolution']=all(r['width']==r['framebuffer'] for r in native_render if 'framebuffer' in r)
    if args.render_diagnostic:
        checks['blend_gpu']=len(re.findall(r'SMB COMPOSITE GPU .*pass=1',text))==5
        checks['blend_world']=len(re.findall(r'SMB COMPOSITE WORLD .*pass=1',text))==5
    if course_enemies and args.difficulty!='none':
        for key in ('all_player_damage_panels','distinct_hud_positions','all_player_portrait_icons','chosen_enemy_fighters','both_climbers_rendered','enemies_across_courses','enemies_rejoin_each_area'):
            checks.pop(key,None)
        entries=[{k:int(v) for k,v in re.findall(r'(course|count)=(\d+)',line)} for line in text.splitlines() if line.startswith('SMB ENCOUNTER ENTER')]
        checks['course_owned_enemies']=bool(entries) and all(1<=e['count']<=3 for e in entries)
        checks['first_course_authored']=all('external=1,' in line for line in text.splitlines() if line.startswith('SMB ENCOUNTER ENTER course=1 '))
        checks['final_hud_overlay']='SMB HUD OVERLAY' in text
        if args.scenario=='course-battle':
            for key in ('airborne','active_attack','world_movement'):checks.pop(key,None)
            checks['difficulty_varies_count']={(e['course'],e['count']) for e in entries}>={(0,1),(1,1),(6,1),(11,3)}
            checks['native_knockback_ko']='SMB ENCOUNTER TEST HIT' in text and 'SMB ENCOUNTER KO course=1 slot=1' in text
            checks['defeated_stays_out']='SMB ENCOUNTER TEST STAYS_DEAD course=1 defeated=1' in text
            checks['new_visit_restores_encounter']='SMB ENCOUNTER TEST REENTER course=1 defeated=0 count=1' in text
            checks['no_retreat_teleports']=not any('catchup=1' in line for line in text.splitlines() if line.startswith('SMB NPC REJOIN'))
    if args.difficulty=='none' and args.scenario not in ('thematic-rounds','npc-tall','coop-capacity','swim-cpus','npc-environment','npc-lava'):
        checks['no_melee_enemies']='SMB ENEMIES DISABLED native_cpus=0 sm64_enemies=unchanged' in text and 'SMB NPC SPAWN' not in text and 'SMB ENCOUNTER ENTER' not in text and 'SMB ENCOUNTER WAVE' not in text
        checks['one_fighter_hud']={int(p['slot']) for p in panels}=={0}
        checks['zero_enemy_seats']=args.cpus==0 and not enemy_names
    if args.scenario=='ground-hud':
        for key in ('airborne','active_attack','world_movement'):checks.pop(key,None)
        checks['live_circle_shadow']=bool(re.search(r'SMB GROUND MARKER .*alpha=[1-9]\d* .*line=0 live_sm64=1',text))
        checks['hud_over_foreground']='SMB HUD OCCLUSION' in text and bool(re.search(r'SMB HUD OCCLUSION .*pass=1',text))
    if args.scenario=='star-return':
        for key in ('airborne','active_attack','world_movement'):checks.pop(key,None)
        controls=[{k:float(v) for k,v in re.findall(r'(round|tick|delta|grounded|hits|active)=(\d+(?:\.\d+)?)',line)} for line in text.splitlines() if line.startswith('SMB STAR RETURN CONTROL')]
        checks['three_real_star_exits']=text.count('SMB VICTORY END')>=3 and text.count('SMB VICTORY CLEANUP gameplay_preserved=1 demos_remaining=0')>=3
        checks['movement_after_each_star']=all(any(r['round']==n and r['delta']>50 for r in controls) for n in range(3))
        checks['jump_after_each_star']=all(any(r['round']==n and r['grounded']==0 and 30<r['tick']<48 for r in controls) for n in range(3))
        checks['attack_after_each_star']=all(any(r['round']==n and r['hits']>0 and r['tick']>48 for r in controls) for n in range(3))
    if args.scenario=='encounter-launch':
        for key in ('airborne','active_attack','world_movement'):checks.pop(key,None)
        checks['moderate_launch_survives']='SMB LAUNCH REGRESSION SURVIVED defeated=0' in text
        checks['airborne_recall_deferred']='SMB NPC AIR RECALL DEFERRED' in text
        npc_rows=[{k:float(v) for k,v in re.findall(r'(\w+)=(-?[\d.]+)',line)} for line in text.splitlines() if line.startswith('SMB NPC FRAME')]
        recovered=any(r.get('percent',0)>=500 and r.get('motion')==14 for r in npc_rows)
        checks['strong_hit_resolves_by_course']=recovered or 'SMB LAUNCH REGRESSION HORIZONTAL_KO' in text
        checks['local_blast_zones_disabled']='SMB LOCAL BLAST' not in text
        checks['camera_frames_airborne_enemy']=bool(re.search(r'SMB COMBAT CAMERA .*zoom=0\.[1-9]\d* lift=[1-9]',text))
    if args.scenario=='campaign-waves':
        for key in ('airborne','active_attack','world_movement'):checks.pop(key,None)
        checks['three_sequential_waves']='SMB CAMPAIGN WAVES COMPLETE waves=3' in text
        checks['native_knockout_effect']=len(re.findall(r'SMB KO EFFECT .*created=1',text))>=3
        checks['defeat_announcement']=len(re.findall(r'SMB KO ANNOUNCER defeated=1 voice=\d+',text))>=3
    if args.new_game:
        checks['fresh_progression_zero']='SMB PROGRESSION mode=new stars=0 flags=00000000 cannon_bob=0' in text
        checks['no_unlock_grant']='SMB PLAYTEST SAVE' not in text
        checks['original_opening']='SMB PLAYTHROUGH OPENING' in text
    if args.record is not None:
        checks['mp4_finalized']=bool(re.search(r'SMB RECORD FINISH .*status=2 frames=[1-9]\d* .*error=none',text))
        source=re.search(r'SMB RECORD SOURCES sm64_frames=(\d+) melee_frames=(\d+) sm64_rms=([\d.]+) melee_rms=([\d.]+)',text)
        checks['both_audio_engines']=bool(source) and int(source[1])>0 and int(source[2])>0 and float(source[3])>0 and float(source[4])>0
    if args.grab_diagnostic:
        # This fixture replaces the basic player's jump with a grounded catch.
        # Its own assertion verifies the captured fighter's real launch flight.
        checks.pop('airborne',None)
        checks['native_network_grab']='SMB NET GRAB TEST PASS' in text and 'SMB NET GRAB TEST FAIL' not in text
        checks['all_player_damage_panels']={0,4}.issubset({int(p['slot']) for p in panels}) and all(p['damage_digits']==1 and p['emblem']==1 for p in panels)
        checks['distinct_hud_positions']=len({round(p['x'],1) for p in panels})>=2
        checks['all_player_portrait_icons']={0,4}.issubset({int(n) for n in re.findall(r'SMB HUD ICON slot=(\d+)',text)})
        checks.pop('one_fighter_hud',None)
    if args.remote_diagnostic:
        checks['remote_articles_and_partner']='SMB REMOTE TEST PASS arrow=1 bomb=1 independent_nana=1 disconnect=1 world_refresh=1' in text
        checks['remote_native_article_models']=len(set(re.findall(r'SMB COOP ARTICLE .*kind=(\d+) native_model=1 callbacks=0',text)))>=2
        checks['remote_presentation_states']='SMB REMOTE STATES PASS common=341 ' in text
        checks['all_player_damage_panels']={0,4,5}.issubset({int(p['slot']) for p in panels}) and all(p['damage_digits']==1 and p['emblem']==1 for p in panels)
        checks['distinct_hud_positions']=len({round(p['x'],1) for p in panels})>=3
        checks['all_player_portrait_icons']={0,4,5}.issubset({int(n) for n in re.findall(r'SMB HUD ICON slot=(\d+)',text)})
        checks.pop('one_fighter_hud',None)
    if args.water_diagnostic:
        checks['native_water_physics']='SMB WATER PHYSICS PASS' in text and 'SMB WATER PHYSICS FAIL' not in text
        checks['actual_damage_event_accounting']=len(re.findall(r'SMB DAMAGE EVENTS PROBE .*passed=1',text))==2 and not re.search(r'SMB DAMAGE EVENTS PROBE .*passed=0',text)
        checks.pop('no_melee_enemies',None) # The isolated fixture creates and immediately retires an unused CPU seat.
    attributes=[{k:float(v) for k,v in re.findall(r'(\w+)=(-?[\d.]+)',line)} for line in text.splitlines() if line.startswith('SMB ATTR ')]
    result=dict(native_attributes=attributes,observed_motions=sorted({int(r['motion']) for r in rows}),scope=__doc__,character=args.character,enemy_characters=enemy_names,cpu_count=args.cpus,difficulty=args.difficulty,speed=args.speed,save_profile=str(profile),all_stars='SMB_ALL_STARS' in env,smoke=args.smoke,passed=all(checks.values()) and not failures if args.smoke else None,
        checks=checks,game_pid=game.pid,child_process_samples=samples,exit_code=code,seconds=elapsed,failures=failures,
        native_original_sha256=pin['executable_sha256'],library_sha256=hashlib.sha256(library.read_bytes()).hexdigest(),
        host_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
        native_gpu_character_rendering=checks.get('native_render',False),adventure_completion_verified=False,coop_deluxe_native_bridge=False)
    (case/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2),flush=True)
    return 0 if (not args.smoke and code==0) or result['passed'] else 1

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--title-preview',action='store_true',help='Render only the selected fighter intro, then exit without entering a level')
    p.add_argument('--save-profile',type=Path)
    p.add_argument('--legacy-lighting',action='store_true',help=argparse.SUPPRESS)
    p.add_argument('--effect-depth-off',action='store_true',help=argparse.SUPPRESS)
    p.add_argument('--slope-steer',action='store_true',help='Exercise steering on the real moat slide')
    p.add_argument('--slope-jump',action='store_true',help='Verify jumping out of the VCUTM ramp')
    p.add_argument('--camera',choices=['melee','sm64'],default='sm64',help='SM64 camera with 25%% more follow distance (default), or optional Melee3D orbit camera')
    p.add_argument('--input-diagnostic',action='store_true')
    p.add_argument('--grab-diagnostic',action='store_true',help='Verify native tether, pummel and throw ownership in a Link smoke run')
    p.add_argument('--remote-diagnostic',action='store_true',help='Verify remote articles, Nana, shields, cleanup and common/Bowser/Sonic pose purity')
    p.add_argument('--water-diagnostic',action='store_true',help='Verify faster native water descent, kicks, specials, CPU/Nana, metal and proxy ownership')
    p.add_argument('--sound-diagnostic',action='store_true',help='Capture native-only PCM and output energy for effect/voice checks')
    p.add_argument('--roster-test',action='store_true',help='Smoke-test a neutral special after movement, jumping and attacking')
    p.add_argument('--story',action='store_true',help='Continue the latest normal-progression playthrough')
    p.add_argument('--new-game',action='store_true',help='Start a separate fresh playthrough with no unlocks')
    p.add_argument('--record',nargs='?',const='',metavar='MP4',help='Record game picture and audio; optionally choose an MP4 path (F9 toggles)')
    p.add_argument('--all-stars',action='store_true',help='Also unlock stars in smoke-test profiles')
    p.add_argument('--speed',type=float,default=1.25,help='Native horizontal movement multiplier (default 1.25)')
    p.add_argument('--stair-ordinal',type=int,default=0)
    p.add_argument('--benchmark',action='store_true',help='Measure uninterrupted gameplay without the smoke pause probe')
    p.add_argument('--render-height',type=int,help='Whole-scene render height cap; 0 uses every drawable pixel')
    p.add_argument('--stage',type=int,choices=range(27),help='Start one recommended stage/character pairing (0–26), with separate earned star progress')
    p.add_argument('--character',choices=sorted(BY_NAME),help='Direct character launch; omit for the in-game character selector')
    p.add_argument('--select',action='store_true',help='Show character selection even with --character')
    p.add_argument('--cpus',type=int,choices=range(4),help='Level-9 enemy fighters: 0..3 (normal default 3; smoke default 0)')
    p.add_argument('--difficulty',choices=['default','impossible','none'],help='Authored encounters, maximum-level opponents, or no Melee enemies (none)')
    p.add_argument('--course-enemies',action='store_true',help='Course-specific one-stock encounters (default in normal play)')
    p.add_argument('--enemies',help='One to three comma-separated fighter names, e.g. sonic,link,bowser; also sets CPU count')
    p.add_argument('--wall-jumps',action='store_true',help='Enable wall jumps for every fighter')
    p.add_argument('--copy-kind',type=int,default=9,help=argparse.SUPPRESS)
    p.add_argument('--copy-costume',type=int,choices=range(6),default=0,help=argparse.SUPPRESS)
    p.add_argument('--scenario',choices=['kirby-copy','thematic-rounds','air-meter','camera-follow','swim-controls','npc-lava','npc-environment','coop-capacity','npc-tall','swim-drown','swim','swim-cpus','shell-ride','wing-flight','directions','stage-stars','wing-entrance','stage','entrance-snow','entrance-lava','camera-look','no-fast-fall','ground-pound','powerups','walls-all','character-switch','basic','star-return','campaign-waves','encounter-launch','course-battle','ground-hud','model-forms','npcs','npc-follow','npc-catchup','grab','bowser','shield','heave-ho','king-bobomb','cannon-range','cannon','victory-forms','controls','controller','doors','stairs','progression','impact','projectiles','ledges','bomb-impact','walls','health','platforms','star','reload-save','slope','effects','lighting','respawn','transform','flight-low','flight-high','barriers'],default='basic')
    p.add_argument('--render-diagnostic',action='store_true');p.add_argument('--smoke',action='store_true');p.add_argument('--frames',type=int,default=180)
    p.add_argument('--model-diagnostic',action='store_true',help='Check native model-part visibility against authored DAT selections')
    p.add_argument('--legacy-model-visibility',action='store_true',help=argparse.SUPPRESS)
    p.add_argument('--timeout',type=float)
    args=p.parse_args()
    if args.remote_diagnostic and (not args.smoke or args.character!='link' or args.scenario!='projectiles' or args.frames<550):p.error('--remote-diagnostic requires --smoke --character link --scenario projectiles --frames 550 or more')
    if args.water_diagnostic and (not args.smoke or args.difficulty!='none' or args.cpus):p.error('--water-diagnostic requires --smoke --difficulty none --cpus 0')
    if not .5<=args.speed<=2:p.error('speed must be finite and between 0.5 and 2')
    if args.story and args.new_game:p.error('--story and --new-game are mutually exclusive')
    if args.new_game and args.save_profile and (args.save_profile/'sm64_save_file.bin').exists():p.error('--new-game requires a fresh save profile')
    if (args.story or args.new_game) and args.all_stars:p.error('Normal progression cannot be combined with --all-stars')
    if not 1<=args.frames<=18000:p.error('frames must be 1..18000')
    sys.exit(run(args))
