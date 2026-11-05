#!/usr/bin/env python3
"""Native encounter modes, KO presentation, and repeated real star exits."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess,sys,time
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2]
CASES=[('impossible-castle','mario','basic',240,['--difficulty','impossible','--select']),
       ('default-waves','link','campaign-waves',1100,['--difficulty','default','--sound-diagnostic']),
       ('mario-star-returns','mario','star-return',480,[]),
       ('sonic-star-returns','sonic','star-return',480,[]),
       ('climbers-star-returns','iceclimbers','star-return',480,[]),
       ('default-star-returns','mario','star-return',480,['--difficulty','default','--model-diagnostic'])]
def extra_checks(name,case):
    text=(case/'game.log').read_text();checks={}
    if name=='impossible-castle':
        checks['selected_impossible']='SMB SELECT MODE name=impossible' in text
        checks['opening_lineup']='mode=impossible wave=1/1 count=3 external=17,12,5 levels=9,9,9' in text
        rows=[l for l in text.splitlines() if l.startswith('SMB NPC FRAME')]
        checks['three_live_level9_fighters']=all(any(f'slot={i} kind={kind} level=9 ' in l and 'ready=1 ' in l for l in rows) for i,kind in ((1,14),(2,9),(3,5)))
    if name=='default-waves':
        checks['grounds_have_yoshi']='key=1016 mode=default wave=1/1 count=1 external=17,0,0 levels=3,0,0' in text
        checks['consecutive_opponents']=all(f'wave={i}/3 count=1 ' in text for i in (1,2,3))
        checks['names_and_defeated_resolve']=len(re.findall(r'SMB KO ANNOUNCER name=\d+ sound=[0-9a-f]+ voice=\d+ generic=[01]',text))>=3 and len(re.findall(r'SMB KO ANNOUNCER defeated=1 voice=\d+',text))>=3
        checks['no_missing_audio']='sfx MISS' not in text
    if name=='default-star-returns':
        checks['castle_single_peach']='key=1006 mode=default wave=1/1 count=1 external=12,0,0 levels=5,0,0' in text
        checks['battlefield_strong_duel']='mode=default wave=1/1 count=1 external=6,0,0 levels=6,0,0' in text
        checks['models_survive_warps']=bool(re.search(r'SMB MODEL CHECK .*mismatch=0',text)) and not re.search(r'SMB MODEL CHECK .*mismatch=[1-9]',text)
    return checks

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--reuse-report',type=Path);args=parser.parse_args()
    old=json.loads(args.reuse_report.read_text()) if args.reuse_report else []
    if isinstance(old,dict):old=old['cases']
    current=(hashlib.sha256((ROOT/'work/melee-embedded/libmelee_embedded.dylib').read_bytes()).hexdigest(),hashlib.sha256((ROOT/'work/Super Melee 64.app/Contents/MacOS/sm64').read_bytes()).hexdigest())
    suite=ROOT/'work/adventure-mode-suites'/str(time.time_ns());suite.mkdir(parents=True);cases=[]
    for name,character,scenario,frames,extra in CASES:
        reused=next((c for c in old if c['name']==name and c.get('exit_code')==0 and c['result'].get('passed') and (c['result'].get('library_sha256'),c['result'].get('host_sha256'))==current),None)
        if reused:case=Path(reused['case']);code=0;result=reused['result']
        else:
            path=suite/(name+'.log')
            with path.open('w') as stream:code=subprocess.run([sys.executable,str(HERE/'play_embedded.py'),'--smoke','--character',character,'--scenario',scenario,'--frames',str(frames),'--timeout','200',*extra],stdout=stream,stderr=subprocess.STDOUT,cwd=ROOT).returncode
            match=re.search(r'^Logs: (.+)$',path.read_text(),re.M);case=Path(match[1]) if match else None
            result=json.loads((case/'result.json').read_text()) if case and (case/'result.json').exists() else {'passed':False,'error':path.read_text()[-2000:]}
        checks=extra_checks(name,case) if case and (case/'game.log').exists() else {'log_available':False}
        passed=code==0 and result.get('passed',False) and all(checks.values())
        cases.append(dict(name=name,case=str(case),exit_code=code,passed=passed,extra_checks=checks,result=result));print(name,passed,checks,flush=True)
    report=dict(scope=__doc__,passed=all(c['passed'] for c in cases),same_build=len({(c['result'].get('library_sha256'),c['result'].get('host_sha256')) for c in cases})==1,cases=cases)
    dest=ROOT/'outputs/sm64-melee-lab/verification/adventure-modes-20261026.json';dest.write_text(json.dumps(report,indent=2)+'\n');print(dest)
    return 0 if report['passed'] else 1
if __name__=='__main__':sys.exit(main())
