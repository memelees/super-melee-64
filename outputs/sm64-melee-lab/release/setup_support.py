"""Installer-only state, logging and cancellable child-process support."""
from pathlib import Path
import json, os, signal, shutil, subprocess, sys, tempfile, threading, time

class SetupCancelled(RuntimeError):
    pass

def atomic_json(path, value):
    path=Path(path);path.parent.mkdir(parents=True,exist_ok=True)
    temporary=path.with_name(path.name+'.tmp')
    temporary.write_text(json.dumps(value,indent=2)+'\n');temporary.replace(path)

def filesystem_report(root, minimum_gib=12):
    root=Path(root).resolve();existing=root
    while not existing.exists():existing=existing.parent
    free=shutil.disk_usage(existing).free
    return dict(root=str(root),free_bytes=free,required_bytes=int(minimum_gib*1024**3),
        enough_space=free>=minimum_gib*1024**3,path_has_spaces=any(c.isspace() for c in str(root)),
        writable=os.access(existing,os.W_OK))

def require_filesystem(root, minimum_gib=12):
    report=filesystem_report(root,minimum_gib)
    if report['path_has_spaces']:
        raise RuntimeError('Choose a build folder without spaces, such as ~/SuperMelee64 or C:\\SuperMelee64. Game filenames may contain spaces.')
    if not report['writable']:
        raise RuntimeError('The build folder is not writable. Choose a folder owned by your account; do not run game setup as administrator/root.')
    if not report['enough_space']:
        raise RuntimeError('Setup needs at least %.1f GiB free in the build folder; %.1f GiB is available. Verified caches are kept when setup stops.'%(minimum_gib,report['free_bytes']/1024**3))
    Path(root).mkdir(parents=True,exist_ok=True)
    with tempfile.NamedTemporaryFile(prefix='.setup-write-check-',dir=root):pass
    return report

def checkout_pinned(url, revision, path, run):
    """Publish a checkout only after its exact revision is present."""
    path=Path(path)
    if len(revision)!=40 or any(c not in '0123456789abcdef' for c in revision):raise ValueError('A source dependency must use an exact commit')
    if not path.exists():
        path.parent.mkdir(parents=True,exist_ok=True);partial=path.with_name(path.name+'.partial')
        if not partial.exists():run(['git','init',partial])
        remote=subprocess.run(['git','-C',str(partial),'config','--get','remote.origin.url'],text=True,capture_output=True).stdout.strip()
        if remote and remote!=url:raise RuntimeError('Preserving a partial checkout with a different origin: '+str(partial))
        if not remote:run(['git','-C',partial,'remote','add','origin',url])
        run(['git','-C',partial,'fetch','--depth','1','origin',revision])
        run(['git','-C',partial,'checkout','--detach',revision]);partial.rename(path)
    actual=subprocess.check_output(['git','-C',str(path),'rev-parse','HEAD'],text=True).strip()
    if actual!=revision:raise RuntimeError('Existing dependency has a different revision; preserving it: '+str(path))
    run(['git','-C',path,'submodule','update','--init','--recursive','--jobs','6'])
    return revision

class Tee:
    def __init__(self, console, log):self.console=console;self.log=log
    def write(self, value):self.console.write(value);self.log.write(value);self.flush();return len(value)
    def flush(self):self.console.flush();self.log.flush()
    def isatty(self):return False

