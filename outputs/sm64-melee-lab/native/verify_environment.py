#!/usr/bin/env python3
"""Check the actual VCUTM ramp, native respawns, particle depth and SFX output."""
from pathlib import Path
import json,re,subprocess,sys,time
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
def main():
    run=ROOT/'work/environment-suites'/str(time.time_ns());run.mkdir(parents=True)
    cases=[('particle-depth-negative','mario','effects',150,['--effect-depth-off']),
        ('particle-depth','mario','effects',150,[]),
        ('sonic-vcutm','sonic','slope',260,[]),('link-vcutm','link','slope',260,[]),
        ('mario-slide-jump','mario','slope',260,['--slope-jump']),
        ('link-respawn','link','respawn',240,[]),('climbers-respawn','iceclimbers','respawn',240,[]),
        ('sonic-effects-audio','sonic','basic',200,['--roster-test','--sound-diagnostic'])]
    report=dict(scope=__doc__,cases=[],full_adventure_verified=False,all_effects_and_sounds_verified=False)
    for name,character,scenario,frames,extra in cases:
        path=run/(name+'.log')
        with path.open('w') as log:code=subprocess.run([sys.executable,str(HERE/'play_embedded.py'),'--smoke','--character',character,'--scenario',scenario,'--frames',str(frames),'--timeout','120',*extra],stdout=log,stderr=subprocess.STDOUT,cwd=ROOT).returncode
        text=path.read_text();match=re.search(r'^Logs: (.+)$',text,re.M);case=Path(match[1]) if match else None
        result=json.loads((case/'result.json').read_text()) if case and (case/'result.json').exists() else dict(passed=False,error=text[-2000:])
        game=(case/'game.log').read_text() if case and (case/'game.log').exists() else ''
        if scenario=='slope' and '--slope-jump' not in extra:
            heights=[float(v) for v in re.findall(r'SMB SLOPE .* y=([-\d.]+)',game)]
            result.setdefault('checks',{})['survives_below_original_blast_plane']=bool(heights) and min(heights)<-2500
        if character=='iceclimbers' and scenario=='respawn':
            result.setdefault('checks',{})['partner_rebirth']='SMB RESPAWN PLATFORM slot=0 sub=1' in game
        if '--sound-diagnostic' in extra:
            rms=[float(v) for v in re.findall(r'SMB AUDIO OUTPUT .*rms=([\d.]+)',game)]
            result.setdefault('checks',{})['native_pcm_audible']=any(v>.001 for v in rms)
            result['checks']['native_pcm_captured']=(case/'native-audio.f32').stat().st_size>100000 if case and (case/'native-audio.f32').exists() else False
        result['passed']=result.get('passed',False) and all(result.get('checks',{}).values())
        report['cases'].append(dict(name=name,character=character,scenario=scenario,result=result,exit_code=code,logs=str(path)))
        report['passed']=all(c['exit_code']==0 and c['result'].get('passed') for c in report['cases'])
        (run/'result.json').write_text(json.dumps(report,indent=2)+'\n')
        failed=[k for k,v in result.get('checks',{}).items() if not v]
        print(name+': '+('PASS' if code==0 and result.get('passed') else 'FAIL '+str(failed)),flush=True)
    report['same_build']=len({(c['result'].get('library_sha256'),c['result'].get('host_sha256')) for c in report['cases']})==1
    report['passed']=report['passed'] and report['same_build']
    target=ROOT/'outputs/sm64-melee-lab/verification/environment.json';target.write_text(json.dumps(report,indent=2)+'\n');print(target,flush=True)
    return 0 if report['passed'] else 1
if __name__=='__main__':sys.exit(main())
