#!/usr/bin/env python3
"""Run serial, isolated game checks and publish a path-free beta acceptance report.

This exercises real engine seams and stage entry, not human mission completion.
Run only after native builds finish; do not compile while these GPU checks run.
"""
from pathlib import Path
from datetime import datetime, timezone
import argparse, hashlib, json, os, re, signal, subprocess, sys, time

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]

FEATURES=[
    ('controller','link','controller',720,['--input-diagnostic']),
    ('camera-follow','link','camera-follow',340,[]),
    ('camera-look','link','camera-look',240,[]),
    ('cannon','link','cannon-range',320,[]),
    ('enemy-throw','link','king-bobomb',360,[]),
    ('grab','link','grab',180,[]),
    ('bowser-tail','link','bowser',210,[]),
    ('shield','link','shield',180,[]),
    ('arrow-world-impact','link','impact',300,[]),
    ('bomb-world-impact','link','bomb-impact',300,[]),
    ('steep-slide','sonic','slope',260,['--slope-steer']),
    ('moving-platform','link','platforms',150,[]),
    ('shell','link','shell-ride',220,[]),
    ('wing-cap','link','wing-flight',240,[]),
    ('swimming','link','swim-controls',700,['--input-diagnostic','--water-diagnostic']),
    ('air-meter','link','air-meter',200,[]),
    ('star-return-mario','mario','star-return',480,[]),
    ('star-return-sonic','sonic','star-return',480,[]),
    ('star-return-climbers','iceclimbers','star-return',480,[]),
    ('respawn','link','respawn',240,[]),
    ('manual-rounds','link','thematic-rounds',650,[]),
    ('kirby-directions','kirby','directions',480,[]),
    ('cpu-swimming','link','swim-cpus',180,[]),
    ('no-fast-fall','link','no-fast-fall',320,[]),
    ('ground-pound','link','ground-pound',240,[]),
    ('powerups','link','powerups',420,[]),
    ('performance','link','basic',900,['--benchmark']),
]

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def build_identity():
    return {name:digest(ROOT/path) for name,path in (
        ('host','work/sm64ex-source/build/us_melee_bridge/sm64.us.f3dex2e'),
        ('fighters','work/melee-embedded/libmelee_embedded.dylib'),
        ('co_op','work/sm64coopdx-melee/build/us_pc/sm64coopdx'),
        ('test_runner','outputs/sm64-melee-lab/native/verify_beta.py'),
        ('single_player_checks','outputs/sm64-melee-lab/native/play_embedded.py'),
        ('network_checks','outputs/sm64-melee-lab/native/verify_coop_features.py'))}