class Session:
    def __init__(self, root, identity, cancel_file=None):
        self.root=Path(root);self.work=self.root/'work';self.identity=identity
        self.external_cancel=cancel_file is not None
        self.cancel_file=Path(cancel_file) if cancel_file else self.work/'setup.cancel'
        self.state_path=self.work/'setup-state.json';self.lock=self.work/'setup.lock'
        self.cancelled=False;self.child=None;self.completed=[];self.current=None
        self.log_path=self.root/'setup.log';self._handlers={}
    def __enter__(self):
        self.work.mkdir(parents=True,exist_ok=True)
        if self.lock.exists():
            try:
                pid=int(self.lock.read_text());os.kill(pid,0)
            except (ValueError,ProcessLookupError):self.lock.unlink(missing_ok=True)
            except PermissionError:raise RuntimeError('Another setup process owns this build folder.')
            else:raise RuntimeError('Setup is already running for this build folder (PID %d).'%pid)
        descriptor=os.open(self.lock,os.O_CREAT|os.O_EXCL|os.O_WRONLY,0o600)
        with os.fdopen(descriptor,'w') as out:out.write(str(os.getpid()))
        if not self.external_cancel:self.cancel_file.unlink(missing_ok=True)
        try:self.log=self.log_path.open('a',buffering=1)
        except BaseException:self.lock.unlink(missing_ok=True);raise
        self.stdout,self.stderr=sys.stdout,sys.stderr
        sys.stdout=Tee(self.stdout,self.log);sys.stderr=Tee(self.stderr,self.log)
        for sig in (signal.SIGINT,signal.SIGTERM):
            self._handlers[sig]=signal.getsignal(sig);signal.signal(sig,self._signal)
        prior=None
        if self.state_path.is_file():
            try:prior=json.loads(self.state_path.read_text())
            except (ValueError,OSError):pass
        print('\nSUPER MELEE 64 SETUP — '+time.strftime('%Y-%m-%d %H:%M:%S'))
        print('Log: '+str(self.log_path))
        if prior:print('Resuming: verified downloads and compiler outputs are retained. Steps run incrementally and verify their pins again.')
        self.save('running');return self
    def _signal(self,*_):self.cancelled=True
    def check_cancel(self):
        if self.cancelled or self.cancel_file.exists():raise SetupCancelled('Setup cancelled. Inputs are unchanged; run Build again to resume verified caches.')
    def save(self,status,error=None):
        atomic_json(self.state_path,dict(status=status,identity=self.identity,current=self.current,
            completed=self.completed,error=error,updated=time.strftime('%Y-%m-%dT%H:%M:%S%z')))
    def stage(self, number, total, label):
        self.check_cancel();self.current=dict(number=number,total=total,label=label)
        self.save('running');print('[%d/%d] %s'%(number,total,label),flush=True)
    def run(self, command, **kw):
        self.check_cancel();command=[str(x) for x in command]
        print('> '+subprocess.list2cmdline(command),flush=True)
        if 'shell' in kw:raise ValueError('Installer commands must use explicit argument lists')
        process=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
            text=True,errors='replace',start_new_session=os.name!='nt',**kw);self.child=process
        def stream():
            for line in process.stdout:print(line,end='',flush=True)
        reader=threading.Thread(target=stream,daemon=True);reader.start()
        try:
            while process.poll() is None:
                self.check_cancel();time.sleep(.15)
            reader.join(timeout=3)
            if process.returncode:raise subprocess.CalledProcessError(process.returncode,command)
        except BaseException:
            if process.poll() is None:
                try:
                    if os.name!='nt':os.killpg(process.pid,signal.SIGTERM)
                    else:process.terminate()
                except ProcessLookupError:pass
                try:process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    try:
                        if os.name!='nt':os.killpg(process.pid,signal.SIGKILL)
                        else:process.kill()
                    except ProcessLookupError:pass
                    process.wait()
            reader.join(timeout=3);raise
        finally:
            process.stdout.close();self.child=None
    def done(self):
        if self.current and self.current not in self.completed:self.completed.append(self.current)
        self.save('running')
    def __exit__(self, kind, error, traceback):
        try:
            self.save('cancelled' if isinstance(error,SetupCancelled) else 'failed' if error else 'complete',str(error) if error else None)
            if error:print('\n'+str(error)+'\nLog: '+str(self.log_path),file=sys.stderr)
        finally:
            for sig,handler in self._handlers.items():signal.signal(sig,handler)
            sys.stdout=self.stdout;sys.stderr=self.stderr
            self.log.close();self.lock.unlink(missing_ok=True)
        return False
