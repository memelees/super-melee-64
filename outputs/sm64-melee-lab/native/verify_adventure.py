#!/usr/bin/env python3
"""Exercise the real embedded game; use isolated saves and test-only positioning.

These checks verify engine seams, not that a human completed the missions.
"""
from pathlib import Path
import json,re,subprocess,sys,time
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]

def main():
    run=ROOT/'work/adventure-suites'/str(time.time_ns());run.mkdir(parents=True)
    profile=run/'star-save'
    cases=[('castle-stairs','link','stairs',150),('link-ledges','link','ledges',150),('terrain-barriers','link','barriers',150),
        ('flight-low','link','flight-low',200),('flight-high','link','flight-high',200),
        ('link-basic','link','basic',900),('sonic-basic','sonic','basic',180),
        ('mario-wall-jump','mario','walls',150),('moving-platform','link','platforms',150),
        ('arrow-impact','link','impact',300),('bomb-impact','link','bomb-impact',300),
        ('world-health','link','health',220),('star-save','link','star',180),
        ('save-reload','link','reload-save',150)]
    results=[]
    for name,character,scenario,frames in cases:
        args=[sys.executable,str(HERE/'play_embedded.py'),'--smoke','--character',character,'--scenario',scenario,'--frames',str(frames),'--timeout','110']
        if scenario in ('star','reload-save'):args+=['--save-profile',str(profile)]
        path=run/(name+'.log')
        with path.open('w') as log:code=subprocess.run(args,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT).returncode
        output=path.read_text();match=re.search(r'^Logs: (.+)$',output,re.M)
        result_path=Path(match[1])/'result.json' if match else None
        result=json.loads(result_path.read_text()) if result_path and result_path.is_file() else {'passed':False,'error':output[-2000:]}
        record=dict(name=name,character=character,scenario=scenario,frames=frames,exit_code=code,result=result,logs=str(path))
        results.append(record)
        failed=[k for k,v in result.get('checks',{}).items() if not v]
        print(name+': '+('PASS' if code==0 and result.get('passed') else 'FAIL '+str(failed)),flush=True)
        summary=dict(passed=all(r['exit_code']==0 and r['result'].get('passed') for r in results),
            scope=__doc__,cases=results,full_playthrough_verified=False,coop_verified=False)
        (run/'result.json').write_text(json.dumps(summary,indent=2)+'\n')
    flights={}
    for record in results:
        if record['name'] not in ('flight-low','flight-high'):continue
        log=Path(record['logs']).read_text();match=re.search(r'^Logs: (.+)$',log,re.M)
        text=(Path(match[1])/'game.log').read_text() if match else ''
        rows=[{k:float(v) for k,v in re.findall(r'(\w+)=(-?[\d.]+)',line)} for line in text.splitlines() if line.startswith('SMB SCENARIO FLIGHT')]
        if rows:
            start=rows[0]
            flights[record['name']]=max(sum((r[k]-start[k])**2 for k in ('x','z'))**.5 for r in rows if r['tick']-start['tick']<=20)
    comparison=flights.get('flight-high',0)>flights.get('flight-low',0)*1.25>0
    summary.update(flight_distance_first_20_host_frames=flights,higher_percent_launches_farther=comparison)
    summary['passed']=summary['passed'] and comparison
    (run/'result.json').write_text(json.dumps(summary,indent=2)+'\n')
    target=ROOT/'outputs/sm64-melee-lab/verification/adventure-suite.json'
    target.write_text(json.dumps(summary,indent=2)+'\n')
    print(target,flush=True)
    return 0 if summary['passed'] else 1
if __name__=='__main__':sys.exit(main())
