#!/usr/bin/env python3
"""Bounded real-engine launches of the authored route. No save completion claims."""
from pathlib import Path
import argparse,json,subprocess,sys,time
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2]
def main():
    p=argparse.ArgumentParser();p.add_argument('--stages',default=','.join(map(str,range(27))));p.add_argument('--frames',type=int,default=240);args=p.parse_args()
    stages=json.loads((HERE/'campaign.json').read_text())['stages']
    out=ROOT/'work'/('stage-verification-'+time.strftime('%Y%m%d-%H%M%S'));out.mkdir();results=[]
    for number in map(int,args.stages.split(',')):
        stage=stages[number];log=out/f'{number:02}-{stage["player"]}.log'
        command=[sys.executable,str(HERE/'play_embedded.py'),'--stage',str(number),'--select','--difficulty','default','--smoke','--scenario','stage','--frames',str(args.frames),'--timeout','100']
        with log.open('w') as f:code=subprocess.run(command,stdout=f,stderr=subprocess.STDOUT).returncode
        text=log.read_text();case=next((line[6:] for line in text.splitlines() if line.startswith('Logs: ')),None)
        details=json.loads((Path(case)/'result.json').read_text()) if case and (Path(case)/'result.json').exists() else {}
        result=dict(stage=number,name=stage['name'],player=stage['player'],passed=code==0,log=str(log),case=case,checks=details.get('checks',{}),exit_code=details.get('exit_code',code),seconds=details.get('seconds'))
        results.append(result);(out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
        print(f'{number:02} {stage["name"]} / {stage["player"]}: {"PASS" if result["passed"] else "FAIL"}',flush=True)
    print(out,flush=True);return int(not all(r['passed'] for r in results))
if __name__=='__main__':raise SystemExit(main())
