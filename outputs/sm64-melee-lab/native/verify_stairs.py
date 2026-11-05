#!/usr/bin/env python3
"""Check native stair walking and the collision seams it must preserve."""
from pathlib import Path
import json,re,subprocess,sys,time
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]

def main():
    run=ROOT/'work/stair-suites'/str(time.time_ns());run.mkdir(parents=True)
    cases=[('link-tall-step','link','stairs',0),('link-stair-flight','link','stairs',9),
        ('sonic-stair-flight','sonic','stairs',9),('wall-containment','link','barriers',0),
        ('real-ledges','link','ledges',0),('moving-platform','link','platforms',0),
        ('mario-wall-jump','mario','walls',0)]
    results=[]
    for name,character,scenario,ordinal in cases:
        args=[sys.executable,str(HERE/'play_embedded.py'),'--smoke','--character',character,
            '--scenario',scenario,'--stair-ordinal',str(ordinal),'--frames','150','--timeout','100']
        log=run/(name+'.log')
        with log.open('w') as f:code=subprocess.run(args,cwd=ROOT,stdout=f,stderr=subprocess.STDOUT).returncode
        text=log.read_text();match=re.search(r'^Logs: (.+)$',text,re.M)
        result_path=Path(match[1])/'result.json' if match else None
        result=json.loads(result_path.read_text()) if result_path and result_path.is_file() else dict(passed=False,error=text[-1500:])
        results.append(dict(name=name,character=character,scenario=scenario,ordinal=ordinal,exit_code=code,result=result,logs=str(log)))
        failed=[k for k,v in result.get('checks',{}).items() if not v]
        print(name+': '+('PASS' if code==0 and result.get('passed') else 'FAIL '+str(failed)),flush=True)
        report=dict(passed=all(r['exit_code']==0 and r['result'].get('passed') for r in results),scope=__doc__,
            cases=results,step_limit_sm64_units=60,visual_sm64_geometry_changed=False,full_adventure_verified=False)
        (run/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    libraries={r['result'].get('library_sha256') for r in results}
    hosts={r['result'].get('host_sha256') for r in results}
    report['same_build']=len(libraries)==len(hosts)==1 and None not in libraries and None not in hosts
    report['passed']=report['passed'] and report['same_build']
    target=ROOT/'outputs/sm64-melee-lab/verification/stairs.json'
    target.write_text(json.dumps(report,indent=2)+'\n')
    print(target,flush=True)
    return 0 if report['passed'] else 1

if __name__=='__main__':sys.exit(main())
