#!/usr/bin/env python3
"""Build source-only release candidates from an explicit file allowlist."""
from pathlib import Path
import hashlib,json,shutil,stat,zipfile
HERE=Path(__file__).resolve().parent;LAB=HERE.parent;RELEASE=LAB/'release'
VERSION='0.6.0-beta.2'
def package_name(target):return 'SuperMelee64-'+VERSION+'-'+target+('-experimental' if target=='windows' else '')+'-source-kit'
SCRIPTS=('build_adventure.py','build_embedded.py','build_compositor.py','build_selection_art.py','build_branding.py','build_mac_app.py','build_campaign.py','build_host.py','prepare_sm64_host.py','compile_castle.py','refresh_melee.py','relink_embedded.py','play_embedded.py','verify_stage_starts.py','choose_character.py','native_lineup.py','session_lock.py','campaign.json','package_release.py','build_coop.py','prepare_coop_host.py','play_coop.py','verify_coop.py','build_install_helper.py','build_bootstrap.py','verify_coop_features.py','verify_coop_campaign.py','verify_air.py','verify_rounds.py','coop_network_safety.py','verify_network_safety.py','verify_repository.py','verify_bootstrap.py','verify_release.py','verify_beta.py','export_public_source.py','update_download_links.py','title_lettering.py','verify_npc_placement.py')
FORBIDDEN={'.iso','.gcm','.z64','.n64','.v64','.rvz','.dat','.usd','.dol','.smm','.dll','.dylib','.exe','.o','.obj','.a','.lib','.mp4','.bin'}
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def build():
    from build_campaign import build as campaign
    from build_branding import build as branding
    from build_bootstrap import build as bootstrap, verify as verify_bootstrap
    campaign();branding();bootstrap()
    sources=[HERE/name for name in SCRIPTS]+[p for p in HERE.iterdir() if p.suffix in ('.c','.cpp','.inc','.h','.m','.patch') and not p.name.startswith('test_')]
    # These contain no game data; the CSS atlas and preview caches stay in work/.
    brand_names=('Super Melee 64.png','Super Melee 64 Rendered.png','Super Melee 64.icns','Super Melee 64.ico','Super Melee 64 Icon.png','logo.rgba')
    docs=('SmashRoyale-bootstrap.zip','Setup.py','setup_support.py','mac_toolchain.py','prepare_installer_inputs.py','installer_tests.py','installer_windows_support.ps1','installer_tests.ps1','WindowsLauncher.cpp','SetupWindows.ps1','InstallHelper.swift','Install Helper.ps1','START HERE.txt','THIRD-PARTY.txt','UPSTREAM-LICENSING.txt','COPYING-port.txt')
    dist=RELEASE/'dist';dist.mkdir(exist_ok=True);artifacts=[]
    for target in ('mac','windows'):
        name=package_name(target);stage=dist/name
        if stage.exists():shutil.rmtree(stage)
        native=stage/'outputs/sm64-melee-lab/native';native.mkdir(parents=True)
        for path in sources:shutil.copy2(path,native/path.name)
        brand=stage/'outputs/sm64-melee-lab/branding';brand.mkdir()
        for filename in brand_names:shutil.copy2(LAB/'branding'/filename,brand/filename)
        for filename in docs:shutil.copy2(RELEASE/filename,stage/filename)
        source_release=stage/'outputs/sm64-melee-lab/release';source_release.mkdir()
        for filename in docs:shutil.copy2(RELEASE/filename,source_release/filename)
        for filename in ('README.md','LICENSE','LICENSING.md','SECURITY.md','CONTRIBUTING.md'):
            path=HERE.parents[2]/filename
            if path.is_file():shutil.copy2(path,stage/filename)
        documentation=HERE.parents[2]/'docs'
        if documentation.is_dir():shutil.copytree(documentation,stage/'docs')
        launchers=('Setup Mac.command','Play Super Melee 64.command') if target=='mac' else ('Setup Windows.cmd','Play Super Melee 64.cmd','Play Super Melee 64.vbs')
        for filename in launchers:
            shutil.copy2(RELEASE/filename,stage/filename)
            shutil.copy2(RELEASE/filename,source_release/filename)
        if target=='mac':
            from build_install_helper import build as helper
            shutil.copytree(helper(),stage/'Install Super Melee 64.app')
        shutil.copy2(LAB/'Recommended Playthrough.html',stage/'Recommended Playthrough.html')
        manifest=dict(version=VERSION,platform=target,source_only=True,game_assets_included=False,game_binaries_included=False,
            clean_machine_verified=False,full_playthrough_performed=False,
            release_status='experimental beta' if target=='windows' else 'beta',real_windows_gameplay_verified=False,
            bundled_smash_royale_source=True,private_repository_access_required=False,
            bootstrap=dict(sha256=sha(RELEASE/'SmashRoyale-bootstrap.zip'),revision=verify_bootstrap()['revision'],files=len(verify_bootstrap()['files'])),
            pins={'melee_adapter':verify_bootstrap()['revision'],'sm64ex':'d7ca2c04364a6dd0dac58b47151e04e26887e6f0','melee_pc':'0cc24fee6b8e7dd7a9233daa126829d374fe1f40','sm64coopdx':'8cd6e5977d9f920d51ca71f2c61801d019ed79c6'},
            files={str(p.relative_to(stage)):sha(p) for p in sorted(stage.rglob('*')) if p.is_file()})
        (stage/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
        files=[p for p in stage.rglob('*') if p.is_file()]
        for path in files:
            assert path.suffix.lower() not in FORBIDDEN,path
            assert not any(part in ('work','Recordings','__pycache__','.git') for part in path.relative_to(stage).parts),path
            if path.name=='SmashRoyale-bootstrap.zip':verify_bootstrap(path)
            if path.suffix in ('.py','.c','.cpp','.h','.inc','.m','.txt','.json','.html','.patch'):
                assert str(Path.home()) not in path.read_text(),path
        archive=dist/(name+'.zip')
        with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
            for path in sorted(files):
                info=zipfile.ZipInfo(str(Path(name)/path.relative_to(stage)),date_time=(2026,10,26,0,0,0));info.compress_type=zipfile.ZIP_DEFLATED
                info.create_system=3;info.external_attr=(stat.S_IFREG|(0o755 if path.suffix=='.command' or path.stat().st_mode&0o111 else 0o644))<<16
                z.writestr(info,path.read_bytes())
        with zipfile.ZipFile(archive) as z:assert z.testzip() is None
        artifacts.append(dict(platform=target,path=str(archive),sha256=sha(archive),bytes=archive.stat().st_size,files=len(files),asset_audit=True))
    (dist/'SHA256SUMS.txt').write_text(''.join(x['sha256']+'  '+Path(x['path']).name+'\n' for x in artifacts))
    (dist/'release-audit.json').write_text(json.dumps(artifacts,indent=2)+'\n');print(json.dumps(artifacts,indent=2));return artifacts
if __name__=='__main__':build()
