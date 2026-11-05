#!/usr/bin/env python3
"""Check roster-specific adventure seams, unlock persistence and speed tuning."""
from pathlib import Path
import json,re,subprocess,sys,time
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
def main():
    run=ROOT/'work/roster-edge-suites'/str(time.time_ns());run.mkdir(parents=True)
    profile=ROOT/'work/unlocked-profile'
    cases=[('unlocked-profile','mario','basic',180,['--save-profile',str(profile),'--all-stars']),
        ('unlocked-reload','mario','basic',180,['--save-profile',str(profile),'--all-stars']),
        ('mario-original-speed','mario','basic',180,['--speed','1']),
        ('zelda-to-sheik','zelda','transform',180,[]),('sheik-to-zelda','sheik','transform',180,[]),
        ('climbers-castle-entry','iceclimbers','doors',180,[]),
        ('gamewatch-wall','gamewatch','barriers',150,[]),
        ('link-stair-flight','link','stairs',150,['--stair-ordinal','9']),
        ('sonic-stair-flight','sonic','stairs',150,['--stair-ordinal','9'])]
    report=dict(scope=__doc__,cases=[],all_moves_verified=False,full_adventure_verified=False)
    for name,character,scenario,frames,extra in cases:
        log=run/(name+'.log')
        with log.open('w') as f:code=subprocess.run([sys.executable,str(HERE/'play_embedded.py'),'--smoke','--character',character,'--scenario',scenario,'--frames',str(frames),'--timeout','100',*extra],stdout=f,stderr=subprocess.STDOUT,cwd=ROOT).returncode
        text=log.read_text();m=re.search(r'^Logs: (.+)$',text,re.M)
        case=Path(m[1]) if m else None
        result=json.loads((case/'result.json').read_text()) if case and (case/'result.json').exists() else dict(passed=False,error=text[-1500:])
        if case and name=='unlocked-reload':
            persisted='previous_stars=120 stars=120 cannons=15' in (case/'game.log').read_text()
            result.setdefault('checks',{})['unlocked_save_persisted']=persisted
            result['passed']=result.get('passed',False) and persisted
        report['cases'].append(dict(name=name,character=character,scenario=scenario,exit_code=code,result=result,logs=str(log)))
        report['passed']=all(c['exit_code']==0 and c['result'].get('passed') for c in report['cases'])
        (run/'result.json').write_text(json.dumps(report,indent=2)+'\n')
        failed=[k for k,v in result.get('checks',{}).items() if not v]
        print(name+': '+('PASS' if code==0 and result.get('passed') else 'FAIL '+str(failed)),flush=True)
    by_name={c['name']:c['result'] for c in report['cases']}
    normal=by_name['mario-original-speed'].get('native_attributes',[{}])[0]
    boosted=by_name['unlocked-profile'].get('native_attributes',[{}])[0]
    scaled=('walk','dash_initial','run','jump_h','air_h')
    unchanged=('jump_squat','jump_v','hop_v','gravity','terminal','jumps','weight','walljump')
    report['speed_comparison']=dict(original=normal,boosted=boosted,
        horizontal_attributes_scale=all(abs(boosted.get(k,0)-normal.get(k,0)*1.25)<.00001 and normal.get(k,0)>0 for k in scaled),
        vertical_and_character_attributes_unchanged=all(k in normal and boosted.get(k)==normal[k] for k in unchanged))
    report['same_build']=len({(c['result'].get('library_sha256'),c['result'].get('host_sha256')) for c in report['cases']})==1
    report['passed']=report['passed'] and report['same_build'] and report['speed_comparison']['horizontal_attributes_scale'] and report['speed_comparison']['vertical_and_character_attributes_unchanged']
    target=ROOT/'outputs/sm64-melee-lab/verification/roster-edges.json';target.write_text(json.dumps(report,indent=2)+'\n');print(target,flush=True)
    return 0 if report['passed'] else 1
if __name__=='__main__':sys.exit(main())
