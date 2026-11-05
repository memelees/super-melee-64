#!/usr/bin/env python3
"""Exercise native projectiles, combat, grabs and shared campaign over real LAN packets."""
from pathlib import Path
import argparse,json,math,re,subprocess,sys,time
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2]
def records(text,prefix):
    return [dict(re.findall(r'(\w+)=([^\s]+)',line)) for line in text.splitlines() if line.startswith(prefix+' ')]
def number(row,key):
    try:return float(row[key])
    except (KeyError,ValueError):return float('nan')
def launch(row,keys):
    values=[number(row,key) for key in keys]
    return all(math.isfinite(value) for value in values) and sum(value*value for value in values)>.0001
def run(mode,port):
    case=ROOT/'work/coop-feature-tests'/(mode+'-'+str(time.time_ns()));case.mkdir(parents=True)
    processes=[];files=[];result={'mode':mode,'checks':{}}
    try:
        for name,character,role,frames in [('host','link',['--host'],1400 if mode=='rounds' else 1200 if mode=='campaign' else 1000),('client','iceclimbers' if mode=='partners' else 'fox',['--join','127.0.0.1'],1200 if mode=='rounds' else 1000 if mode=='campaign' else 700)]:
            output=(case/(name+'.log')).open('w');files.append(output)
            command=[sys.executable,str(HERE/'play_coop.py'),*role,'--character',character,'--difficulty','impossible' if mode in ('campaign','rounds') else 'none','--network-test',mode,'--port',str(port),'--test-frames',str(frames),'--timeout','150']
            processes.append(subprocess.Popen(command,stdout=output,stderr=subprocess.STDOUT))
            if name=='host':time.sleep(4)
        deadline=time.monotonic()+170
        while any(p.poll() is None for p in processes) and time.monotonic()<deadline:time.sleep(.5)
        logs={}
        for name,p in zip(('host','client'),processes):
            if p.poll() is None:p.kill();p.wait()
            text=(case/(name+'.log')).read_text();match=re.search(r'Logs: (.+)',text)
            game=Path(match[1])/'game.log' if match else None;logs[name]=game.read_text() if game and game.exists() else text
            result[name]={'exit':p.returncode,'log':str(game)}
            result['checks'][name+'_clean_exit']=p.returncode==0 and 'SMB HOST SMOKE COMPLETE' in logs[name]
            result['checks'][name+'_native_peer']='SMB COOP FIGHTER seat=4' in logs[name]
            result['checks'][name+'_no_native_errors']=not any(s in logs[name] for s in ('SMB COOP REMOTE ERROR','Assertion failed','SMB HOST FAIL','PANIC:'))
        if mode=='projectiles':
            result['checks']['remote_article_models']='SMB COOP ARTICLE' in logs['client'] and 'native_model=1' in logs['client']
            result['checks']['articles_transported']='SMB COOP ARTICLES peer=1' in logs['client']
            result['checks']['native_projectile_damage']='SMB NET HIT victim=0' in logs['client']
        elif mode=='combat':
            hits=records(logs['client'],'SMB NET HIT')
            result['checks']['native_hit_and_knockback']=any(row.get('victim')=='0' and number(row,'damage')>0 and number(row,'knockback')>0 and launch(row,('vx','vy','vz')) for row in hits)
            result['checks']['native_shield_block']='SMB NET SHIELD seat=0' in logs['client']
        elif mode=='grab':
            grants={row['id'] for row in records(logs['host'],'SMB COOP GRAB GRANT') if row.get('result')=='0' and row.get('catcher')=='0' and row.get('victim')=='1'}
            presentations=records(logs['client'],'SMB COOP GRAB PRESENT')
            captures={row['id'] for row in presentations if row.get('phase')=='1' and row.get('victim_owned')=='1' and row.get('result')=='0'}
            releases={}
            for row in records(logs['host'],'SMB NET GRAB RELEASE'):
                try:velocity=[float(value) for value in row.get('velocity','').split(',')]
                except ValueError:continue
                if row.get('phase')=='2' and number(row,'percent')>0 and number(row,'hitstun')>0 and len(velocity)==3 and all(math.isfinite(value) for value in velocity) and sum(value*value for value in velocity)>.0001:releases[row['id']]=row
            resumed={row['id'] for row in presentations if row.get('phase')=='2' and row.get('victim_owned')=='1' and row.get('result')=='0'}
            complete=grants&captures&releases.keys()&resumed
            result['checks']['grant']=bool(grants)
            result['checks']['victim_owned_capture']=bool(captures)
            result['checks']['native_throw']=bool(releases)
            result['checks']['victim_owned_release']=bool(resumed)
            result['checks']['same_lease_complete']=bool(complete)
            result['checks']['native_pummel_replicated']=any(row.get('id') in complete and row.get('phase')=='1' and row.get('victim_owned')=='1' and row.get('result')=='0' and number(row,'percent')>0 for row in presentations)
            result['checks']['release_damage_matches']=any(row.get('id') in complete and row.get('phase')=='2' and row.get('victim_owned')=='1' and row.get('result')=='0' and abs(number(row,'percent')-number(releases[row['id']],'percent'))<.05 for row in presentations)
            result['grab_releases']=[releases[key] for key in sorted(complete)]
        elif mode=='campaign':
            host_roles=records(logs['host'],'SMB COOP CAMPAIGN ROLE');client_roles=records(logs['client'],'SMB COOP CAMPAIGN ROLE')
            takeover=re.search(r'^SMB COOP CAMPAIGN ROLE owner=1 local=1 authority=1 level=16 area=1 act=0$',logs['client'],re.M)
            before_takeover=logs['client'][:takeover.start()] if takeover else logs['client']
            result['checks']['one_authority']=any(row.get('owner')=='0' and row.get('local')=='0' and row.get('authority')=='1' and row.get('level')=='16' for row in host_roles) and any(row.get('owner')=='0' and row.get('local')=='1' and row.get('authority')=='0' and row.get('level')=='16' for row in client_roles) and not any(row.get('authority')=='1' for row in records(before_takeover,'SMB COOP CAMPAIGN ROLE'))
            result['checks']['shared_npc_mirror']=any(row.get('owner')=='0' and row.get('active')=='7' and row.get('defeated')=='0' and row.get('count')=='3' for row in records(logs['client'],'SMB COOP CAMPAIGN MIRROR'))
            proxies=records(logs['client'],'SMB COOP FIGHTER')
            result['checks']['shared_roster_identity']=all(any(row.get('seat')==str(seat) and row.get('external')==str(character) for row in proxies) for seat,character in ((1,17),(2,12),(3,5)))
            # Promotion logs its native setup immediately before the ROLE
            # change. Until that explicit promotion, mirrors never own CPUs.
            promoted_at=before_takeover.find('SMB COOP CAMPAIGN PROMOTE BEGIN ')
            mirror_only=before_takeover[:promoted_at] if promoted_at>=0 else before_takeover
            result['checks']['client_never_simulates_campaign']=not any(prefix in mirror_only for prefix in ('SMB NPC SPAWN ','SMB ENCOUNTER WAVE ','SMB ENCOUNTER ENTER '))
            def sonic(row):return row.get('character')=='38' and row.get('costume')=='0' and row.get('level')=='9'
            result['checks']['spawn_requested']=any(sonic(row) for row in records(logs['client'],'SMB COOP SPAWN REQUEST'))
            result['checks']['spawn_received']=any(sonic(row) and row.get('sender')=='1' and row.get('result')=='0' for row in records(logs['host'],'SMB COOP SPAWN ACCEPT'))
            result['checks']['spawn_mirrored']=any(row.get('external')=='38' and row.get('seat') in ('1','2','3') for row in proxies)
            configured=records(logs['host'],'SMB NPC CONFIGURE')
            result['checks']['max_cpu_levels']=all(any(row.get('seat')==str(seat) and row.get('level')=='9' and row.get('impossible')=='1' for row in configured) for seat in range(1,4)) and all(row.get('level')=='9' and row.get('impossible')=='1' for row in configured)
            result['checks']['max_cpu_spawn_levels']=all(row.get('level')=='9' for row in records(logs['host'],'SMB NPC SPAWN'))
            result['checks']['host_leaves_for_battlefield']=any(row.get('global')=='0' and row.get('target')=='9' and row.get('result')=='1' for row in records(logs['host'],'SMB COOP CAMPAIGN DEPART')) and any(row.get('owner')=='0' and row.get('local')=='0' and row.get('authority')=='1' and row.get('level')=='9' for row in host_roles)
            result['checks']['client_becomes_castle_authority']=bool(takeover)
            promotions=records(logs['client'],'SMB COOP CAMPAIGN PROMOTED')
            result['checks']['handoff_retains_three_slots']=any(row.get('owner')=='1' and row.get('level')=='16' and row.get('count')=='3' and row.get('defeated')=='0' for row in promotions)
            resumed=records(logs['client'],'SMB COOP CAMPAIGN RESUMED')
            result['checks']['handoff_retains_manual_roster']=all(any(row.get('seat')==str(seat) and row.get('character')==str(character) and row.get('level')=='9' for row in resumed) for seat,character in ((1,17),(2,12),(3,38)))
        elif mode=='rounds':
            spawned=[row for row in records(logs['host'],'SMB MANUAL ROUND') if 'count' in row]
            result['checks']['two_remote_round_requests']=any(number(row,'serial')==1 for row in spawned) and any(number(row,'serial')==2 for row in spawned)
            result['checks']['manual_rounds_use_stage_roster']=all(row.get('count')=='3' and row.get('automatic')=='0' for row in spawned) and len(spawned)==2
            result['checks']['requester_places_rounds']='SMB COOP ROUND ACCEPT sender=1' in logs['host']
            result['checks']['host_changes_levels']='SMB COOP ROUNDS DEPART global=0' in logs['host']
            result['checks']['client_takes_round_ownership']=bool(re.search(r'SMB COOP CAMPAIGN ROLE owner=1 local=1 authority=1 level=16',logs['client']))
            local=[row for row in records(logs['client'],'SMB MANUAL ROUND') if 'count' in row]
            result['checks']['serial_survives_handoff']=any(number(row,'serial')==3 and row.get('count')=='3' for row in local)
        elif mode=='partners':
            result['checks']['independent_partner']='partner=1' in logs['host']
        if mode in ('combat','projectiles'):
            for name in ('host','client'):
                result['checks'][name+'_remote_audio']=any(row.get('stop')=='0' and row.get('played')=='1' for row in records(logs[name],'SMB NET SOUND'))
        result['passed']=all(result['checks'].values());return result
    finally:
        for p in processes:
            if p.poll() is None:p.kill();p.wait()
        for file in files:file.close()
def main():
    p=argparse.ArgumentParser();p.add_argument('mode',choices=['projectiles','combat','grab','campaign','partners','rounds','all']);args=p.parse_args()
    modes=['projectiles','combat','grab','campaign','partners','rounds'] if args.mode=='all' else [args.mode]
    result={'cases':[run(mode,7800+i) for i,mode in enumerate(modes)]};result['passed']=all(case['passed'] for case in result['cases'])
    target=HERE.parent/'verification'/('coop-features-'+args.mode+'.json');target.parent.mkdir(parents=True,exist_ok=True);target.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2));return 0 if result['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
