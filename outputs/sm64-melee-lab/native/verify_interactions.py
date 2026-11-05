#!/usr/bin/env python3
"""Focused end-to-end checks for compositing, grabbing, shields and star poses."""
from pathlib import Path
import argparse,json,subprocess,sys,time
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2]
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--followups',action='store_true');args=parser.parse_args()
    suite=ROOT/'work/interaction-suites'/str(time.time_ns());suite.mkdir(parents=True)
    cases=[('link-grab','link','grab',180),('mario-grab','mario','grab',180),('samus-grab','samus','grab',180),
           ('bowser-tail','link','bowser',210),('shield','link','shield',180),
           ('material-effects','link','effects',120),('stairs','link','stairs',150),
           ('arrow-impact','link','impact',300),('wall-jump','mario','walls',150)]
    cases += [(name+'-star',name,'star',180) for name in
              ('link','sonic','wolf','diddy','charizard','lucas','dedede','tails','iceclimbers','gamewatch')]
    followups=[('zelda-victory-forms','zelda','victory-forms',300),
               ('sonic-repeat-victory','sonic','victory-forms',300),('fox-star-props','fox','star',180)]
    records=[]
    if args.followups:
        records=json.loads((ROOT/'outputs/sm64-melee-lab/verification/interactions-main-20261025.json').read_text())['cases'];cases=followups
    else:cases+=followups
    total=len(records)+len(cases)
    for name,character,scenario,frames in cases:
        command=[sys.executable,str(HERE/'play_embedded.py'),'--smoke','--character',character,
                 '--scenario',scenario,'--frames',str(frames),'--timeout','115','--sound-diagnostic']
        if scenario=='effects':command+=['--render-diagnostic']
        before=set((ROOT/'work/embedded-runs').iterdir());log=suite/(name+'.log')
        with log.open('w') as f:
            run=subprocess.run(command,stdout=f,stderr=subprocess.STDOUT)
        added=set((ROOT/'work/embedded-runs').iterdir())-before
        result_path=next((p/'result.json' for p in sorted(added) if (p/'result.json').exists()),None)
        record=json.loads(result_path.read_text()) if result_path else {'passed':False,'exit_code':run.returncode}
        record.update(name=name,scenario=scenario,result_path=str(result_path) if result_path else None,log=str(log))
        records.append(record)
        report=dict(scope=__doc__,passed=all(r['passed'] for r in records),cases=records,
                    complete=len(records)==total,suite=str(suite),total_cases=total)
        (ROOT/'outputs/sm64-melee-lab/verification/interactions-20261025.json').write_text(json.dumps(report,indent=2)+'\n')
        print(name, 'PASS' if record['passed'] else 'FAIL',
              [k for k,v in record.get('checks',{}).items() if not v],flush=True)
    return 0 if report['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
