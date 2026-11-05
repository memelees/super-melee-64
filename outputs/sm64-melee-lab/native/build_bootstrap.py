#!/usr/bin/env python3
"""Package the pinned, asset-free Smash Royale build adapter source."""
from pathlib import Path, PurePosixPath
import argparse, ast, hashlib, json, os, shutil, stat, subprocess, tempfile, zipfile
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
PIN='super-melee64-adapter-v1'
ARCHIVE=HERE.parent/'release/SmashRoyale-bootstrap.zip'
MANIFEST='smash-royale-bootstrap.json'
ENTRY_MODULES=('native_mac','native_windows','prepare_native_inputs','native_lineup',
    'extract_n64_arenas','n64_display_mesh','n64_materials','compile_arena_visuals',
    'sunshine_compile','sunshine_navigation','sunshine_occlusion')
SOURCE_SUFFIXES={'.c','.cpp','.h','.inc','.json','.patch','.manifest','.md'}
# Apply only these recorded installer-owned configuration changes; the ZIP
# itself remains the exact pinned source, and other local edits are preserved.
LOCAL_OVERRIDES={
    'tools/melee3d/native_compatibility.py':[(
        'DISC = Path.home() / "Downloads/Super Smash Bros. Melee (USA) (En,Ja) (Rev 2).iso"',
        'DISC = Path(os.environ["SM64_MELEE_DISC"])')],
    'tools/melee3d/native_reference.py':[(
        'from install_canonical import DEFAULT_DISC,MARKER,verify_files',
        'from install_canonical import MARKER,verify_files\nDEFAULT_DISC=ROOT/"build/local-reference-source"'),(
        "    from player_install import DEFAULT\n",
        "    # Use the installer-owned, verified Akaneia extraction.\n"),(
        "    candidates=(DEFAULT/'akaneia/files/GrCs.dat',ROOT/'build/akaneia/disc/files/GrCs.dat')",
        "    candidates=(ROOT/'build/akaneia/disc/files/GrCs.dat',)")],
}

def configure_local_sources(destination):
    destination=Path(destination)
    for name,replacements in LOCAL_OVERRIDES.items():
        path=destination/name;source=path.read_text();original=source
        for before,after in replacements:
            if before not in source and after not in source:raise RuntimeError('Unexpected installer source configuration: '+name)
            source=source.replace(before,after)
        if source!=original:path.write_text(source)


def sha(data):return hashlib.sha256(data).hexdigest()
def source_path(name):
    p=PurePosixPath(name)
    if p.is_absolute() or '..' in p.parts or '\\' in name:return False
    return ((len(p.parts)==3 and p.parts[:2]==('tools','melee3d') and p.suffix=='.py') or
        (len(p.parts)>=3 and p.parts[:2]==('native','mac') and p.suffix in SOURCE_SUFFIXES) or
        name in ('BOOTSTRAP-NOTICE.txt','COPYING-port.txt','UPSTREAM-LICENSING.txt','config/GALE01/symbols.txt'))

def verify(archive=ARCHIVE):
    archive=Path(archive)
    with zipfile.ZipFile(archive) as z:
        names=z.namelist()
        if len(names)!=len(set(names)) or MANIFEST not in names:raise ValueError('Invalid source bootstrap manifest')
        info=json.loads(z.read(MANIFEST))
        if info.get('revision')!=PIN or info.get('source_only') is not True:raise ValueError('Unexpected source bootstrap revision')
        files=info.get('files',{})
        if set(names)!={MANIFEST,*files}:raise ValueError('Unexpected source bootstrap member')
        for name,digest in files.items():
            if not source_path(name):raise ValueError('Non-source bootstrap member: '+name)
            entry=z.getinfo(name)
            if stat.S_ISLNK(entry.external_attr>>16) or entry.file_size>8*1024*1024:raise ValueError('Invalid source bootstrap entry: '+name)
            data=z.read(name)
            if sha(data)!=digest or b'\x00' in data:raise ValueError('Changed or binary source bootstrap member: '+name)
        if not all('tools/melee3d/'+name+'.py' in files for name in ENTRY_MODULES):raise ValueError('Incomplete build adapter bootstrap')
    return info

