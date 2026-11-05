#!/usr/bin/env python3
"""Native squad selection/HUD, head starts, midpoint catch-up and respawning."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess,sys,time
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reuse-report',type=Path)
    args=parser.parse_args()
    previous=json.loads(args.reuse_report.read_text())['cases'] if args.reuse_report else []
    current=(hashlib.sha256((ROOT/'work/melee-embedded/libmelee_embedded.dylib').read_bytes()).hexdigest(),
             hashlib.sha256((ROOT/'work/Super Melee 64.app/Contents/MacOS/sm64').read_bytes()).hexdigest())
    cases=[('three-custom-fighters','link','npcs',540,['--select','--enemies','bowser,sonic,gamewatch']),
           ('one-enemy','link','basic',180,['--select','--enemies','sheik']),
           ('two-enemies-with-partner','link','basic',180,['--select','--enemies','iceclimbers,tails']),
           ('zero-enemies','sheik','basic',180,['--select','--cpus','0']),
           ('door-headstarts','link','npc-follow',900,['--cpus','3','--all-stars']),
           ('midpoint-catchup','link','npc-catchup',300,['--enemies','mario,fox,link']),
           ('respawn-with-partners','iceclimbers','respawn',300,['--enemies','sheik,kirby,iceclimbers'])]
    suite=ROOT/'work/npc-configuration-suites'/str(time.time_ns());suite.mkdir(parents=True)
    report=dict(scope=__doc__,cases=[],full_game_navigation_verified=False)
    for name,character,scenario,frames,extra in cases:
        reused=next((c for c in previous if c['name']==name and c['result']['passed'] and
            (c['result'].get('library_sha256'),c['result'].get('host_sha256'))==current),None)
        if reused:code=reused['exit_code'];case=Path(reused['case']);result=reused['result']
        else:
            path=suite/(name+'.log')
            with path.open('w') as log:
                code=subprocess.run([sys.executable,str(HERE/'play_embedded.py'),'--smoke','--character',character,
                    '--scenario',scenario,'--frames',str(frames),'--timeout','180',*extra],
                    stdout=log,stderr=subprocess.STDOUT,cwd=ROOT).returncode
            match=re.search(r'^Logs: (.+)$',path.read_text(),re.M);case=Path(match[1]) if match else None
            result=json.loads((case/'result.json').read_text()) if case and (case/'result.json').exists() else dict(passed=False,error=path.read_text()[-2000:])
        if case and (case/'game.log').exists():
            log=(case/'game.log').read_text()
            rejoined=[dict(re.findall(r'(\w+)=(-?[\d.]+)',s)) for s in log.splitlines() if s.startswith('SMB NPC REJOIN')]
            if scenario=='respawn':
                result['checks']['all_cpus_rejoin_after_respawn']=all(any(int(r['slot'])==slot and int(r['reason'])&8 and int(r['wait_frames'])>=90 for r in rejoined) for slot in range(1,4))
                result['checks']['cpu_partner_rejoins']='SMB NPC SPAWN slot=3 sub=1 ' in log
            if name=='two-enemies-with-partner':result['checks']['cpu_partner_rendered']='SMB NATIVE FIGHTERS count=4 ' in log
            result['passed']=result['passed'] and all(result.get('checks',{}).values())
        report['cases'].append(dict(name=name,exit_code=code,case=str(case),result=result))
        print(name+': '+('PASS' if result['passed'] and code==0 else 'FAIL '+str([k for k,v in result.get('checks',{}).items() if not v])),flush=True)
        (suite/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    report['same_build']=len({(c['result'].get('library_sha256'),c['result'].get('host_sha256')) for c in report['cases']})==1
    report['passed']=report['same_build'] and all(c['result']['passed'] and c['exit_code']==0 for c in report['cases'])
    target=ROOT/'outputs/sm64-melee-lab/verification/npc-configuration-20261025.json'
    target.write_text(json.dumps(report,indent=2)+'\n');print(target)
    return 0 if report['passed'] else 1
if __name__=='__main__':sys.exit(main())
