"""Backend for the native Windows installer. Game images never leave this PC."""
from pathlib import Path, PurePosixPath
import argparse
import hashlib
import importlib.util
import json
import os
import shutil
import subprocess
import sys
import threading
import time
import re

from windows_dependencies import PACKAGES, environment, fetch, sha, unpack, safe_member
from windows_native_build import build_drive
from setup_support import SetupCancelled, atomic_json


def data_directory():
    value = os.environ.get('LOCALAPPDATA')
    if not value:
        raise RuntimeError('Windows did not provide your local application-data folder.')
    return Path(value) / 'Memelees/SuperMelee64'


def source_files(source):
    """Use a fixed reviewed manifest, never a recursive user-folder merge."""
    source = Path(source)
    manifest = json.loads((source / 'PUBLIC-SOURCE-MANIFEST.json').read_text(encoding='utf-8'))
    if not manifest.get('source_only') or manifest.get('git_history_included'):
        raise ValueError('The installer source kit failed its manifest check.')
    result = dict(manifest['files'])
    result['PUBLIC-SOURCE-MANIFEST.json'] = sha(source / 'PUBLIC-SOURCE-MANIFEST.json')
    for name, expected in result.items():
        safe_member(name)
        path = PurePosixPath(name)
        if path.is_absolute() or '..' in path.parts or '\\' in name or ':' in name:
            raise ValueError('Unsafe installer source path')
        target = source / name
        if target.is_symlink() or not target.is_file() or sha(target) != expected:
            raise RuntimeError('An installer source file failed verification: ' + name)
    return result


def copy_sources(source, workspace):
    source, workspace = Path(source), Path(workspace)
    files = source_files(source)
    record = workspace / '.supermelee-source-files.json'
    previous = json.loads(record.read_text(encoding='utf-8')) if record.exists() else {}
    # Verify every conflict before replacing any source file. Preserve work/,
    # imported games, saves and any edits that were not made by this installer.
    for name, expected in files.items():
        target = workspace / name
        if any(parent.is_symlink() for parent in (target,*target.parents) if parent!=workspace and workspace in parent.parents):
            raise RuntimeError('Preserving a linked folder in the existing build: ' + name)
        known=(expected,previous.get(name))
        line_endings_only=False
        if target.is_file() and target.suffix.lower() in ('.py','.c','.cpp','.h','.inc','.json','.html','.md','.txt','.ps1','.swift','.patch'):
            line_endings_only=hashlib.sha256(target.read_bytes().replace(b'\r\n',b'\n')).hexdigest() in known
        if target.exists() and (target.is_symlink() or (sha(target) not in known and not line_endings_only)):
            raise RuntimeError('Preserving edited source in the existing build folder: ' + name)
    workspace.mkdir(parents=True, exist_ok=True)
    for name, expected in files.items():
        target = workspace / name
        if not target.exists() or sha(target) != expected:
            target.parent.mkdir(parents=True, exist_ok=True)
            temporary = target.with_name(target.name + '.setup-new')
            shutil.copy2(source / name, temporary)
            temporary.replace(target)
    atomic_json(record, files)
    return files


def quiet_children():
    """Keep native compilers out of separate console windows."""
    original = subprocess.Popen
    class QuietProcess(original):
        def __init__(self, *args, **kwargs):
            kwargs['creationflags'] = kwargs.get('creationflags', 0) | subprocess.CREATE_NO_WINDOW
            super().__init__(*args, **kwargs)
    subprocess.Popen = QuietProcess


STATUS_LOCK = threading.Lock()
STATUS = dict(data=None, label='Starting setup', progress=0, changed=0.0)
STATUS_STOP = threading.Event()


def write_status(data, label, progress):
    # Plain UTF-8 keeps the Win32 frontend independent of a JSON dependency.
    path = Path(data) / 'setup-ui.status'
    temporary = path.with_suffix('.tmp')
    temporary.write_text(str(progress) + '\n' + label.replace('\n', ' ') + '\n', encoding='utf-8')
    temporary.replace(path)


def publish_ui(data, label, progress=0):
    with STATUS_LOCK:
        STATUS.update(data=Path(data), label=label, progress=progress, changed=time.monotonic())
        write_status(data, label, progress)


