#!/usr/bin/env python3
"""Asset-free installer tests. Never install tools, build engines or start games."""
from pathlib import Path
import contextlib,hashlib,importlib.util,io,json,os,shutil,subprocess,sys,tempfile,threading,time,unittest
from unittest import mock
import setup_support as support
import Setup as setup
import prepare_installer_inputs as inputs

class InstallerTests(unittest.TestCase):
    def setUp(self):
        self.temporary=tempfile.TemporaryDirectory(prefix='supermelee-installer-')
        self.root=Path(self.temporary.name)
    def tearDown(self):self.temporary.cleanup()
    def session(self,**kw):return support.Session(self.root,{'test':'asset-free'},**kw)
    def state(self):return json.loads((self.root/'work/setup-state.json').read_text())
    def test_public_adapter_is_bundled(self):
        self.assertEqual(setup.PINS['smash-royale'],('bundled-source-bootstrap',setup.ADAPTER_PIN))
        self.assertFalse(setup.ADAPTER_PIN.startswith('http'))
    def test_bootstrap_integrity(self):
        manifest=setup.verify_bootstrap(setup.BOOTSTRAP)
        self.assertTrue(manifest['source_only']);self.assertEqual(manifest['revision'],setup.ADAPTER_PIN)
    def test_filesystem_checks(self):
        report=support.filesystem_report(self.root,1)
        self.assertIsInstance(report['free_bytes'],int)
        with mock.patch.object(support,'filesystem_report',return_value=dict(report,path_has_spaces=True)):
            with self.assertRaisesRegex(RuntimeError,'without spaces'):support.require_filesystem(self.root)
        with mock.patch.object(support,'filesystem_report',return_value=dict(report,writable=False,path_has_spaces=False)):
            with self.assertRaisesRegex(RuntimeError,'not writable'):support.require_filesystem(self.root)
        with mock.patch.object(support,'filesystem_report',return_value=dict(report,enough_space=False,path_has_spaces=False,writable=True)):
            with self.assertRaisesRegex(RuntimeError,'available'):support.require_filesystem(self.root)
    def test_staged_success_and_resume(self):
        with self.session() as session:
            session.stage(1,2,'Asset-free child');session.run([sys.executable,'-c','print("child-output")']);session.done()
        self.assertEqual(self.state()['status'],'complete')
        self.assertEqual(self.state()['completed'][0]['number'],1)
        self.assertIn('child-output',(self.root/'setup.log').read_text())
        with self.session() as session:session.stage(2,2,'Resume validation');session.done()
        self.assertIn('Resuming:',(self.root/'setup.log').read_text())
        self.assertFalse((self.root/'work/setup.lock').exists())
    def test_active_lock_is_preserved(self):
        with self.session():
            with self.assertRaisesRegex(RuntimeError,'already running'):
                with self.session():pass
            self.assertTrue((self.root/'work/setup.lock').exists())
    def test_pid_probe_current_process_never_signals_on_windows(self):
        # On Windows os.kill(pid, 0) emits CTRL_C_EVENT instead of probing.
        if os.name=='nt':
            with mock.patch.object(os,'kill',side_effect=AssertionError('A Windows PID probe must not signal')):
                self.assertTrue(support.pid_is_running(os.getpid()))
        else:self.assertTrue(support.pid_is_running(os.getpid()))
    def test_pid_probe_invalid_group_ids(self):
        with mock.patch.object(os,'kill',side_effect=AssertionError('Invalid PID reached signalling API')):
            self.assertFalse(support.pid_is_running(0));self.assertFalse(support.pid_is_running(-1))
    def test_pid_probe_child_lifetime(self):
        child=subprocess.Popen([sys.executable,'-c','import time; time.sleep(5)'])
        try:self.assertTrue(support.pid_is_running(child.pid))
        finally:child.terminate();child.wait(timeout=5)
        self.assertFalse(support.pid_is_running(child.pid))
    def test_stale_lock_recovered(self):
        work=self.root/'work';work.mkdir();(work/'setup.lock').write_text('invalid-stale-pid')
        with self.session():pass
        self.assertFalse((work/'setup.lock').exists())
    def test_child_failure_recorded(self):
        with self.assertRaises(subprocess.CalledProcessError):
            with self.session() as session:session.run([sys.executable,'-c','raise SystemExit(7)'])
        self.assertEqual(self.state()['status'],'failed');self.assertFalse((self.root/'work/setup.lock').exists())
    def test_external_cancel_before_child_is_kept(self):
        cancel=self.root/'cancel';cancel.touch()
        with self.assertRaises(support.SetupCancelled):
            with self.session(cancel_file=cancel) as session:session.stage(1,1,'Cancelled before build')
        self.assertEqual(self.state()['status'],'cancelled')
    def test_cancel_stops_child_and_keeps_cache(self):
        cached=self.root/'verified-cache';cached.write_text('keep')
        ready=self.root/'ready';late=self.root/'should-not-exist'
        cancel=self.root/'stop'
        def request():
            deadline=time.monotonic()+5
            while not ready.exists() and time.monotonic()<deadline:time.sleep(.01)
            cancel.touch()
        worker=threading.Thread(target=request);worker.start()
        code='from pathlib import Path; import time; Path(%r).touch(); time.sleep(4); Path(%r).touch()'%(str(ready),str(late))
        started=time.monotonic()
        with self.assertRaises(support.SetupCancelled):
            with self.session(cancel_file=cancel) as session:session.run([sys.executable,'-c',code])
        worker.join(timeout=5)
        self.assertLess(time.monotonic()-started,10)
        self.assertFalse(late.exists());self.assertEqual(cached.read_text(),'keep')
        self.assertEqual(self.state()['status'],'cancelled')
    def test_shell_execution_rejected(self):
        with self.session() as session:
            with self.assertRaisesRegex(ValueError,'argument lists'):session.run(['echo','test'],shell=True)
    def test_owned_input_copy_is_atomic_and_preserves_conflicts(self):
        original=self.root/'own input';original.write_bytes(b'original test bytes')
        expected=hashlib.sha256(original.read_bytes()).hexdigest();target=self.root/'prepared/input'
        setup.copy_owned_input(original,target,'sha256',expected)
        setup.copy_owned_input(original,target,'sha256',expected)
        self.assertEqual(target.read_bytes(),original.read_bytes())
        target.write_bytes(b'different')
        with self.assertRaisesRegex(RuntimeError,'Preserving'):setup.copy_owned_input(original,target,'sha256',expected)
        self.assertEqual(target.read_bytes(),b'different');self.assertEqual(original.read_bytes(),b'original test bytes')
    def test_cancelled_input_copy_can_retry(self):
        source=self.root/'input';source.write_bytes(b'x'*(1024*1024+7));target=self.root/'out/input'
        expected=hashlib.sha256(source.read_bytes()).hexdigest()
        fake=mock.Mock();fake.check_cancel.side_effect=support.SetupCancelled('cancel')
        with mock.patch.object(setup,'SESSION',fake):
            with self.assertRaises(support.SetupCancelled):setup.copy_owned_input(source,target,'sha256',expected)
        self.assertFalse(target.exists());self.assertTrue(target.with_name('input.partial').exists())
        setup.copy_owned_input(source,target,'sha256',expected);self.assertEqual(setup.digest(target,'sha256'),expected)
    def test_wrong_copy_hash_never_published(self):
        source=self.root/'input';source.write_bytes(b'not a game');target=self.root/'out/input'
        with self.assertRaisesRegex(RuntimeError,'verification failed'):setup.copy_owned_input(source,target,'sha256','0'*64)
        self.assertFalse(target.exists());self.assertEqual(source.read_bytes(),b'not a game')
    def test_windows_settings_portable_and_native(self):
        env=setup.windows_environment()
        self.assertEqual(env['SMB_PROFILE_NEW'],'${NEW_PROFILE}')
        self.assertEqual(env['SMB_PROFILE_CONTINUE'],'${CONTINUE_PROFILE}')
        self.assertIn('latest.txt',env['SMB_PLAYTHROUGH_LATEST'])
        for key,value in env.items():
            self.assertTrue(key.startswith(('SMB_','SM64_MELEE_','MELEE_','MELEE3D_')))
            self.assertNotIn('\n',value);self.assertNotIn('\r',value)
            self.assertNotIn('https://',value)
        helper=Path(__file__).parent
        for name in ('Play Super Melee 64.cmd','Play Super Melee 64.vbs'):
            text=(helper/name).read_text().lower();self.assertNotIn('python',text);self.assertIn('super melee 64.exe',text)
    def test_check_tools_reads_no_games_and_writes_no_setup(self):
        with mock.patch.object(setup,'ROOT',self.root),mock.patch.object(sys,'argv',['Setup.py','--check-tools']),contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(setup.main(),0)
        report=json.loads(output.getvalue());self.assertIn('missing',report);self.assertIn('filesystem',report)
        self.assertFalse((self.root/'work').exists());self.assertFalse((self.root/'setup.log').exists())
    def test_patch_resume_and_verified_cache(self):
        target=self.root/'official-patch';partial=target.with_name(target.name+'.partial');partial.write_bytes(b'ab')
        payload=b'abcdef';digest=hashlib.sha256(payload).hexdigest();calls=[]
        def fetch(command):
            calls.append(command);self.assertIn('--continue-at',command)
            self.assertEqual(partial.read_bytes(),b'ab');partial.write_bytes(payload)
        inputs.download_pinned('https://github.com/example/patch',target,digest,len(payload),fetch)
        inputs.download_pinned('https://github.com/example/patch',target,digest,len(payload),lambda _:self.fail('Verified cache was downloaded again'))
        self.assertEqual(len(calls),1);self.assertEqual(target.read_bytes(),payload);self.assertFalse(partial.exists())
    def test_patch_checksum_failure_never_published(self):
        target=self.root/'patch'
        def fetch(command):Path(command[command.index('--output')+1]).write_bytes(b'wrong')
        with self.assertRaisesRegex(RuntimeError,'pinned SHA-256'):
            inputs.download_pinned('https://github.com/example/patch',target,'0'*64,5,fetch)
        self.assertFalse(target.exists());self.assertEqual(len(list(self.root.glob('*.rejected-*'))),1)
    def test_patch_range_refusal_retries_from_zero(self):
        target=self.root/'patch';partial=target.with_name(target.name+'.partial');partial.write_bytes(b'ab')
        payload=b'abcdef';calls=[]
        def fetch(command):
            calls.append(command)
            if len(calls)==1:raise subprocess.CalledProcessError(33,command)
            self.assertFalse(partial.exists());partial.write_bytes(payload)
        inputs.download_pinned('https://github.com/example/patch',target,hashlib.sha256(payload).hexdigest(),len(payload),fetch)
        self.assertEqual(len(calls),2);self.assertEqual(target.read_bytes(),payload)
    def test_changed_existing_patch_preserved(self):
        target=self.root/'patch';target.write_bytes(b'changed')
        with self.assertRaisesRegex(RuntimeError,'Preserving'):
            inputs.download_pinned('https://github.com/example/patch',target,'0'*64,7,lambda _:self.fail('Unexpected download'))
        self.assertEqual(target.read_bytes(),b'changed')
    def test_interrupted_source_checkout_resumes_atomically(self):
        if not shutil.which('git'):self.skipTest('Git unavailable')
        origin=self.root/'origin';origin.mkdir()
        def run(command):subprocess.run([str(x) for x in command],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        run(['git','init',origin]);(origin/'test-source').write_text('asset-free test')
        run(['git','-C',origin,'add','test-source'])
        run(['git','-C',origin,'-c','user.name=Installer Test','-c','user.email=installer-test@invalid','commit','-m','fixture'])
        revision=subprocess.check_output(['git','-C',str(origin),'rev-parse','HEAD'],text=True).strip()
        target=self.root/'checkout';calls=[]
        def interrupt(command):
            calls.append(command)
            if 'fetch' in command:raise support.SetupCancelled('Interrupted source fetch')
            run(command)
        with self.assertRaises(support.SetupCancelled):support.checkout_pinned(str(origin),revision,target,interrupt)
        self.assertFalse(target.exists());self.assertTrue(target.with_name('checkout.partial').is_dir())
        support.checkout_pinned(str(origin),revision,target,run)
        self.assertEqual((target/'test-source').read_text(),'asset-free test')
        self.assertFalse(target.with_name('checkout.partial').exists())

if __name__=='__main__':unittest.main(verbosity=2)
