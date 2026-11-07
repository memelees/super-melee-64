#!/usr/bin/env python3
"""Build a drag-and-drop Mac installer containing only the source kit."""
from pathlib import Path
import json,os,plistlib,shutil,struct,subprocess,tempfile,time
HERE=Path(__file__).resolve().parent
LAB=HERE.parent
MINIMUM_MACOS='15.0'
SWIFT_TARGET='arm64-apple-macosx'+MINIMUM_MACOS

def verify_binary(path):
    """Check the actual executable, not just the bundle's advertised minimum."""
    data=Path(path).read_bytes()
    if len(data)<32 or data[:4]!=b'\xcf\xfa\xed\xfe':
        raise ValueError('Installer must be a 64-bit Apple Silicon Mach-O executable')
    _,cpu,_,_,count,size,_,_=struct.unpack_from('<8I',data)
    if cpu!=0x100000c or size>len(data)-32:
        raise ValueError('Installer must target Apple Silicon with valid load commands')
    end=32+size;offset=32;versions=[]
    for _ in range(count):
        if offset+8>end:raise ValueError('Truncated installer load command')
        command,length=struct.unpack_from('<2I',data,offset)
        if length<8 or offset+length>end:raise ValueError('Invalid installer load command')
        if command==0x32:
            if length<24:raise ValueError('Invalid installer build-version command')
            platform,minimum=struct.unpack_from('<2I',data,offset+8)
            if platform!=1:raise ValueError('Installer must target macOS')
            versions.append(minimum)
        elif command==0x24:
            if length<16:raise ValueError('Invalid installer minimum-version command')
            versions.append(struct.unpack_from('<I',data,offset+8)[0])
        offset+=length
    major,minor=map(int,MINIMUM_MACOS.split('.'));expected=(major<<16)|(minor<<8)
    if offset!=end or versions!=[expected]:
        raise ValueError('Installer executable must require macOS '+MINIMUM_MACOS+' exactly; rebuild with the explicit deployment target')
    return dict(architecture='arm64',minimum_macos=MINIMUM_MACOS)

def compile_helper(source,output):
    # A recent SDK is fine; its default deployment target must never leak into
    # releases. Override inherited shell settings as well as the compiler flag.
    environment=dict(os.environ,MACOSX_DEPLOYMENT_TARGET=MINIMUM_MACOS)
    subprocess.run(['xcrun','swiftc','-O','-target',SWIFT_TARGET,'-file-prefix-map',str(HERE.parents[2])+'=SuperMelee64',str(source),'-o',str(output)],env=environment,check=True)
    return verify_binary(output)

def stage(app):
    release=LAB/'release'
    from build_bootstrap import build as bootstrap
    bootstrap()
    contents=app/'Contents';resources=contents/'Resources'
    (contents/'MacOS').mkdir(parents=True,exist_ok=True);resources.mkdir(exist_ok=True)
    compile_helper(release/'InstallHelper.swift',contents/'MacOS/InstallSuperMelee64')
    kit=resources/'SourceKit/outputs/sm64-melee-lab'
    for name in ('native','branding','release'):
        source=LAB/name;dest=kit/name;dest.mkdir(parents=True,exist_ok=True)
        if name=='native':
            from package_release import SCRIPTS, VERSION
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
    (contents/'Info.plist').write_bytes(plistlib.dumps(dict(CFBundleIdentifier='dev.supermelee64.installer',CFBundleExecutable='InstallSuperMelee64',CFBundleVersion=VERSION.replace('-beta.','b'),CFBundleShortVersionString=VERSION.split('-')[0],SMBInstallerVersion=VERSION,CFBundleName='Install Super Melee 64',CFBundlePackageType='APPL',CFBundleIconFile='Super Melee 64.icns',LSMinimumSystemVersion=MINIMUM_MACOS,NSHighResolutionCapable=True,CFBundleDocumentTypes=[dict(CFBundleTypeName='Game images',CFBundleTypeRole='Viewer',CFBundleTypeExtensions=['iso','gcm','z64'])])))
    # Finder/cloud-backed build folders can add metadata forbidden by codesign.
    # Remove only those two attributes from the newly staged bundle (Apple QA1940).
    for attribute in ('com.apple.FinderInfo','com.apple.ResourceFork'):
        subprocess.run(['/usr/bin/xattr','-r','-d',attribute,str(app)],check=True)
    identity=os.environ.get('SMB_CODESIGN_IDENTITY','-')
    command=['codesign','--force','--sign',identity]
    if identity!='-':command+=['--options','runtime','--timestamp']
    subprocess.run([*command,str(app)],check=True)
    # File providers may attach Finder metadata while codesign is running.
    for attribute in ('com.apple.FinderInfo','com.apple.ResourceFork'):
        subprocess.run(['/usr/bin/xattr','-r','-d',attribute,str(app)],check=True)
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
