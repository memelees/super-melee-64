#!/usr/bin/env python3
"""Build a drag-and-drop Mac installer containing only the source kit."""
from pathlib import Path
import json,os,plistlib,shutil,subprocess,tempfile,time
HERE=Path(__file__).resolve().parent
LAB=HERE.parent
def stage(app):
    release=LAB/'release'
    from build_bootstrap import build as bootstrap
    bootstrap()
    contents=app/'Contents';resources=contents/'Resources'
    (contents/'MacOS').mkdir(parents=True,exist_ok=True);resources.mkdir(exist_ok=True)
    subprocess.run(['xcrun','swiftc','-O','-file-prefix-map',str(HERE.parents[2])+'=SuperMelee64',str(release/'InstallHelper.swift'),'-o',str(contents/'MacOS/InstallSuperMelee64')],check=True)
    kit=resources/'SourceKit/outputs/sm64-melee-lab'
    for name in ('native','branding','release'):
        source=LAB/name;dest=kit/name;dest.mkdir(parents=True,exist_ok=True)
        if name=='native':
            from package_release import SCRIPTS
            files=[source/n for n in SCRIPTS]+[p for p in source.iterdir() if p.suffix in ('.c','.cpp','.inc','.h','.m','.patch') and not p.name.startswith('test_')]
        elif name=='branding':files=[p for p in source.iterdir() if p.suffix in ('.png','.icns','.ico','.rgba')]
        else:files=[p for p in source.iterdir() if p.is_file() and (p.suffix in ('.py','.ps1','.swift','.cpp','.txt','.cmd','.command','.vbs') or p.name=='SmashRoyale-bootstrap.zip')]
        for path in files:shutil.copy2(path,dest/path.name)
    for name in ('README.md','LICENSE','LICENSING.md','SECURITY.md','CONTRIBUTING.md'):
        source=HERE.parents[2]/name
        if source.is_file():shutil.copy2(source,resources/'SourceKit'/name)
    documentation=HERE.parents[2]/'docs'
    if documentation.is_dir():shutil.copytree(documentation,resources/'SourceKit/docs')
    shutil.copy2(LAB/'branding/Super Melee 64.icns',resources/'Super Melee 64.icns')
    (contents/'Info.plist').write_bytes(plistlib.dumps(dict(CFBundleIdentifier='dev.supermelee64.installer',CFBundleExecutable='InstallSuperMelee64',CFBundleVersion='0.6.0',CFBundleShortVersionString='0.6.0',CFBundleName='Install Super Melee 64',CFBundlePackageType='APPL',CFBundleIconFile='Super Melee 64.icns',NSHighResolutionCapable=True,CFBundleDocumentTypes=[dict(CFBundleTypeName='Game images',CFBundleTypeRole='Viewer',CFBundleTypeExtensions=['iso','gcm','z64'])])))
    identity=os.environ.get('SMB_CODESIGN_IDENTITY','-')
    command=['codesign','--force','--sign',identity]
    if identity!='-':command+=['--options','runtime','--timestamp']
    subprocess.run([*command,str(app)],check=True,capture_output=True)
    subprocess.run(['codesign','--verify','--deep','--strict',str(app)],check=True)
    return app

def build():
    release=LAB/'release';target=release/'Install Super Melee 64.app'
    # Build from a clean allowlist so files removed from the source kit cannot
    # linger in an older helper, and a failed compile preserves the old helper.
    with tempfile.TemporaryDirectory(prefix='.installer-build-',dir=release) as folder:
        app=stage(Path(folder)/target.name)
        prior=release/('.previous-installer-'+str(time.time_ns()))
        if target.exists():target.rename(prior)
        try:app.rename(target)
        except OSError:
            if prior.exists():prior.rename(target)
            raise
        if prior.exists():shutil.rmtree(prior)
    print(target)
    return target
if __name__=='__main__':build()
