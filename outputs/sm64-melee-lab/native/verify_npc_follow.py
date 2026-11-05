#!/usr/bin/env python3
"""Verify the enemy squad follows real doors, paintings and area transitions."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess,sys,time

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reuse-report',type=Path,help='Reuse passing cases only when both game binary hashes match')
    args=parser.parse_args()
    previous=json.loads(args.reuse_report.read_text())['cases'] if args.reuse_report else []
    current=(hashlib.sha256((ROOT/'work/melee-embedded/libmelee_embedded.dylib').read_bytes()).hexdigest(),
             hashlib.sha256((ROOT/'work/Super Melee 64.app/Contents/MacOS/sm64').read_bytes()).hexdigest())
    suite=ROOT/'work/npc-follow-suites'/str(time.time_ns())
    suite.mkdir(parents=True)
    cases=[('rooms-and-levels','link','npc-follow',900,['--cpus','3','--all-stars']),
           ('native-combat','link','npcs',540,['--cpus','3']),
           ('partner-and-squad-respawn','iceclimbers','respawn',240,['--cpus','3']),
           ('solo-adventure','link','basic',180,['--cpus','0'])]
    report=dict(scope=__doc__,cases=[],full_game_navigation_verified=False)
    for name,character,scenario,frames,extra in cases:
        reused=next((c for c in previous if c['name']==name and c['result']['passed'] and
            (c['result'].get('library_sha256'),c['result'].get('host_sha256'))==current),None)
        path=suite/(name+'.log')
        if reused:
            code=reused['exit_code'];case=Path(reused['case']);result=reused['result']
        else:
            with path.open('w') as log:
                code=subprocess.run([sys.executable,str(HERE/'play_embedded.py'),'--smoke','--character',character,
                    '--scenario',scenario,'--frames',str(frames),'--timeout','180',*extra],
                    stdout=log,stderr=subprocess.STDOUT,cwd=ROOT).returncode
            match=re.search(r'^Logs: (.+)$',path.read_text(),re.M)
            case=Path(match[1]) if match else None
            result=json.loads((case/'result.json').read_text()) if case and (case/'result.json').exists() else dict(passed=False,error=path.read_text()[-2000:])
        if scenario=='respawn' and case and (case/'game.log').exists():
            rejoins=[dict(re.findall(r'(\w+)=(\d+)',line)) for line in (case/'game.log').read_text().splitlines() if line.startswith('SMB NPC REJOIN')]
            result['checks']['all_cpus_rejoin_after_respawn']=all(any(int(r['slot'])==slot and int(r['reason'])&8 for r in rejoins) for slot in range(1,4))
            result['passed']=result['passed'] and result['checks']['all_cpus_rejoin_after_respawn']
        report['cases'].append(dict(name=name,exit_code=code,case=str(case),result=result))
        print(name+': '+('PASS' if result['passed'] and code==0 else 'FAIL '+str([k for k,v in result.get('checks',{}).items() if not v])),flush=True)
        (suite/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    report['same_build']=len({(c['result'].get('library_sha256'),c['result'].get('host_sha256')) for c in report['cases']})==1
    report['passed']=report['same_build'] and all(c['result']['passed'] and c['exit_code']==0 for c in report['cases'])
    target=ROOT/'outputs/sm64-melee-lab/verification/npc-follow-20261025.json'
    target.write_text(json.dumps(report,indent=2)+'\n')
    print(target)
    return 0 if report['passed'] else 1

if __name__=='__main__':sys.exit(main())
