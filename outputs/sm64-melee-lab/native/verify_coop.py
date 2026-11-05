#!/usr/bin/env python3
"""Two real Coop Deluxe owners, separate native fighters, one shared world."""
from pathlib import Path
import json,re,subprocess,sys,time
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2]
def main():
    case=ROOT/'work/coop-verification'/str(time.time_ns());case.mkdir(parents=True)
    processes=[];files=[]
    try:
        for name,character,role,frames in [('host','link',['--host'],1200),('client','sonic',['--join','127.0.0.1'],700)]:
            output=(case/(name+'.log')).open('w');files.append(output)
            command=[sys.executable,str(HERE/'play_coop.py'),*role,'--character',character,'--difficulty','none','--port','7794','--test-frames',str(frames),'--timeout','160']
            processes.append(subprocess.Popen(command,stdout=output,stderr=subprocess.STDOUT))
            if name=='host':time.sleep(5)
        deadline=time.monotonic()+180
        while any(p.poll() is None for p in processes) and time.monotonic()<deadline:time.sleep(.5)
        result=dict(scope=__doc__,full_16_player_session_tested=False,checks={})
        for name,p in zip(('host','client'),processes):
            if p.poll() is None:p.terminate();p.wait(timeout=10)
            output=(case/(name+'.log')).read_text();match=re.search(r'Logs: (.+)',output)
            game=Path(match[1])/'game.log' if match else None
            text=game.read_text() if game and game.exists() else output
            result[name]=dict(exit_code=p.returncode,log=str(game),remote_fighters=re.findall(r'SMB COOP FIGHTER .*',text))
            result['checks'][name+'_completed']=p.returncode==0 and 'SMB HOST SMOKE COMPLETE' in text
            result['checks'][name+'_remote_native_fighter']=('SMB COOP FIGHTER seat=4' in text or 'SMB COOP FIGHTER peer=1' in text)
            result['checks'][name+'_native_render']='SMB HOST NATIVE RENDER' in text and 'SMB NATIVE FIGHTERS count=2' in text
            movement=[tuple(map(float,r)) for r in re.findall(r'SMB COOP OWNED frame=(\d+) x=([-\d.]+) z=([-\d.]+)',text) if int(r[0])>=240]
            result['checks'][name+'_owned_movement_with_peers']=bool(movement) and max(((r[1]-movement[0][1])**2+(r[2]-movement[0][2])**2)**.5 for r in movement)>10
            result['checks'][name+'_no_remote_errors']='SMB COOP REMOTE ERROR' not in text
            result['checks'][name+'_correct_local_character']=('SMB HOST FRAME frame=1 kind='+('6' if name=='host' else '37')) in text
        result['passed']=all(result['checks'].values());result['logs']=str(case)
        target=HERE.parent/'verification/multiplayer.json';target.parent.mkdir(parents=True,exist_ok=True);target.write_text(json.dumps(result,indent=2)+'\n')
        print(json.dumps(result,indent=2));return 0 if result['passed'] else 1
    finally:
        for p in processes:
            if p.poll() is None:p.kill();p.wait()
        for file in files:file.close()
if __name__=='__main__':raise SystemExit(main())
