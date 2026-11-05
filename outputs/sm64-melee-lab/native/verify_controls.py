#!/usr/bin/env python3
"""Exercise native directional specials, jump audio and Melee3D camera orbit."""
from pathlib import Path
import json,re,subprocess,sys,time
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]

def main():
    run=ROOT/'work/control-suites'/str(time.time_ns());run.mkdir(parents=True)
    report=dict(scope=__doc__,cases=[],all_moves_verified=False)
    for name,character,scenario in [('sonic','sonic','controls'),('link','link','controls'),('mario','mario','controls'),('fox','fox','controls'),('sonic-controller','sonic','controller'),('link-controller','link','controller')]:
        path=run/(name+'.log')
        with path.open('w') as log:
            code=subprocess.run([sys.executable,str(HERE/'play_embedded.py'),'--smoke','--character',character,
                '--scenario',scenario,'--camera','melee','--frames','360','--sound-diagnostic','--input-diagnostic','--timeout','120'],stdout=log,stderr=subprocess.STDOUT,cwd=ROOT).returncode
        text=path.read_text();match=re.search(r'^Logs: (.+)$',text,re.M)
        case=Path(match[1]) if match else None
        result=json.loads((case/'result.json').read_text()) if case and (case/'result.json').exists() else dict(passed=False,error=text[-2000:])
        game=(case/'game.log').read_text() if case and (case/'game.log').exists() else ''
        inputs=[{k:float(v) for k,v in re.findall(r'(seq|y|move)=(-?[\d.]+)',line)} for line in game.splitlines() if line.startswith('SMB NATIVE INPUT')]
        before=[r['y'] for r in inputs if 315<=r['seq']<=320]
        recovery=[r['y'] for r in inputs if 321<=r['seq']<480]
        result['recovery_rise_world_units']=(max(recovery)-max(before))*10 if before and recovery else 0
        checks=result.setdefault('checks',{})
        checks['up_special_gains_height']=result['recovery_rise_world_units']>20
        fovs=[float(v) for v in re.findall(r'SMB CAMERA .*fov=([\d.]+)',game)]
        checks['native_camera_fov']=bool(fovs) and all(30<=f<=90 for f in fovs)
        rms=[float(v) for v in re.findall(r'SMB AUDIO OUTPUT .*rms=([\d.]+)',game)]
        checks['native_pcm_audible']=bool(rms) and max(rms)>.001
        checks['akaneia_audio_overlay']='M3D AUDIO OVERLAY files=' in game
        result['passed']=result.get('passed',False) and all(checks.values())
        report['cases'].append(dict(name=name,result=result,exit_code=code,logs=str(path)))
        failed=[k for k,v in checks.items() if not v]
        print(name+': '+('PASS' if result['passed'] and code==0 else 'FAIL '+str(failed)),flush=True)
        (run/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    report['same_build']=len({(c['result'].get('library_sha256'),c['result'].get('host_sha256')) for c in report['cases']})==1
    report['passed']=report['same_build'] and all(c['result']['passed'] and c['exit_code']==0 for c in report['cases'])
    target=ROOT/'outputs/sm64-melee-lab/verification/controls-camera-audio.json'
    target.write_text(json.dumps(report,indent=2)+'\n');print(target,flush=True)
    return 0 if report['passed'] else 1

if __name__=='__main__':sys.exit(main())
