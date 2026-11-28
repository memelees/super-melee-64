"""Private, pinned Windows tools: no administrator, global PATH changes or WSL."""
from pathlib import Path, PurePosixPath
import hashlib
import json
import os
import re
import shutil
import stat
import subprocess
import tempfile
import urllib.request
import zipfile

PACKAGES = {
    'gcc': dict(name='winlibs-x86_64-posix-seh-gcc-16.2.0-mingw-w64ucrt-14.0.0-r3.7z',
        url='https://github.com/brechtsanders/winlibs_mingw/releases/download/16.2.0posix-23.1.3-14.0.0-ucrt-r3/winlibs-x86_64-posix-seh-gcc-16.2.0-mingw-w64ucrt-14.0.0-r3.7z',
        sha256='db014a73b71907c6c778fd1c938056cddcae9023f89f8dfff7783d9e45baf975', bytes=111718557,
        sentinel='mingw64/bin/gcc.exe'),
    'git': dict(name='MinGit-2.56.1-busybox-64-bit.zip',
        url='https://github.com/git-for-windows/git/releases/download/v2.56.1.windows.1/MinGit-2.56.1-busybox-64-bit.zip',
        sha256='baa40c1b8d5897826f47c2b819156553b89833bc52cb42cdfca7d1f0faeed34a', bytes=35330661,
        sentinel='cmd/git.exe'),
    'cmake': dict(name='cmake-3.31.8-windows-x86_64.zip',
        url='https://github.com/Kitware/CMake/releases/download/v3.31.8/cmake-3.31.8-windows-x86_64.zip',
        sha256='81aa9964dbabd71fe02e7ec50472fd3ad56138c49944515ece9001efbff8d719', bytes=46709267,
        sentinel='cmake-3.31.8-windows-x86_64/bin/cmake.exe'),
    'ninja': dict(name='ninja-win.zip',
        url='https://github.com/ninja-build/ninja/releases/download/v1.13.1/ninja-win.zip',
        sha256='26a40fa8595694dec2fad4911e62d29e10525d2133c9a4230b66397774ae25bf', bytes=289808,
        sentinel='ninja.exe'),
}


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def fetch(package, cache, check_cancel=lambda: None):
    target = Path(cache) / package['name']
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.exists():
        if target.stat().st_size != package['bytes'] or sha(target) != package['sha256']:
            raise RuntimeError('A cached Windows tool failed verification: ' + target.name)
        return target
    temporary = target.with_name(target.name + '.partial')
    check_cancel()
    # A partial transfer is never used as an executable or archive.
    request = urllib.request.Request(package['url'], headers={'User-Agent': 'SuperMelee64-Setup'})
    with urllib.request.urlopen(request, timeout=90) as source, temporary.open('wb') as output:
        if not source.url.startswith('https://'):
            raise RuntimeError('A Windows tool download redirected away from HTTPS.')
        size = 0
        while block := source.read(1024 * 1024):
            check_cancel()
            size += len(block)
            if size > package['bytes']:
                raise RuntimeError('A Windows tool download exceeded its pinned size.')
            output.write(block)
    if temporary.stat().st_size != package['bytes'] or sha(temporary) != package['sha256']:
        raise RuntimeError('A Windows tool download failed its checksum: ' + target.name)
    temporary.replace(target)
    return target


def safe_member(name):
    path = PurePosixPath(name.rstrip('/'))
    if not name or path.is_absolute() or '\\' in name or ':' in name or '..' in path.parts:
        raise ValueError('Unsafe tool archive member')
    if any(part.rstrip(' .') != part or not part for part in path.parts):
        raise ValueError('Ambiguous Windows archive member')
    devices = {'CON', 'PRN', 'AUX', 'NUL', *('COM' + str(i) for i in range(1, 10)), *('LPT' + str(i) for i in range(1, 10))}
    if any(part.split('.')[0].upper() in devices for part in path.parts):
        raise ValueError('Reserved Windows archive member')
    return path


def listing_member(name):
    # 7-Zip prints members with the platform separator. Validate the same
    # archive-relative path that the extractor will write, so a drive letter,
    # rooted path or ".." is still refused after normalisation.
    return safe_member(name.replace('\\', '/'))


def unzip(archive, target):
    with zipfile.ZipFile(archive) as source:
        if sum(item.file_size for item in source.infolist()) > 3 * 1024**3:
            raise ValueError('Oversized Windows tool archive')
        for item in source.infolist():
            safe_member(item.filename)
            if stat.S_ISLNK(item.external_attr >> 16):
                raise ValueError('Linked Windows tool archive member')
        source.extractall(target)


