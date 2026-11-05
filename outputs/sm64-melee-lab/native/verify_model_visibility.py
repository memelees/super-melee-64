#!/usr/bin/env python3
"""Compare actual fighter mesh visibility with native Melee model selections."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess,sys,time
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2]
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--reuse-report',type=Path);args=p.parse_args()
    previous=json.loads(args.reuse_report.read_text())['cases'] if args.reuse_report else []
    current=(hashlib.sha256((ROOT/'work/melee-embedded/libmelee_embedded.dylib').read_bytes()).hexdigest(),hashlib.sha256((ROOT/'work/Super Melee 64.app/Contents/MacOS/sm64').read_bytes()).hexdigest())
    cases=[('legacy-squad-reproduction','link','npcs',540,['--enemies','bowser,sonic,gamewatch','--legacy-model-visibility']),
           ('bowser-specials-and-interruption','bowser','model-forms',480,[]),
           ('sonic-specials-and-interruption','sonic','model-forms',480,[]),
           ('fixed-squad','link','npcs',540,['--enemies','bowser,sonic,gamewatch']),
           ('doors-and-wakeup','link','npc-follow',900,['--enemies','bowser,sonic,samus','--all-stars']),
           ('partner-and-outline-regression','iceclimbers','basic',240,['--enemies','samus,yoshi,gamewatch'])]
    suite=ROOT/'work/model-visibility-suites'/str(time.time_ns());suite.mkdir(parents=True)
    report=dict(scope=__doc__,cases=[])
    for name,character,scenario,frames,extra in cases:
        reused=next((c for c in previous if c['name']==name and c.get('passed') and
            (c['result'].get('library_sha256'),c['result'].get('host_sha256'))==current),None)
        if reused:code=reused['exit_code'];case=Path(reused['case']);result=reused['result']
        else:
            path=suite/(name+'.log')
            with path.open('w') as stream:
                code=subprocess.run([sys.executable,str(HERE/'play_embedded.py'),'--smoke','--character',character,
                    '--scenario',scenario,'--frames',str(frames),'--timeout','180','--model-diagnostic',*extra],stdout=stream,stderr=subprocess.STDOUT,cwd=ROOT).returncode
            match=re.search(r'^Logs: (.+)$',path.read_text(),re.M);case=Path(match[1]) if match else None
            result=json.loads((case/'result.json').read_text()) if case and (case/'result.json').exists() else dict(passed=False,error=path.read_text()[-2000:])
        text=(case/'game.log').read_text() if case else ''
        rows=[{k:int(v) for k,v in re.findall(r'(\w+)=(-?\d+)',line)} for line in text.splitlines() if line.startswith('SMB MODEL CHECK')]
        mismatch=sum(r['mismatch'] for r in rows);neutral=sum(r['mismatch'] for r in rows if 14<=r['motion']<24)
        checks=dict(authored_parts_checked=bool(rows) and any(r['checked']>0 for r in rows))
        if name.startswith('legacy'):checks['reproduces_stale_walking_model']=neutral>0
        else:checks['all_visible_parts_match_native_selection']=mismatch==0
        if scenario=='model-forms':
            main=[r for r in rows if r['slot']==0 and r['sub']==0]
            checks['special_model_remains_available']=any(r['motion']>=341 and r['model0']!=r['default0'] for r in main)
            special=next((i for i,r in enumerate(main) if r['motion']>=341),len(main))
            checks['returns_to_normal_body']=any(14<=r['motion']<24 and r['model0']==r['default0'] and r['mismatch']==0 for r in main[special+1:])
        passed=code==0 and result['passed'] and all(checks.values())
        entry=dict(name=name,passed=passed,exit_code=code,case=str(case),checks=checks,mesh_checks=len(rows),mismatched_parts=mismatch,neutral_mismatched_parts=neutral,result=result)
        report['cases'].append(entry)
        print(name+': '+('PASS' if passed else 'FAIL '+str([k for k,v in {**result.get('checks',{}),**checks}.items() if not v])),flush=True)
        (suite/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    report['same_build']=len({(c['result'].get('library_sha256'),c['result'].get('host_sha256')) for c in report['cases']})==1
    report['passed']=report['same_build'] and all(c['passed'] for c in report['cases'])
    target=ROOT/'outputs/sm64-melee-lab/verification/model-visibility-20261025.json'
    target.write_text(json.dumps(report,indent=2)+'\n');print(target)
    return 0 if report['passed'] else 1
if __name__=='__main__':sys.exit(main())
