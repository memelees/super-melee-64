#!/usr/bin/env python3
"""Validate current compiled encounter policy and source kits without gameplay."""
from pathlib import Path, PurePosixPath
from datetime import datetime
import argparse,ctypes,hashlib,importlib.util,json,os,re,shutil,subprocess,sys,zipfile
from package_release import VERSION,package_name,FORBIDDEN
from build_bootstrap import verify as verify_bootstrap
from build_campaign import validate as validate_campaign
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2];LAB=HERE.parent
class Plan(ctypes.Structure):
    _fields_=[('waves',ctypes.c_uint),('size',ctypes.c_uint*3),('roster',(ctypes.c_uint*3)*3),('costume',(ctypes.c_uint*3)*3),('level',(ctypes.c_uint*3)*3)]
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def compiled_policy(work):
    data=json.loads((HERE/'campaign.json').read_text());forms=validate_campaign(data)
    source=(HERE/'sm64_npcs.inc').read_text();a=source.index('typedef struct {unsigned waves');b=source.index('static void smb_encounter_enter',a)
    fixture='#include <stdint.h>\n#include <string.h>\n#include <stdio.h>\nstatic int mode;static int smb_impossible(void){return mode;}\n'+source[a:b]+'\nint main(int argc,char** argv){(void)argv;mode=argc>1;smb_campaign_plan();fwrite(campaign,sizeof campaign,1,stdout);return 0;}\n'
    path=work/'campaign.c';path.write_text(fixture);exe=work/'campaign';cc=shutil.which('cc')
    if not cc:raise RuntimeError('A native C compiler is required for the compiled campaign check')
    subprocess.run([cc,'-I',str(HERE),str(path),'-o',str(exe)],check=True)
    tables=[]
    for impossible in (False,True):
        test=subprocess.run([str(exe)]+(['impossible'] if impossible else []),check=True,capture_output=True)
        assert len(test.stdout)==ctypes.sizeof(Plan)*len(data['stages'])
        table=(Plan*len(data['stages'])).from_buffer_copy(test.stdout);tables.append(table)
        for stage,compiled in zip(data['stages'],table):
            if impossible and stage['stage']==0:
                assert compiled.waves==1 and compiled.size[0]==3 and list(compiled.roster[0])==[17,12,5] and list(compiled.level[0])==[9,9,9];continue
            assert compiled.waves==len(stage['waves'])
            for w,wave in enumerate(stage['waves']):
                assert compiled.size[w]==len(wave)
                for i,enemy in enumerate(wave):
                    assert compiled.roster[w][i]==enemy['external']
                    assert compiled.costume[w][i]==enemy['costume_variant']
                    assert compiled.level[w][i]==(9 if impossible else enemy['level'])
        (work/('campaign-impossible.log' if impossible else 'campaign-default.log')).write_bytes(test.stderr)
    assert tables[0][1].waves==1 and tables[0][1].size[0]==1 and tables[0][1].roster[0][0]==12
    guide=(LAB/'Recommended Playthrough.html').read_text();assert guide.count('type="checkbox"')==120
    magic=int(re.search(r'#define SMB_MAGIC (0x[0-9a-fA-F]+)',(HERE/'bridge_protocol.h').read_text())[1],16)
    abi=magic-0x534d4230
    assert 'SuperMelee64-native'+str(abi) in (HERE/'prepare_coop_host.py').read_text()
    return dict(compiled_campaign_matches_source=True,impossible_policy=True,no_mirror_recommended_pairs=True,forms=len(forms),stages=len(data['stages']),checklist_stars=120,native_abi=abi)

