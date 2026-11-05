#!/usr/bin/env python3
"""Resume pinned official patch downloads before the bundled local extraction.

Only patches and public source are downloaded. Original game images stay local.
Archives are SHA-256 pinned; the adapter also verifies each resulting image.
"""
from pathlib import Path
import argparse,hashlib,subprocess,sys,time
from setup_support import checkout_pinned

PATCHES=(
    ('akaneia','https://github.com/akaneia/akaneia-build/releases/download/1.0.1/Akaneia.Builder.1.0.1.7z',
     '7d46ce47b27272a369150cb105f4cb3b046b2e7790d233595ff4537ef442919f',246177787),
    ('reference','https://github.com/walz0/Melee-3D/releases/download/v1.0/Melee3D.v1.0.zip',
     'ad34e72e1ae66c68690a329ff1aa04af5bf82c8c8af1d1a26ba693e290d9cff4',1446550))

def sha(path):
    digest=hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''):digest.update(block)
    return digest.hexdigest()

def download_pinned(url, destination, expected, expected_size, run=None):
    destination=Path(destination)
    if not url.startswith('https://github.com/'):raise ValueError('Unexpected patch source')
    if destination.exists():
        if destination.stat().st_size!=expected_size or sha(destination)!=expected:raise RuntimeError('Preserving a changed existing patch archive: '+str(destination))
        print('Verified cached patch: '+destination.name,flush=True);return
    destination.parent.mkdir(parents=True,exist_ok=True);partial=destination.with_name(destination.name+'.partial')
    def reject():
        rejected=partial.with_name(partial.name+'.rejected-'+str(time.time_ns()));partial.rename(rejected)
        return rejected
    if partial.exists() and partial.stat().st_size>=expected_size:
        if partial.stat().st_size==expected_size and sha(partial)==expected:partial.replace(destination);return
        reject()
    command=['curl','--fail','--location','--proto','=https','--proto-redir','=https','--retry','3','--retry-delay','1','--connect-timeout','20','--max-filesize',str(expected_size),'--continue-at','-','--output',str(partial),url]
    def fetch():
        if run:run(command)
        else:subprocess.run(command,check=True)
    try:fetch()
    except subprocess.CalledProcessError as error:
        if error.returncode!=33 or not partial.exists():raise
        # Some HTTPS servers refuse byte ranges; retry from zero once while
        # keeping the earlier partial for inspection.
        reject();fetch()
    if partial.stat().st_size!=expected_size or sha(partial)!=expected:
        rejected=reject()
        raise RuntimeError('Patch archive did not match the pinned SHA-256. It was kept for inspection; retry setup to download again: '+str(rejected))
    partial.replace(destination);print('Verified downloaded patch: '+destination.name,flush=True)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adapter',required=True,type=Path);parser.add_argument('--melee',required=True,type=Path)
    args=parser.parse_args();adapter=args.adapter.resolve()
    sys.path.insert(0,str(adapter/'tools/melee3d'))
    import prepare_native_inputs as inputs
    from native_compatibility import ROOT,WORK,SOURCES
    destinations={'akaneia':ROOT/'build/akaneia/Akaneia.Builder.1.0.1.7z','reference':ROOT/'build/references/Melee3D.v1.0.zip'}
    for name,url,expected,size in PATCHES:download_pinned(url,destinations[name],expected,size)
    # Retain all bundled image, payload and executable checks; only network and
    # checkout preparation are handled by this resumable installer wrapper.
    sys.argv=[str(adapter/'tools/melee3d/prepare_native_inputs.py'),'--melee',str(args.melee.resolve())]
    inputs.main()
    def run(command):subprocess.run([str(x) for x in command],check=True)
    for name in ('melee-pc','gd-melee'):
        repository,_,revision=SOURCES[name]
        checkout_pinned('https://github.com/'+repository+'.git',revision,WORK/name,run)
    print('Verified local game assets and pinned native source dependencies are ready.',flush=True)

if __name__=='__main__':main()