def start_status_heartbeat(interval=15):
    """A long silent step (disc patching, a compiler) must not look like a stall."""
    def tick():
        while not STATUS_STOP.wait(interval):
            with STATUS_LOCK:
                data = STATUS['data']
                if data is None or STATUS['progress'] >= 100:
                    continue
                minutes, seconds = divmod(int(time.monotonic() - STATUS['changed']), 60)
                label = '%s (still working, %d:%02d)' % (STATUS['label'], minutes, seconds)
                try:
                    write_status(data, label, STATUS['progress'])
                except OSError:
                    pass  # The frontend may be closing; never interrupt the build.
    thread = threading.Thread(target=tick, name='setup-status', daemon=True)
    thread.start()
    return thread


def load_setup(workspace):
    native = Path(workspace) / 'outputs/sm64-melee-lab/native'
    release = native.parent / 'release'
    sys.path[:0] = [str(release), str(native)]
    # The worker runs in a separate process, so no earlier source-kit modules
    # can survive an update and shadow the files just verified above.
    spec = importlib.util.spec_from_file_location('supermelee_windows_setup', release / 'Setup.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def verify_tools(package):
    tools = Path(package) / 'Tools'
    manifest = json.loads((tools / 'manifest.json').read_text(encoding='utf-8'))
    for name, expected in manifest['files'].items():
        safe_member(name)
        target = tools / name
        if target.is_symlink() or not target.is_file() or sha(target) != expected:
            raise RuntimeError('A bundled setup tool failed verification: ' + name)
    for name in ('7zz.exe', 'xdelta3.exe', 'curl.exe', 'ffi/lib/libffi.a'):
        if name not in manifest['files']:
            raise RuntimeError('The native installer is missing a required build tool: ' + name)
    return tools


def install(args):
    data = (args.data or data_directory()).resolve()
    package = args.package.resolve()
    data.mkdir(parents=True, exist_ok=True)
    start_status_heartbeat()
    cancel = data / 'setup.cancel'
    cancel.unlink(missing_ok=True)
    lock = data / 'setup.lock'
    from setup_support import pid_is_running
    if lock.exists():
        try:
            active = pid_is_running(int(lock.read_text()))
        except (ValueError, OSError):
            active = False
        if active:
            raise RuntimeError('Another setup process is already running. Return to it before retrying.')
        lock.unlink()
    descriptor = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
    with os.fdopen(descriptor, 'w') as stream:
        stream.write(str(os.getpid()))
    def check_cancel():
        if cancel.exists():
            raise SetupCancelled('Setup stopped safely. Your game files and verified downloads are kept.')
    workspace = data / 'Workspace'
    try:
        publish_ui(data, 'Checking the installer and your game files…', 1)
        files = copy_sources(package / 'SourceKit', workspace)
        setup = load_setup(workspace)
        # Validate user inputs before fetching tools or changing game outputs.
        identities = setup.verify_inputs(args.rom, args.melee)
        if args.check_only:
            print(json.dumps(dict(verified=True, inputs=identities, downloaded=False)))
            return 0
        if shutil.disk_usage(data).free < args.minimum_free_gib * 1024**3:
            raise RuntimeError('Setup needs at least %.1f GiB free in the build folder; %.1f GiB is available.' %
                               (args.minimum_free_gib, shutil.disk_usage(data).free / 1024**3))
        tools = verify_tools(package)
        certificate = package / 'Python/Lib/site-packages/certifi/cacert.pem'
        if certificate.is_file():os.environ['SSL_CERT_FILE']=str(certificate)
        (data / 'ready.json').unlink(missing_ok=True)
        preferred = 'R:'
        prior = data / 'build-drive.json'
        if prior.exists():
            preferred = json.loads(prior.read_text())['drive']
        with build_drive(workspace, preferred) as alias:
            os.environ['SMB_WINDOWS_BUILD_ROOT'] = str(alias)
            # Reimport after mapping so each native module uses the short,
            # installer-owned path even with spaces/non-ASCII in the account.
            for name in ('build_host', 'package_release', 'build_mac_app'):
                sys.modules.pop(name, None)
            setup = load_setup(alias)
            private = alias / 'work/windows-tools'
            downloads = alias / 'work/windows-downloads'
            quiet_children()
            for index, (name, pin) in enumerate(PACKAGES.items()):
                publish_ui(data, 'Preparing Windows tools: ' + name + '…', 3 + index * 3)
                archive = fetch(pin, downloads, check_cancel)
                unpack(name, archive, private, tools / '7zz.exe')
                check_cancel()
            library = alias / 'work/windows-ffi'
            if not library.exists():
                shutil.copytree(tools / 'ffi', library)
            env = environment(private, tools, package / 'Python/python.exe')
            env.update(SMB_WINDOWS_BUILD_ROOT=str(alias), SMB_WINDOWS_FFI=str(library),
                       SMB_SETUP_PACKAGE=str(package), SMB_SETUP_CANCEL=str(cancel), SMB_KEEP_LINEUP='1', PYTHONUTF8='1', PYTHONUNBUFFERED='1')
            # Every tool is private to this build; nothing is installed globally.
            os.environ.update(env)
            from windows_extract import configure as configure_extraction
            configure_extraction(private/'cmake/cmake-3.31.8-windows-x86_64/bin/cmake.exe',
                                 package/'Python/python.exe',alias/'outputs/sm64-melee-lab/release/windows_extract.py')
            atomic_json(prior, dict(drive=alias.drive))
            original_stage = setup.Session.stage
            def stage(session, number, total, label):
                publish_ui(data, label + '…', 15 + int(80 * (number - 1) / total))
                def output(line):
                    match=re.match(r'^\[(\d+)/(\d+)\]',line)
                    if match and int(match[2])>0:
                        fraction=min(1,int(match[1])/int(match[2]))
                        percent=15+int(80*(number-1+fraction)/total)
                        publish_ui(data,label+'… ('+match[1]+'/'+match[2]+')',percent)
                session.output_callback=output
                return original_stage(session, number, total, label)
            setup.Session.stage = stage
            setup.ROOT = alias
            setup.NATIVE = alias / 'outputs/sm64-melee-lab/native'
            sys.argv = ['Setup.py', '--platform', 'windows', '--rom', str(args.rom), '--melee', str(args.melee),
                        '--cancel-file', str(cancel), '--minimum-free-gib', str(args.minimum_free_gib)]
            code = setup.entrypoint()
            if code:
                error = alias / 'work/setup-error.json'
                if error.exists():
                    shutil.copy2(error, data / 'setup-error.json')
                    from setup_diagnostics import format_diagnostic
                    (data / 'setup-error.txt').write_text(format_diagnostic(json.loads(error.read_text(encoding='utf-8'))),encoding='utf-8')
                return code
            check_cancel()
            launcher = workspace / 'Super Melee 64.exe'
            if not launcher.is_file():
                raise RuntimeError('Setup completed without creating the Windows game launcher.')
            # Record known installer-generated changes as managed, not user edits.
            atomic_json(workspace / '.supermelee-source-files.json',
                        {name: sha(workspace / name) for name in files})
            atomic_json(data / 'ready.json', dict(version=setup.VERSION, launcher=str(launcher), inputs=identities))
            (data / 'installed-version.txt').write_text(setup.VERSION,encoding='utf-8')
            publish_ui(data, 'Super Melee 64 is ready to play.', 100)
            return 0
    finally:
        STATUS_STOP.set()
        lock.unlink(missing_ok=True)
        os.environ.pop('SMB_WINDOWS_BUILD_ROOT', None)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', required=True, type=Path)
    parser.add_argument('--data', type=Path)
    parser.add_argument('--rom', required=True, type=Path)
    parser.add_argument('--melee', required=True, type=Path)
    parser.add_argument('--check-only', action='store_true')
    parser.add_argument('--minimum-free-gib', type=float, default=12)
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('Use the Mac installer on macOS; this helper runs natively on Windows.')
    args.rom = args.rom.expanduser().resolve()
    args.melee = args.melee.expanduser().resolve()
    if not 1 <= args.minimum_free_gib <= 1024:
        parser.error('minimum-free-gib must be between 1 and 1024')
    try:
        return install(args)
    except SetupCancelled as error:
        print(str(error), flush=True)
        return 130
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        from setup_diagnostics import diagnose, format_diagnostic
        version='native-windows'
        try:
            version_file=args.package/'SourceKit/outputs/sm64-melee-lab/native/package_release.py'
            match=re.search(r"^VERSION='([^']+)'",version_file.read_text(encoding='utf-8'),re.M)
            if match:version=match.group(1)
        except OSError:pass
        diagnostic = diagnose(error, version=version)
        data = args.data or data_directory()
        try:
            atomic_json(data / 'setup-error.json', diagnostic)
            (data / 'setup-error.txt').write_text(format_diagnostic(diagnostic), encoding='utf-8')
        except OSError:pass # Keep the original explanation in the GUI's open log.
        print(format_diagnostic(diagnostic), file=sys.stderr, flush=True)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
