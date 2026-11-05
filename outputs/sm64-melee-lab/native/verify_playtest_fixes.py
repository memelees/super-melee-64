#!/usr/bin/env python3
"""Real-game checks for host throws, crouch slides, deaths, camera and voices."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess,sys,time
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reuse',action='append',type=Path,default=[])
    args=parser.parse_args()
    current=(hashlib.sha256((ROOT/'work/melee-embedded/libmelee_embedded.dylib').read_bytes()).hexdigest(),
             hashlib.sha256((ROOT/'work/Super Melee 64.app/Contents/MacOS/sm64').read_bytes()).hexdigest())
    cached={}
    for case in args.reuse:
        result=json.loads((case/'result.json').read_text())
        if result['passed'] and (result['library_sha256'],result['host_sha256'])==current:
            text=(case/'game.log').read_text()
            scenario=re.search(r'SMB SCENARIO LEVEL name=([^ ]+)',text)
            if scenario:cached[(result['character'],scenario[1])]=(case,result)
    suite=ROOT/'work/playtest-fix-suites'/str(time.time_ns());suite.mkdir(parents=True)
    cases=[('link','king-bobomb',180,['--input-diagnostic']),('link','heave-ho',180,['--input-diagnostic']),('link','cannon',180,['--input-diagnostic']),
           ('link','slope',260,[]),('sonic','slope',260,[]),('mario','slope',260,['--slope-jump']),
           ('link','respawn',240,[]),('iceclimbers','respawn',240,[]),
           ('link','flight-low',180,[]),('sonic','health',180,[]),('gamewatch','health',180,[]),
           ('link','grab',180,[]),('link','controls',360,['--input-diagnostic'])]
    report=dict(scope=__doc__,cases=[],full_adventure_verified=False)
    for character,scenario,frames,extra in cases:
        name=character+'-'+scenario
        if (character,scenario) in cached:
            case,result=cached[(character,scenario)];code=result['exit_code']
        else:
            path=suite/(name+'.log')
            with path.open('w') as log:
                code=subprocess.run([sys.executable,str(HERE/'play_embedded.py'),'--smoke','--character',character,
                    '--scenario',scenario,'--frames',str(frames),'--sound-diagnostic','--timeout','120',*extra],
                    stdout=log,stderr=subprocess.STDOUT,cwd=ROOT).returncode
            text=path.read_text();match=re.search(r'^Logs: (.+)$',text,re.M)
            case=Path(match[1]) if match else None
            result=json.loads((case/'result.json').read_text()) if case and (case/'result.json').exists() else dict(passed=False,checks={},error=text[-2000:])
        game=(case/'game.log').read_text() if case else ''
        checks=result['checks']
        checks['no_mario_voice_played']='SMB WORLD SOUND event=damage' not in game and 'SMB WORLD SOUND event=grab' not in game
        if scenario in ('flight-low','health'):
            voices=[tuple(map(int,row)) for row in re.findall(r'SMB DAMAGE VOICE kind=(\d+) samples=(\d+) channel=(-?\d+)',game)]
            checks['native_damage_voice_or_silence']=bool(voices) and all(count==0 or channel>=0 for _,count,channel in voices)
            checks['mario_damage_voice_suppressed']='suppressed=mario_voice sound=240aff81' in game
        if scenario=='grab':checks['mario_grab_voice_suppressed']=bool(re.search(r'suppressed=mario_voice sound=240[67]8081',game))
        if scenario in ('king-bobomb','heave-ho'):
            checks['mario_grab_voice_suppressed']='suppressed=mario_voice sound=240b8081' in game
        result['passed']=result['passed'] and all(checks.values())
        report['cases'].append(dict(name=name,result=result,exit_code=code,case=str(case)))
        failed=[key for key,value in checks.items() if not value]
        print(name+': '+('PASS' if result['passed'] and code==0 else 'FAIL '+str(failed)),flush=True)
        (suite/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    report['same_build']=len({(c['result'].get('library_sha256'),c['result'].get('host_sha256')) for c in report['cases']})==1
    report['passed']=report['same_build'] and all(c['result']['passed'] and c['exit_code']==0 for c in report['cases'])
    target=ROOT/'outputs/sm64-melee-lab/verification/playtest-fixes-20261025.json'
    target.write_text(json.dumps(report,indent=2)+'\n');print(target,flush=True)
    return 0 if report['passed'] else 1

if __name__=='__main__':sys.exit(main())
