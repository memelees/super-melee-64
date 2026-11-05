#!/usr/bin/env python3
"""Exercise every selectable fighter in the actual embedded SM64 host.

This is a compatibility baseline: native render/HUD, movement, jump, attack,
neutral special, geometry and tick synchronization. It does not certify every
special move, boss, transformation, copy ability or complete adventure.
"""
from pathlib import Path
import argparse,json,re,subprocess,sys,time
from play_embedded import BY_NAME
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--characters',nargs='+',default=['gamewatch','luigi','mario']+sorted(set(BY_NAME)-{'gamewatch','luigi','mario'}))
    args=parser.parse_args()
    run=ROOT/'work/roster-suites'/str(time.time_ns());run.mkdir(parents=True)
    report=dict(scope=__doc__,cases=[],full_adventure_verified=False,all_moves_verified=False)
    for character in args.characters:
        log=run/(character+'.log')
        with log.open('w') as f:
            code=subprocess.run([sys.executable,str(HERE/'play_embedded.py'),'--smoke','--roster-test','--all-stars','--character',character,'--frames','180','--timeout','100'],stdout=f,stderr=subprocess.STDOUT,cwd=ROOT).returncode
        text=log.read_text();match=re.search(r'^Logs: (.+)$',text,re.M)
        result_path=Path(match[1])/'result.json' if match else None
        result=json.loads(result_path.read_text()) if result_path and result_path.exists() else dict(passed=False,error=text[-2000:])
        report['cases'].append(dict(character=character,exit_code=code,result=result,logs=str(log)))
        report['passed']=all(c['exit_code']==0 and c['result'].get('passed') for c in report['cases'])
        report['tested']=len(report['cases']);report['selected_roster_size']=len(BY_NAME)
        report['same_build']=len({(c['result'].get('library_sha256'),c['result'].get('host_sha256')) for c in report['cases']})==1
        (run/'result.json').write_text(json.dumps(report,indent=2)+'\n')
        failures=[k for k,v in result.get('checks',{}).items() if not v]
        print(f'{character}: '+('PASS' if code==0 and result.get('passed') else 'FAIL '+str(failures))+f' [{report["tested"]}/{len(args.characters)}]',flush=True)
    target=ROOT/'outputs/sm64-melee-lab/verification/roster.json'
    target.write_text(json.dumps(report,indent=2)+'\n');print(target,flush=True)
    return 0 if report['passed'] and report['same_build'] else 1
if __name__=='__main__':sys.exit(main())