def extract(archive,destination):
    """Verify before writing, preserve existing source, and install atomically."""
    archive=Path(archive);destination=Path(destination);info=verify(archive)
    if destination.exists():
        marker=destination/MANIFEST
        if not marker.is_file() or json.loads(marker.read_text()).get('revision')!=PIN:raise RuntimeError('Preserving an unrecognized existing build adapter: '+str(destination))
        for name,digest in info['files'].items():
            path=destination/name
            if not path.is_file():raise RuntimeError('Existing build adapter is incomplete: '+str(path))
            data=path.read_bytes()
            for before,after in reversed(LOCAL_OVERRIDES.get(name,[])):data=data.replace(after.encode(),before.encode())
            if sha(data)!=digest:raise RuntimeError('Preserving changed build adapter source: '+str(path))
        return PIN
    destination.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.smash-source-',dir=destination.parent) as temp:
        staged=Path(temp)/'adapter';staged.mkdir()
        with zipfile.ZipFile(archive) as z:
            for name in [*info['files'],MANIFEST]:
                target=staged/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(z.read(name))
        staged.rename(destination)
    return PIN

def build(source=None,force=False):
    if ARCHIVE.exists() and not force:
        verify();return ARCHIVE
    source=Path(source or os.environ.get('SMB_UPSTREAM_ROOT',ROOT/'work/deps/smash-royale'))
    revision=os.environ.get('SMB_ADAPTER_SOURCE_REVISION')
    if not revision or len(revision)!=40 or any(c not in '0123456789abcdef' for c in revision):
        raise ValueError('Rebuilding the bundled adapter requires SMB_ADAPTER_SOURCE_REVISION, an exact local source commit. Normal setup uses the verified included bundle.')
    def git(*args):return subprocess.check_output(['git','-C',str(source),*args])
    git('cat-file','-e',revision+'^{commit}')
    paths=git('ls-tree','-r','--name-only',revision,'--','native/mac','tools/melee3d').decode().splitlines()
    modules={Path(p).stem:p for p in paths if len(Path(p).parts)==3 and p.endswith('.py')}
    selected={};pending=list(ENTRY_MODULES)
    while pending:
        name=pending.pop()
        if name in selected:continue
        data=git('show',revision+':'+modules[name]);selected[name]=data
        for node in ast.walk(ast.parse(data)):
            imported=[a.name for a in node.names] if isinstance(node,ast.Import) else [node.module] if isinstance(node,ast.ImportFrom) and node.module else []
            pending.extend(n.split('.')[0] for n in imported if n.split('.')[0] in modules and n.split('.')[0] not in selected)
    files={modules[name]:data for name,data in selected.items()}
    files.update({p:git('show',revision+':'+p) for p in paths if p.startswith('native/mac/') and source_path(p)})
    files['config/GALE01/symbols.txt']=git('show',revision+':config/GALE01/symbols.txt')
    release=HERE.parent/'release'
    for name in ('COPYING-port.txt','UPSTREAM-LICENSING.txt'):files[name]=(release/name).read_bytes()
    files['BOOTSTRAP-NOTICE.txt']=(
        'Super Melee 64 native build adapter — Super Melee 64 contributors\n'
        'Verified source bundle: '+PIN+'\n'
        'Only build tools, symbol metadata and original native adapter source/patches are included.\n'
        'No decompiled engine tree, original game assets, disc images or binaries.\n'
        'The native bridge adapters identify GPL-3.0-or-later in their source.\n'
        'Keep all source notices and the accompanying GPL/upstream license texts.\n'
        'Public engine dependencies and official mod patches are fetched at setup.\n').encode()
    for name,data in files.items():
        if not source_path(name) or b'\x00' in data or b'/Users/' in data:raise ValueError('Unexpected or personal bootstrap content: '+name)
    manifest=dict(revision=PIN,source_only=True,game_assets_included=False,engine_source_included=False,
        python_modules=sorted(selected),files={n:sha(d) for n,d in sorted(files.items())})
    files[MANIFEST]=(json.dumps(manifest,indent=2,sort_keys=True)+'\n').encode()
    ARCHIVE.parent.mkdir(parents=True,exist_ok=True)
    temporary=ARCHIVE.with_suffix('.zip.part')
    with zipfile.ZipFile(temporary,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
        for name,data in sorted(files.items()):
            entry=zipfile.ZipInfo(name,date_time=(2026,10,26,0,0,0));entry.compress_type=zipfile.ZIP_DEFLATED
            entry.create_system=3;entry.external_attr=(stat.S_IFREG|0o644)<<16;z.writestr(entry,data)
    verify(temporary);temporary.replace(ARCHIVE)
    print(json.dumps(dict(archive=str(ARCHIVE),sha256=sha(ARCHIVE.read_bytes()),files=len(files),bytes=ARCHIVE.stat().st_size),indent=2))
    return ARCHIVE
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--source',type=Path);parser.add_argument('--force',action='store_true');args=parser.parse_args();build(args.source,args.force)