def run_case(command, log, timeout):
    with log.open('w') as stream:
        process=subprocess.Popen(command,cwd=ROOT,stdout=stream,stderr=subprocess.STDOUT,start_new_session=True)
        try:return process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid,signal.SIGTERM)
            try:process.wait(timeout=5)
            except subprocess.TimeoutExpired:os.killpg(process.pid,signal.SIGKILL);process.wait()
            return 124

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--groups',nargs='+',choices=('stages','roster','features','network'),default=['stages','roster','features','network'])
    parser.add_argument('--resume',type=Path,help='Reuse only successful cases from the exact same compiled build')
    args=parser.parse_args()
    identity=build_identity()
    suite=ROOT/'work/beta-acceptance'/str(time.time_ns());suite.mkdir(parents=True)
    private=suite/'result.json';public=HERE.parent/'verification/beta-acceptance.json'
    prior=json.loads(args.resume.read_text()) if args.resume else {}
    reusable={c['name']:c for c in prior.get('cases',[]) if prior.get('build_identity')==identity and c['passed']}
    tasks=[]
    common=['--smoke','--timeout','180']
    if 'stages' in args.groups:
        stages=json.loads((HERE/'campaign.json').read_text())['stages']
        for stage in stages:
            tasks.append((f"stage-{stage['stage']:02d}",'stages',[*common,'--stage',str(stage['stage']),'--select','--difficulty','default','--scenario','stage','--frames','240']))
    if 'roster' in args.groups:
        from native_lineup import BY_NAME
        for name in sorted(BY_NAME):
            tasks.append(('fighter-'+name,'roster',[*common,'--character',name,'--roster-test','--difficulty','none','--cpus','0','--frames','180']))
    if 'features' in args.groups:
        for label,name,scenario,frames,extra in FEATURES:
            tasks.append((label,'features',[*common,'--character',name,'--difficulty','impossible' if label=='performance' else 'none','--cpus','3' if label=='performance' else '0','--scenario',scenario,'--frames',str(frames),*extra]))
    if 'network' in args.groups:tasks.append(('network-all','network',[]))
    records=[]
    def save():
        result=dict(date=datetime.now(timezone.utc).isoformat(timespec='seconds'),
            passed=len(records)==len(tasks) and all(c['passed'] for c in records),
            complete=len(records)==len(tasks),requested_groups=args.groups,
            total_cases=len(tasks),completed_cases=len(records),build_identity=identity,cases=records,
            full_120_star_playthrough=False,real_windows_test=False,internet_test=False,
            network_clients_tested=2 if any(c['group']=='network' and c['passed'] for c in records) else 0)
        private.write_text(json.dumps(result,indent=2)+'\n')
        sanitized={k:v for k,v in result.items() if k!='build_identity'}
        sanitized['cases']=[{k:v for k,v in c.items() if k not in ('log','result_path')} for c in records]
        sanitized['scope']='Real engine stage-entry, roster and integration checks with isolated saves; two-client loopback network checks. No human mission-clear, Windows hardware, Internet/NAT or sixteen-person claim.'
        public.write_text(json.dumps(sanitized,indent=2)+'\n')
    for label,group,options in tasks:
        if build_identity()!=identity:raise RuntimeError('Compiled build changed during acceptance tests')
        if label in reusable:
            records.append(reusable[label]);save();print(label+': reused PASS',flush=True);continue
        log=suite/(label+'.log');started=time.monotonic()
        command=[sys.executable,str(HERE/('verify_coop_features.py' if group=='network' else 'play_embedded.py'))]
        command+=['all'] if group=='network' else options
        code=run_case(command,log,1500 if group=='network' else 210)
        text=log.read_text()
        if group=='network':
            result_path=HERE.parent/'verification/coop-features-all.json'
            details=json.loads(result_path.read_text()) if result_path.exists() else {}
            checks={case['mode']:case['passed'] for case in details.get('cases',[])}
            checks['rounds_included']='rounds' in checks
        else:
            match=re.search(r'^Logs: (.+)$',text,re.M)
            result_path=Path(match[1])/'result.json' if match else None
            details=json.loads(result_path.read_text()) if result_path and result_path.exists() else {}
            checks=details.get('checks',{})
        item=dict(name=label,group=group,passed=code==0 and details.get('passed') is True and bool(checks) and all(checks.values()),
            exit_code=code,seconds=round(time.monotonic()-started,2),failed_checks=[k for k,v in checks.items() if not v],
            log=str(log),result_path=str(result_path) if result_path else None)
        if label=='performance' and result_path:
            game=(result_path.parent/'game.log').read_text()
            rows=[dict((k,float(v)) for k,v in re.findall(r'(fps|render_median_ms|render_p95_ms|frame_p95_ms)=([\d.]+)',line)) for line in game.splitlines() if line.startswith('SMB PERFORMANCE ')]
            item['performance']=rows
            item['performance_target_fps']=30
            item['performance_minimum_fps']=28.5
            item['performance_maximum_frame_p95_ms']=50
            measured=bool(rows) and all(all(key in row for key in ('fps','frame_p95_ms')) for row in rows)
            steady=measured and all(row['fps']>=28.5 and row['frame_p95_ms']<=50 for row in rows)
            if not measured:item['failed_checks'].append('performance_measurements')
            elif not steady:item['failed_checks'].append('steady_30fps_with_three_cpu9')
            item['passed']=item['passed'] and measured and steady
        records.append(item);save();print(label+': '+('PASS' if item['passed'] else 'FAIL '+str(item['failed_checks'])),flush=True)
    print(private,flush=True)
    return int(not all(c['passed'] for c in records))

if __name__=='__main__':raise SystemExit(main())