def package_check(target,work,rom,disc):
    archive=LAB/'release/dist'/(package_name(target)+'.zip')
    folder=work/(target+'-extracted')
    if folder.exists():shutil.rmtree(folder)
    with zipfile.ZipFile(archive) as z:
        assert z.testzip() is None
        for info in z.infolist():
            name=PurePosixPath(info.filename)
            assert not name.is_absolute() and '..' not in name.parts and name.parts[0]==package_name(target)
            assert name.suffix.lower() not in FORBIDDEN
            assert not any(part in ('work','Recordings','__pycache__','.git') for part in name.parts)
        z.extractall(folder)
        if os.name!='nt':
            for info in z.infolist():
                if not info.is_dir():(folder/info.filename).chmod((info.external_attr>>16)&0o777)
    kit=folder/package_name(target);manifest=json.loads((kit/'manifest.json').read_text())
    assert manifest['version']==VERSION and manifest['platform']==target
    assert manifest['source_only'] and not manifest['game_assets_included'] and not manifest['game_binaries_included']
    assert manifest['bundled_smash_royale_source'] and manifest['private_repository_access_required'] is False
    packaged={str(p.relative_to(kit)) for p in kit.rglob('*') if p.is_file()}
    assert packaged=={'manifest.json',*manifest['files']}
    for name,digest in manifest['files'].items():
        path=kit/name;assert sha(path)==digest
        if 'outputs/sm64-melee-lab/native/' in name:
            assert sha(HERE/path.name)==digest,'Packaged native source is stale: '+name
        elif 'outputs/sm64-melee-lab/branding/' in name:
            assert sha(LAB/'branding'/path.name)==digest,'Packaged branding is stale: '+name
        elif 'outputs/sm64-melee-lab/release/' in name or ('/' not in name and (LAB/'release'/name).is_file()):
            assert sha(LAB/'release'/path.name)==digest,'Packaged installer source is stale: '+name
        if path.name=='SmashRoyale-bootstrap.zip':
            verify_bootstrap(path)
            assert sha(path)==sha(LAB/'release/SmashRoyale-bootstrap.zip'),'Packaged bootstrap is stale: '+name
    assert (kit/'Recommended Playthrough.html').read_bytes()==(LAB/'Recommended Playthrough.html').read_bytes()
    assert (kit/'outputs/sm64-melee-lab/native/bridge_protocol.h').read_bytes()==(HERE/'bridge_protocol.h').read_bytes()
    bootstrap=kit/'SmashRoyale-bootstrap.zip';source=verify_bootstrap(bootstrap)
    assert manifest['bootstrap']['sha256']==sha(bootstrap) and source['revision']==manifest['bootstrap']['revision']
    checked=subprocess.run([sys.executable,str(kit/'Setup.py'),'--platform',target,'--rom',str(rom),'--melee',str(disc),'--check'],text=True,capture_output=True)
    assert checked.returncode==0,checked.stdout+checked.stderr
    result=json.loads(checked.stdout);assert result['played'] is False and result['downloaded'] is False
    assert result['source_bootstrap']['revision']==source['revision']
    if target=='mac' and sys.platform=='darwin':
        subprocess.run(['codesign','--verify','--deep','--strict',str(kit/'Install Super Melee 64.app')],check=True)
    return dict(platform=target,version=manifest['version'],manifest_verified=True,current_native_source_matches=True,bootstrap_verified=True,extracted_setup_preflight=True,preflight_host=sys.platform,archive_sha256=sha(archive),bytes=archive.stat().st_size)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom',type=Path,default=ROOT/'work/sm64ex-source/baserom.us.z64')
    parser.add_argument('--melee',type=Path,default=Path.home()/'Downloads/Super Smash Bros. Melee (USA) (En,Ja) (Rev 2).iso')
    parser.add_argument('--campaign-only',action='store_true',help='Check the compiled encounter policy without reading game images or packages')
    parser.add_argument('--output',type=Path)
    args=parser.parse_args();work=ROOT/'work/release-validation';work.mkdir(parents=True,exist_ok=True)
    result=dict(version=VERSION,date=datetime.now().astimezone().isoformat(timespec='seconds'),**compiled_policy(work),played=False,full_clean_machine_build_tested=False)
    if not args.campaign_only:
        sys.path.insert(0,str(LAB/'release'))
        spec=importlib.util.spec_from_file_location('setup_release',LAB/'release/Setup.py');setup=importlib.util.module_from_spec(spec);spec.loader.exec_module(setup)
        bogus=work/'wrong-image';bogus.write_bytes(b'not a game')
        try:setup.verify_inputs(bogus,bogus)
        except ValueError:rejected=True
        else:rejected=False
        assert rejected
        identities=setup.verify_inputs(args.rom,args.melee)
        result.update(incorrect_images_rejected=True,correct_images_verified=bool(identities),packages=[package_check(target,work,args.rom.resolve(),args.melee.resolve()) for target in ('mac','windows')])
    else:result['package_validation_performed']=False
    output=args.output or LAB/'verification'/('release-'+VERSION+('-campaign' if args.campaign_only else '')+'.json')
    output.parent.mkdir(parents=True,exist_ok=True);output.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
if __name__=='__main__':main()
