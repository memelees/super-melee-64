#!/usr/bin/env python3
"""Real native level-9 combat, course changes, selector and solo regressions."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess,sys,time
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2]
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--reuse-npcs',type=Path);args=p.parse_args()
    suite=ROOT/'work/npc-selection-suites'/str(time.time_ns());suite.mkdir(parents=True)
    cases=[('level9-enemies','link','npcs',540,['--cpus','3']),
           ('selector-sheik-solo','sheik','basic',180,['--select','--cpus','0']),
           ('solo-specials','link','controls',360,['--input-diagnostic']),
           ('solo-slide','link','slope',260,[]),
           ('climbers-respawn-enemies','iceclimbers','respawn',240,['--cpus','3']),
           ('solo-cannon','link','cannon',180,['--input-diagnostic'])]
    report=dict(scope=__doc__,cases=[],full_level_navigation_verified=False)
    library=hashlib.sha256((ROOT/'work/melee-embedded/libmelee_embedded.dylib').read_bytes()).hexdigest()
    host=hashlib.sha256((ROOT/'work/Super Melee 64.app/Contents/MacOS/sm64').read_bytes()).hexdigest()
    for name,character,scenario,frames,extra in cases:
        case=args.reuse_npcs.resolve() if name=='level9-enemies' and args.reuse_npcs else None
        if case:
            result=json.loads((case/'result.json').read_text());assert result['passed'] and result['host_sha256']==host and result['library_sha256']==library
            code=result['exit_code']
        else:
            path=suite/(name+'.log')
            with path.open('w') as log:
                code=subprocess.run([sys.executable,str(HERE/'play_embedded.py'),'--smoke','--character',character,'--scenario',scenario,
                    '--frames',str(frames),'--timeout','150','--sound-diagnostic',*extra],stdout=log,stderr=subprocess.STDOUT,cwd=ROOT).returncode
            match=re.search(r'^Logs: (.+)$',path.read_text(),re.M);case=Path(match[1]) if match else None
            result=json.loads((case/'result.json').read_text()) if case and (case/'result.json').exists() else dict(passed=False,error=path.read_text()[-2000:])
        report['cases'].append(dict(name=name,exit_code=code,case=str(case),result=result))
        failed=[k for k,v in result.get('checks',{}).items() if not v]
        print(name+': '+('PASS' if result['passed'] and code==0 else 'FAIL '+str(failed)),flush=True)
        (suite/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    report['same_build']=len({(c['result'].get('host_sha256'),c['result'].get('library_sha256')) for c in report['cases']})==1
    report['passed']=report['same_build'] and all(c['result']['passed'] and c['exit_code']==0 for c in report['cases'])
    target=ROOT/'outputs/sm64-melee-lab/verification/npcs-selection-20261025.json';target.write_text(json.dumps(report,indent=2)+'\n');print(target)
    return 0 if report['passed'] else 1
if __name__=='__main__':sys.exit(main())