def unpack(name, archive, tools, sevenzip, run=subprocess.run):
    package = PACKAGES[name]
    destination = Path(tools) / name
    marker = destination / '.supermelee-tool.json'
    if destination.exists():
        try:
            metadata = json.loads(marker.read_text())
            valid = (metadata['archive_sha256'] == package['sha256'] and
                     sha(destination / package['sentinel']) == metadata['sentinel_sha256'])
        except (OSError, ValueError, KeyError):
            valid = False
        if not valid:
            raise RuntimeError('An existing private Windows tool is incomplete or changed: ' + name)
        return destination
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.' + name + '-', dir=destination.parent) as folder:
        staged = Path(folder) / 'tool'
        staged.mkdir()
        if Path(archive).suffix == '.zip':
            unzip(archive, staged)
        else:
            listing = subprocess.run([str(sevenzip), 'l', '-slt', str(archive)],
                                     capture_output=True, text=True, check=True).stdout
            for record in listing.split('----------\n')[-1].split('\n\n'):
                fields = dict(line.split(' = ', 1) for line in record.splitlines() if ' = ' in line)
                if 'Path' in fields:
                    listing_member(fields['Path'])
                if any(key in fields for key in ('Symbolic Link', 'Hard Link', 'Reparse')):
                    raise ValueError('Linked Windows tool archive member')
            run([str(sevenzip), 'x', '-y', '-o' + str(staged), str(archive)], check=True)
        sentinel = staged / package['sentinel']
        if not sentinel.is_file():
            raise RuntimeError('The extracted Windows tool is incomplete: ' + name)
        (staged / marker.name).write_text(json.dumps(dict(archive_sha256=package['sha256'],
            sentinel_sha256=sha(sentinel))) + '\n')
        staged.rename(destination)
    return destination


def executable_alias(source, destination):
    source, destination = Path(source), Path(destination)
    if destination.exists():
        if sha(destination) != sha(source):
            raise RuntimeError('A private tool alias changed: ' + destination.name)
    else:
        try:os.link(source,destination)
        except OSError:shutil.copy2(source,destination)


def environment(tools, bundled_bin, python):
    tools, bundled_bin, python = Path(tools), Path(bundled_bin), Path(python)
    compiler = tools / 'gcc/mingw64/bin'
    aliases = {'cc.exe': 'gcc.exe', 'c++.exe': 'g++.exe', 'gmake.exe': 'mingw32-make.exe', 'make.exe': 'mingw32-make.exe'}
    for alias, source in aliases.items():
        executable_alias(compiler / source, compiler / alias)
    for tool in ('gcc', 'g++', 'cpp', 'ar', 'as', 'objcopy', 'objdump', 'windres'):
        executable_alias(compiler / (tool + '.exe'), compiler / ('x86_64-w64-mingw32-' + tool + '.exe'))
    shell_bin = tools / 'git/ucrt64/bin'
    for tool in ('sh', 'patch', 'which', 'sed', 'grep', 'head', 'cut', 'cat', 'find',
                 'mkdir', 'rm', 'cp', 'mv', 'touch', 'sort', 'tr', 'hexdump', 'uname', 'printf'):
        executable_alias(shell_bin / 'busybox.exe', shell_bin / (tool + '.exe'))
    # Native GNU Make can launch a simple command directly rather than through
    # sh. Even shell builtins such as `true` therefore need executable aliases.
    applets=subprocess.check_output([str(shell_bin/'busybox.exe'),'--list'],text=True).splitlines()
    for name in applets:
        if not re.fullmatch(r'[A-Za-z0-9_+.\[\]-]+',name):raise ValueError('Unexpected private shell applet')
        destination=shell_bin/(name+'.exe')
        if not destination.exists():executable_alias(shell_bin/'busybox.exe',destination)
    executable_alias(python, python.with_name('python3.exe'))
    # WinLibs also contains CMake/Ninja. Prefer the independently pinned tools,
    # including their tested archive extraction and generated-command behavior.
    paths = [python.parent, bundled_bin, tools / 'cmake/cmake-3.31.8-windows-x86_64/bin',
             tools / 'ninja',compiler, tools / 'git/cmd', shell_bin,tools / 'git/usr/bin']
    result = dict(os.environ)
    result.update(PATH=os.pathsep.join(map(str, paths)) + os.pathsep + result.get('PATH', ''),
        SMB_BUILD_TARGET='windows', SMB_WINDOWS_NATIVE='1',
        SMB_WINDOWS_CC='x86_64-w64-mingw32-gcc', SHELL=(shell_bin / 'sh.exe').as_posix(),
        GIT_CONFIG_COUNT='3', GIT_CONFIG_KEY_0='core.autocrlf', GIT_CONFIG_VALUE_0='false',
        GIT_CONFIG_KEY_1='core.eol', GIT_CONFIG_VALUE_1='lf',
        GIT_CONFIG_KEY_2='http.sslBackend',GIT_CONFIG_VALUE_2='openssl')
    certificate = python.parent / 'Lib/site-packages/certifi/cacert.pem'
    if certificate.is_file():
        result.update(SSL_CERT_FILE=str(certificate), CURL_CA_BUNDLE=str(certificate),GIT_SSL_CAINFO=str(certificate))
    return result
