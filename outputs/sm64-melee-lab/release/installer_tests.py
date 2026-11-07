#!/usr/bin/env python3
"""Asset-free installer tests. Never install tools, build engines or start games."""
from pathlib import Path
import contextlib,hashlib,importlib.util,io,json,os,plistlib,shutil,subprocess,sys,tempfile,threading,time,unittest
from unittest import mock
import setup_support as support
import Setup as setup
import prepare_installer_inputs as inputs
import mac_toolchain as mac
import build_host
import build_coop
import build_mac_app as mac_app
import refresh_melee
import build_install_helper as mac_helper
from setup_diagnostics import diagnose,format_diagnostic

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
    @unittest.skipUnless(os.name!='nt' and (shutil.which('gmake') or shutil.which('make')),'Native Make fixture requires Mac or Linux')
    def test_reproducible_zip_source_update_rebuilds_cached_native_output(self):
        kit=self.root/'kit';kit.mkdir();source=kit/'fixture.c';source.write_text('old version\n')
        target=self.root/'fixture.c'
        built=self.root/'fixture.o'
        (self.root/'Makefile').write_text('fixture.o: fixture.c\n\tcp fixture.c fixture.o\n')
        source.write_text('new version\n');os.utime(source,(1700000000,1700000000))
        # Also cover a source already copied by an older installer, whose
        # cached object never rebuilt because copy2 kept the ZIP's old mtime.
        for prior in ('old version\n','new version\n'):
            target.write_text(prior);os.utime(target,(1700000000,1700000000))
            built.write_text('old version\n');os.utime(built,(1700000100,1700000100))
            build_host.copy_build_source(source,target)
            subprocess.run([shutil.which('gmake') or shutil.which('make')],cwd=self.root,check=True,capture_output=True)
            self.assertEqual(built.read_text(),'new version\n')
    def app_fixture(self,parent,value,identity='dev.supermelee64.launcher'):
        app=parent/'Super Melee 64.app';contents=app/'Contents';contents.mkdir(parents=True)
        (contents/'Info.plist').write_bytes(plistlib.dumps(dict(CFBundleIdentifier=identity)))
        (contents/'fixture').write_text(value)
        return app
    def test_deployment_target_upgrade_discards_old_objects_but_preserves_assets(self):
        output=self.root/'build';output.mkdir()
        for name in ('old.o','old.d','old.a','sm64.us.f3dex2e','sound-data.bin'):(output/name).write_text('cached')
        with mock.patch.dict(os.environ,{'MACOSX_DEPLOYMENT_TARGET':'27.0'}):
            environment=build_host.mac_build_environment(output)
        self.assertEqual(environment['MACOSX_DEPLOYMENT_TARGET'],'15.0')
        self.assertEqual((output/'sound-data.bin').read_text(),'cached')
        self.assertFalse((output/'old.o').exists());self.assertFalse((output/'sm64.us.f3dex2e').exists())
        (output/'current.o').write_text('correct target')
        build_host.mac_build_environment(output)
        self.assertEqual((output/'current.o').read_text(),'correct target')
    def test_mac_install_defaults_to_shared_or_personal_applications(self):
        shared=self.root/'Shared Applications';shared.mkdir();personal=self.root/'User Applications'
        with mock.patch.object(mac_app.os,'access',return_value=True):
            self.assertEqual(mac_app.application_destination(shared,personal),shared/'Super Melee 64.app')
        with mock.patch.object(mac_app.os,'access',return_value=False):
            self.assertEqual(mac_app.application_destination(shared,personal),personal/'Super Melee 64.app')
        self.assertFalse(personal.exists())
    def test_setup_enables_install_mode_and_returns_the_installed_app(self):
        target=self.app_fixture(self.root/'Applications','installed')
        with mock.patch.object(setup,'application_destination',return_value=target),mock.patch.object(setup,'run') as run:
            self.assertEqual(setup.create_mac_launcher(),target)
        command=run.call_args.args[0]
        self.assertIn('--install',command)
        self.assertEqual(command[-2:],['--applications-dir',target.parent])
        with mock.patch.object(setup,'application_destination',return_value=self.root/'missing.app'),mock.patch.object(setup,'run'):
            with self.assertRaisesRegex(RuntimeError,'installation did not create'):setup.create_mac_launcher()
    def test_mac_install_publishes_in_applications_and_retains_previous_version(self):
        source=self.app_fixture(self.root/'build volume','new');target=self.app_fixture(self.root/'Applications volume','old')
        with mock.patch.object(mac_app,'clone',side_effect=lambda src,dst:shutil.copytree(src,dst)),mock.patch.object(mac_app,'run') as verify:
            self.assertEqual(mac_app.publish_app(source,target),target)
        self.assertEqual((target/'Contents/fixture').read_text(),'new')
        self.assertEqual((source/'Contents/fixture').read_text(),'new')
        previous=list(target.parent.glob('.previous-super-melee-app-*'))
        self.assertEqual(len(previous),1);self.assertEqual((previous[0]/'Contents/fixture').read_text(),'old')
        # Verify the transfer in the destination filesystem and the final path.
        self.assertEqual(verify.call_args_list[0].args[-1].parent.parent,target.parent)
        self.assertEqual(verify.call_args_list[-1].args[-1],target)
    def test_mac_install_rejects_unsigned_copy_before_replacing_existing_app(self):
        source=self.app_fixture(self.root/'build','new');target=self.app_fixture(self.root/'Applications','old')
        with mock.patch.object(mac_app,'clone',side_effect=lambda src,dst:shutil.copytree(src,dst)),mock.patch.object(mac_app,'run',side_effect=subprocess.CalledProcessError(1,['codesign'])):
            with self.assertRaises(subprocess.CalledProcessError):mac_app.publish_app(source,target)
        self.assertEqual((target/'Contents/fixture').read_text(),'old')
        self.assertFalse(list(target.parent.glob('.previous-super-melee-app-*')))
    def test_mac_install_rolls_back_when_final_verification_fails(self):
        source=self.app_fixture(self.root/'build','new');target=self.app_fixture(self.root/'Applications','old')
        def verify(*command):
            if command[-1]==target:raise subprocess.CalledProcessError(1,['codesign'])
        with mock.patch.object(mac_app,'clone',side_effect=lambda src,dst:shutil.copytree(src,dst)),mock.patch.object(mac_app,'run',side_effect=verify):
            with self.assertRaises(subprocess.CalledProcessError):mac_app.publish_app(source,target)
        self.assertEqual((target/'Contents/fixture').read_text(),'old')
        self.assertFalse(list(target.parent.glob('.previous-super-melee-app-*')))
    def test_mac_install_preserves_unrecognized_app_at_destination(self):
        source=self.app_fixture(self.root/'build','new');target=self.app_fixture(self.root/'Applications','unrelated','other.app')
        with mock.patch.object(mac_app,'clone',side_effect=AssertionError('An unrelated app must not be replaced')):
            with self.assertRaises(mac_app.MacAppInstallError):mac_app.publish_app(source,target)
        self.assertEqual((target/'Contents/fixture').read_text(),'unrelated')
    def test_mac_install_reports_copy_permissions_without_touching_existing_app(self):
        source=self.app_fixture(self.root/'build','new');target=self.app_fixture(self.root/'Applications','old')
        with mock.patch.object(mac_app,'clone',side_effect=PermissionError('fixture folder access')):
            with self.assertRaises(mac_app.MacAppInstallError) as error:mac_app.publish_app(source,target)
        self.assertEqual(diagnose(error.exception)['code'],'mac-app-install')
        self.assertEqual((target/'Contents/fixture').read_text(),'old')
    def test_packaged_startup_requires_gameplay_even_when_process_exits_zero(self):
        for output in ('SMB SELECT OPEN\n','SMB HOST FAIL dlopen: missing fixture.dylib\n','SMB HOST EMBED COMPLETE pid=1 host_frames=1 native_ticks=1\n'):
            def launch(command,**options):
                log=Path(options['env']['SMB_APP_SMOKE_ROOT'])/'Logs/latest.log';log.parent.mkdir()
                log.write_text(output)
                return subprocess.CompletedProcess(command,0,stdout='')
            with self.subTest(output=output),mock.patch.object(mac_app,'ROOT',self.root),mock.patch.object(mac_app.subprocess,'run',side_effect=launch):
                with self.assertRaises(mac_app.MacAppStartupError) as error:mac_app.verify_app_startup(self.root/'game.app')
                self.assertEqual(diagnose(error.exception)['code'],'mac-app-startup')
    def test_packaged_startup_uses_isolated_saves_and_rejects_environment_overrides(self):
        def launch(command,**options):
            self.assertEqual(command[-1],'--app-smoke')
            for key in ('SM64_MELEE_LIBRARY','MELEE_FILES_DIR','SMB_SMOKE_FRAMES','DYLD_LIBRARY_PATH'):
                self.assertNotIn(key,options['env'])
            root=Path(options['env']['SMB_APP_SMOKE_ROOT']);self.assertTrue(root.is_relative_to(self.root/'work'))
            log=root/'Logs/latest.log';log.parent.mkdir()
            log.write_text('SMB EMBED CONFIG host_owned=1 discovery=off\nSMB HOST NATIVE RENDER frame=120\nSMB HOST EMBED COMPLETE pid=1 host_frames=180 native_ticks=180\n')
            return subprocess.CompletedProcess(command,0,stdout='')
        with mock.patch.object(mac_app,'ROOT',self.root),mock.patch.dict(os.environ,{'SM64_MELEE_LIBRARY':'developer-library','MELEE_FILES_DIR':'developer-assets','SMB_SMOKE_FRAMES':'1','DYLD_LIBRARY_PATH':'external-libraries'}),mock.patch.object(mac_app.subprocess,'run',side_effect=launch):
            self.assertEqual(mac_app.verify_app_startup(self.root/'game.app')['frames'],180)
    @unittest.skipUnless(os.name!='nt' and shutil.which('clang'),'Native C fixture requires Clang on Mac or Linux')
    def test_embedded_engine_skips_standalone_launcher_discovery(self):
        source='''#include <stdio.h>
static int embedded,discovery;
char* getenv(const char* key){(void)key;return embedded?"host-library":NULL;}
void pc_launcher_configure(int* config){(void)config;discovery++;}
int main(int argc,char** argv){(void)argv;embedded=argc>1;int config=0;
    pc_launcher_configure(&config);
    return discovery;
}
'''
        patched=refresh_melee.host_configured_main(source)
        self.assertEqual(refresh_melee.host_configured_main(patched),patched)
        path=self.root/'main.c';path.write_text(patched);exe=self.root/'fixture'
        subprocess.run(['clang',str(path),'-o',str(exe)],check=True)
        self.assertEqual(subprocess.run([str(exe)],capture_output=True).returncode,1)
        self.assertEqual(subprocess.run([str(exe),'embedded'],capture_output=True).returncode,0)
    def test_mac_host_rejects_apple_make_before_build(self):
        with mock.patch.object(build_host.sys,'platform','darwin'),mock.patch.object(build_host.shutil,'which',side_effect=lambda name: '/usr/bin/make' if name=='make' else None):
            with self.assertRaisesRegex(RuntimeError,'brew install make'):build_host.make_program()
    def test_mac_host_prefers_gnu_make(self):
        with mock.patch.object(build_host.sys,'platform','darwin'),mock.patch.object(build_host.shutil,'which',side_effect=lambda name: '/opt/homebrew/bin/gmake' if name=='gmake' else '/usr/bin/make'):
            self.assertEqual(build_host.make_program(),'/opt/homebrew/bin/gmake')
    def test_mac_prerequisites_require_gmake_even_when_apple_make_exists(self):
        with mock.patch.object(setup.shutil,'which',side_effect=lambda name: None if name=='gmake' else '/fixture/'+name),mock.patch.object(setup,'mac_gcc',return_value='/fixture/gcc'),mock.patch.object(setup.importlib.util,'find_spec',return_value=True):
            self.assertIn('gmake',setup.preflight('mac'))
    def test_mac_preflight_catches_each_sound_tool_before_downloads(self):
        for missing in build_host.MAC_SOUND_TOOLS:
            with self.subTest(tool=missing),mock.patch.object(setup.shutil,'which',side_effect=lambda name: None if name==missing else '/fixture/'+name),mock.patch.object(setup,'mac_gcc',return_value='/fixture/gcc'),mock.patch.object(setup.importlib.util,'find_spec',return_value=True):
                self.assertEqual(setup.preflight('mac'),[missing])
                self.assertNotIn(missing,setup.preflight('windows'))
    def test_mac_builders_stop_before_compilation_without_sound_tools(self):
        with mock.patch.object(build_host.sys,'platform','darwin'),mock.patch.dict(os.environ,{'SMB_BUILD_TARGET':'mac'}),mock.patch.object(build_host,'ROOT',self.root),mock.patch.object(build_host,'make_program',return_value='fixture-gmake'),mock.patch.object(build_host.shutil,'which',return_value=None),mock.patch.object(build_host.subprocess,'run',side_effect=AssertionError('Build must not start with missing tools')):
            for builder in (build_host.build,build_coop.build):
                with self.subTest(builder=builder.__module__),self.assertRaisesRegex(RuntimeError,'brew install mingw-w64'):
                    builder()
    def test_native_mac_build_selects_all_three_sound_tools(self):
        with mock.patch.object(build_host.sys,'platform','darwin'),mock.patch.object(build_host.shutil,'which',side_effect=lambda name: '/fixture/'+name):
            self.assertEqual(build_host.mac_sound_tools(),[key+'=/fixture/'+name for key,name in zip(('AS','OBJCOPY','OBJDUMP'),build_host.MAC_SOUND_TOOLS)])
    def test_other_hosts_do_not_require_mac_sound_tools(self):
        for platform in ('linux','win32'):
            with self.subTest(platform=platform),mock.patch.object(build_host.sys,'platform',platform),mock.patch.object(build_host.shutil,'which',side_effect=AssertionError('Mac tool check on another host')):
                self.assertEqual(build_host.mac_sound_tools(),[])
    @unittest.skipUnless(os.name!='nt' and (shutil.which('gmake') or shutil.which('make')),'POSIX make fixture requires Mac or Linux')
    def test_host_build_generates_early_make_include_from_empty_directory(self):
        source=self.root/'work/sm64ex-source';source.mkdir(parents=True)
        # Like the pinned host's Makefile.split: its generated include is needed
        # before make reaches the later directory-creation step.
        (source/'Makefile').write_text('all: $(BUILD_DIR)/complete\n'
            '$(BUILD_DIR)/level_rules.mk:\n\tprintf "READY := yes\\n" > $@\n'
            'include $(BUILD_DIR)/level_rules.mk\n'
            '$(BUILD_DIR)/complete:\n\ttest "$(READY)" = yes\n\ttouch $@\n')
        with mock.patch.object(build_host,'ROOT',self.root),mock.patch.dict(os.environ,{'SMB_BUILD_TARGET':'mac'}),mock.patch.object(build_host,'mac_sound_tools',return_value=[]):
            build_host.build()
            output=source/'build/us_melee_bridge'
            self.assertTrue((output/'complete').is_file())
            marker=output/'keep-existing-output';marker.write_text('preserve')
            build_host.build()
            self.assertEqual(marker.read_text(),'preserve')
    def test_windows_host_build_creates_separate_output_before_dependencies(self):
        source=self.root/'work/sm64ex-source';source.mkdir(parents=True)
        sentinel=source/'build/us_melee_bridge/keep';sentinel.parent.mkdir(parents=True);sentinel.write_text('mac output')
        def dependencies():
            self.assertTrue((source/'build/us_melee_bridge_windows').is_dir())
            self.assertEqual(sentinel.read_text(),'mac output')
            raise RuntimeError('fixture: stop before downloads')
        with mock.patch.object(build_host,'ROOT',self.root),mock.patch.dict(os.environ,{'SMB_BUILD_TARGET':'windows'}),mock.patch.object(build_host,'make_program',return_value='fixture-make'),mock.patch.object(build_host,'prepare_windows',side_effect=dependencies):
            with self.assertRaisesRegex(RuntimeError,'fixture: stop before downloads'):build_host.build()
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
    def test_diagnostic_repair_cases(self):
        cases=[
            ('Setup needs at least 12.0 GiB free in the build folder; 8.3 GiB is available.','disk-space','3.7 GiB'),
            ('cc1: fatal error: opening output file build/us_melee_bridge/level_rules.mk: No such file or directory','gnu-make','brew install make'),
            ('Install the prerequisites in START HERE.txt: gmake','gnu-make','Prepare Build Tools'),
            ('SM64: use the original US big-endian .z64 ROM (8 MiB).','sm64-image','.z64'),
            ('Melee: use an unmodified USA Rev 2 GameCube disc image','melee-image','USA Rev 2'),
            ('Choose a build folder without spaces','build-path','Build Folder'),
            ('The build folder is not writable','folder-access','owned by your account'),
            ('No space left on device','disk-full','20 GiB'),
            ('The Mac target currently requires Apple Silicon and macOS 15+.','mac-platform','Intel'),
            ('Setup is already running for this build folder','setup-running','Cancel'),
            ('SSL certificate problem','download-certificate','Keep certificate verification enabled'),
            ('curl: (6) Could not resolve host','download-network','connection'),
            ('Preserving a different existing local input','existing-files','empty build folder'),
            ('Dependency checksum mismatch','download-integrity','do not disable'),
            ('The complete source kit is missing','incomplete-kit','Extract'),
            ('Unrecognised failure','setup-failed','Open Setup Log')]
        for message,code,repair in cases:
            with self.subTest(code=code,message=message):
                value=diagnose(RuntimeError(message),version='fixture')
                self.assertEqual(value['code'],code);self.assertIn(repair,value['recovery'])
                self.assertIn('Error code: '+code,format_diagnostic(value));self.assertEqual(value['version'],'fixture')
    def test_child_error_retains_only_current_bounded_output(self):
        with self.assertRaises(subprocess.CalledProcessError) as raised:
            with self.session() as session:
                session.run([sys.executable,'-c','print("old sys/types.h: No such file")'])
                session.run([sys.executable,'-c','print("x"*20000); print("curl: (6) Could not resolve host"); raise SystemExit(7)'])
        self.assertLessEqual(len(raised.exception.output),16000)
        self.assertNotIn('sys/types.h',raised.exception.output)
        stage=dict(number=3,total=12,label='Compile native engine')
        value=diagnose(raised.exception,stage=stage)
        self.assertEqual(value['code'],'download-network');self.assertEqual(value['stage'],stage)
        self.assertIn('step 3/12',value['message'])
    def test_early_failure_is_saved_before_backend_log_opens(self):
        error=RuntimeError('Setup needs at least 12.0 GiB free in the build folder; 8.3 GiB is available.')
        with mock.patch.object(setup,'ROOT',self.root),mock.patch.object(setup,'main',side_effect=error),mock.patch.object(setup,'CURRENT_STAGE',None),mock.patch.object(sys,'argv',['Setup.py']),contextlib.redirect_stderr(io.StringIO()) as output:
            self.assertEqual(setup.entrypoint(),1)
        value=json.loads((self.root/'work/setup-error.json').read_text())
        self.assertEqual(value['code'],'disk-space');self.assertIn('3.7 GiB',value['recovery'])
        self.assertIn('Error code: disk-space',output.getvalue());self.assertFalse((self.root/'setup.log').exists())
    def test_check_failure_does_not_write_error_file(self):
        with mock.patch.object(setup,'ROOT',self.root),mock.patch.object(setup,'main',side_effect=ValueError('bad input')),mock.patch.object(sys,'argv',['Setup.py','--check']),contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(setup.entrypoint(),1)
        self.assertFalse((self.root/'work').exists())
    def test_generic_build_error_reports_compiler_reason_and_stage(self):
        error=subprocess.CalledProcessError(2,['fixture-build'],output='a command\nfile.c:23: fatal error: missing fixture.h\nmake: *** [object] Error 1')
        value=diagnose(error,stage=dict(number=8,total=12,label='Compile single-player'))
        self.assertEqual(value['code'],'build-failed');self.assertIn('missing fixture.h',value['message']);self.assertIn('step 8/12',value['message'])
    def test_missing_sound_tool_survives_parallel_output_and_python_traceback(self):
        for tool in build_host.MAC_SOUND_TOOLS:
            with self.subTest(tool=tool):
                output='compile sound data\ngmake: '+tool+': No such file or directory\ngmake: *** [sound.o] Error 127\ngmake: *** Waiting for unfinished jobs....\n'+('encode another sample\n'*80)+'Traceback (most recent call last):\n  File "build.py", line 20, in build\nsubprocess.CalledProcessError: Command returned non-zero exit status 2.\n'
                error=subprocess.CalledProcessError(1,['fixture-build'],output=output)
                value=diagnose(error,stage=dict(number=8,total=12,label='Compile single-player'))
                self.assertEqual(value['code'],'mac-sound-tools');self.assertIn('brew install mingw-w64',value['recovery'])
                self.assertIn('step 8/12',value['message']);self.assertNotIn('CalledProcessError',value['message'])
    def test_compiler_error_is_preferred_to_nested_python_wrapper(self):
        output='file.c:42: error: fixture type mismatch\nmake: *** [object] Error 1\nTraceback (most recent call last):\nsubprocess.CalledProcessError: Command returned non-zero exit status 2.'
        value=diagnose(subprocess.CalledProcessError(1,['fixture-build'],output=output),stage=dict(number=8,total=12,label='Compile single-player'))
        self.assertIn('fixture type mismatch',value['message']);self.assertNotIn('CalledProcessError',value['message'])
    def test_assembler_input_error_does_not_report_a_missing_executable(self):
        output='i686-w64-mingw32-as -o sound.o sound.s\nsound.s:3: Error: missing fixture include: No such file or directory\nmake: *** [object] Error 1'
        value=diagnose(subprocess.CalledProcessError(1,['fixture-build'],output=output),stage=dict(number=8,total=12,label='Compile single-player'))
        self.assertEqual(value['code'],'build-failed');self.assertIn('missing fixture include',value['message'])
    def test_missing_generic_tool_is_preferred_to_nested_python_wrapper(self):
        output='gmake: fixture-assembler: No such file or directory\nmake: *** [object] Error 127\nsubprocess.CalledProcessError: Command returned non-zero exit status 2.'
        value=diagnose(subprocess.CalledProcessError(1,['fixture-build'],output=output),stage=dict(number=8,total=12,label='Compile single-player'))
        self.assertIn('fixture-assembler',value['message']);self.assertNotIn('CalledProcessError',value['message'])
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
        with mock.patch.object(setup,'ROOT',self.root),mock.patch.object(sys,'argv',['Setup.py','--check-tools']),mock.patch.object(setup,'preflight',return_value=[]),mock.patch.object(setup,'mac_toolchain_report',return_value={'ok':True}),contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(setup.main(),0)
        report=json.loads(output.getvalue());self.assertIn('missing',report);self.assertIn('filesystem',report)
        self.assertFalse((self.root/'work').exists());self.assertFalse((self.root/'setup.log').exists())
    def test_check_tools_fails_for_missing_tools_on_either_target(self):
        for target,missing in [('mac',['i686-w64-mingw32-as']),('windows',['x86_64-w64-mingw32-gcc'])]:
            with self.subTest(target=target),mock.patch.object(setup,'ROOT',self.root),mock.patch.object(sys,'argv',['Setup.py','--check-tools','--platform',target]),mock.patch.object(setup,'preflight',return_value=missing),mock.patch.object(setup,'mac_toolchain_report',return_value={'ok':True}),contextlib.redirect_stdout(io.StringIO()) as output:
                self.assertEqual(setup.main(),2)
                self.assertEqual(json.loads(output.getvalue())['missing'],missing)
        self.assertFalse((self.root/'work').exists())
    def test_mac_sdk_and_compilers_are_explicit_with_spaces_and_stale_environment(self):
        sdk=self.root/'Apple SDK/MacOSX.sdk';header=sdk/'usr/include/sys/types.h'
        header.parent.mkdir(parents=True);header.write_text('/* fixture header */')
        original={'PATH':'/fixture/tools','SDKROOT':'/removed/old-sdk','KEEP':'unchanged','MACOSX_DEPLOYMENT_TARGET':'27.0'}
        calls=[]
        def probe(command,environment,source=None):
            calls.append((command,dict(environment),source))
            if command[0].endswith('xcode-select'):return '/fixture/Developer'
            if command[0].endswith('xcrun'):return str(sdk)
            return ''
        with mock.patch.object(mac,'_probe',side_effect=probe),mock.patch.object(mac,'mac_gcc',return_value='/fixture/gcc-16'):
            environment,report=mac.mac_build_environment(original)
        self.assertEqual(original['SDKROOT'],'/removed/old-sdk');self.assertEqual(environment['KEEP'],'unchanged')
        self.assertEqual(environment['SDKROOT'],str(sdk));self.assertEqual(environment['GCC_BIN'],'/fixture/gcc-16')
        self.assertEqual(environment['MACOSX_DEPLOYMENT_TARGET'],'15.0');self.assertEqual(original['MACOSX_DEPLOYMENT_TARGET'],'27.0')
        self.assertNotIn('SDKROOT',calls[0][1]);self.assertNotIn('SDKROOT',calls[1][1])
        self.assertEqual(len(calls),4)
        for command,env,source in calls[2:]:
            self.assertEqual(command[command.index('-isysroot')+1],str(sdk))
            self.assertIn('-fsyntax-only',command);self.assertEqual(command[-1],'-');self.assertIn('sys/types.h',source)
            self.assertEqual(env['GCC_BIN'],'/fixture/gcc-16')
        self.assertTrue(report['c_headers_checked'] and report['cxx_headers_checked'])
    def test_mac_missing_sdk_stops_before_any_compiler(self):
        with mock.patch.object(mac,'_probe',side_effect=['/fixture/Developer','/missing/sdk']) as probe:
            with self.assertRaises(mac.MacToolchainError) as failure:mac.mac_build_environment({})
        self.assertEqual(probe.call_count,2);self.assertEqual(failure.exception.diagnostic['code'],'mac-sdk')
        self.assertIn('Build / Resume',str(failure.exception))
    def test_mac_gcc_selection_matches_upstream_and_honours_override(self):
        def which(name,path):return '/fixture/'+name if name in ('gcc-16','gcc-15','my-gcc') else None
        with mock.patch.object(mac.shutil,'which',side_effect=which):
            self.assertEqual(mac.mac_gcc({'PATH':'/fixture'}),str(Path('/fixture/gcc-16').absolute()))
            self.assertEqual(mac.mac_gcc({'PATH':'/fixture','GCC_BIN':'my-gcc'}),str(Path('/fixture/my-gcc').absolute()))
            self.assertIsNone(mac.mac_gcc({'PATH':'/fixture','GCC_BIN':'missing'}))
    def test_mac_failed_header_check_and_license_are_actionable(self):
        for output,code in [('fatal error: sys/types.h: No such file or directory','mac-compiler'),
                            ('You have not agreed to the Xcode license','xcode-license')]:
            failure=mac.compiler_problem(output,'gcc-16')
            self.assertEqual(failure.diagnostic['code'],code)
            self.assertIn('Build / Resume',failure.diagnostic['recovery'])
            self.assertIn(output,str(failure))
    def test_mac_timeout_becomes_readable_error(self):
        with mock.patch.object(mac.subprocess,'run',side_effect=subprocess.TimeoutExpired(['compiler'],30)):
            with self.assertRaises(mac.MacToolchainError):mac._probe(['compiler'],{})
    def test_mac_preflight_failure_has_no_downloads_and_saves_gui_diagnostic(self):
        # Exercise the real main ordering without game images or external tools.
        failure=mac.compiler_problem('fatal error: sys/types.h: No such file or directory','gcc-16')
        with mock.patch.object(setup,'ROOT',self.root),mock.patch.object(sys,'argv',['Setup.py','--platform','mac','--rom','fixture.z64','--melee','fixture.iso']), \
             mock.patch.object(setup,'verify_inputs',return_value={}),mock.patch.object(setup,'preflight',return_value=[]), \
             mock.patch.object(setup,'verify_bootstrap',return_value={'revision':'fixture','files':{}}),mock.patch.object(setup,'digest',return_value='fixture'), \
             mock.patch.object(setup,'require_filesystem'),mock.patch.object(setup.sys,'platform','darwin'), \
             mock.patch.object(setup.platform,'machine',return_value='arm64'),mock.patch.object(setup.platform,'mac_ver',return_value=('15.0','','')), \
             mock.patch.object(setup,'mac_build_environment',side_effect=failure),mock.patch.object(setup,'install') as install:
            with self.assertRaises(mac.MacToolchainError):setup.main()
        install.assert_not_called();self.assertEqual(self.state()['status'],'failed')
        self.assertEqual(json.loads((self.root/'work/setup-error.json').read_text())['code'],'mac-compiler')
        self.assertIn('sys/types.h',(self.root/'setup.log').read_text())
        self.assertFalse((self.root/'work/setup.lock').exists())
    def test_mac_sdk_override_migrates_verified_adapter_and_preserves_other_edits(self):
        adapter=self.root/'adapter';setup.extract_bootstrap(setup.BOOTSTRAP,adapter)
        native=adapter/'tools/melee3d/native_mac.py';original=native.read_text()
        setup.configure_local_sources(adapter)
        self.assertIn('"-DCMAKE_OSX_SYSROOT=" + os.environ.get("SDKROOT", "macosx")',native.read_text())
        self.assertNotEqual(native.read_text(),original)
        setup.extract_bootstrap(setup.BOOTSTRAP,adapter) # Resume verifies the reversible override.
        setup.configure_local_sources(adapter)
        self.assertEqual(native.read_text().count('"-DCMAKE_OSX_SYSROOT="'),1)
        native.write_text(native.read_text()+'\n# custom change\n')
        with self.assertRaisesRegex(RuntimeError,'Preserving changed'):setup.extract_bootstrap(setup.BOOTSTRAP,adapter)
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

@unittest.skipUnless(sys.platform=='darwin','Compiled Mac helper requires AppKit')
class MacHelperTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build=tempfile.TemporaryDirectory(prefix='supermelee-mac-helper-')
        cls.helper=Path(cls.build.name)/'InstallSuperMelee64'
        with mock.patch.dict(os.environ,{'MACOSX_DEPLOYMENT_TARGET':'27.0'}):
            mac_helper.compile_helper(Path(__file__).with_name('InstallHelper.swift'),cls.helper)
    @classmethod
    def tearDownClass(cls):cls.build.cleanup()
    def test_compiled_helper_targets_macos_15_despite_newer_builder_environment(self):
        self.assertEqual(mac_helper.verify_binary(self.helper),dict(architecture='arm64',minimum_macos='15.0'))
    def test_newer_binary_minimum_is_rejected(self):
        # Change only the real compiled load-command minimum, without executing
        # the fixture. This models the accidentally published beta.7 binary.
        data=bytearray(self.helper.read_bytes());offset=32
        import struct
        for _ in range(struct.unpack_from('<I',data,16)[0]):
            command,length=struct.unpack_from('<2I',data,offset)
            if command==0x32:
                struct.pack_into('<I',data,offset+12,27<<16);break
            offset+=length
        else:self.fail('Missing compiler deployment-target load command')
        changed=Path(self.build.name)/'newer-minimum';changed.write_bytes(data)
        with self.assertRaisesRegex(ValueError,'macOS 15.0 exactly'):mac_helper.verify_binary(changed)
    def test_same_size_same_timestamp_upgrade_replaces_source_and_bytecode(self):
        with tempfile.TemporaryDirectory(prefix='supermelee-upgrade-') as folder:
            root=Path(folder);source=root/'new kit';target=root/'existing build/outputs/sm64-melee-lab'
            for base in (source,target):
                for name in ('native','release','branding'):(base/name).mkdir(parents=True)
                for name in ('native/version.py','release/Setup.py'):
                    path=base/name;path.write_text("VERSION='old'\n");os.utime(path,(1700000000,1700000000))
            # Populate real, valid timestamp-based bytecode for the old source.
            for directory,module in [('native','version'),('release','Setup')]:
                subprocess.run([sys.executable,'-c','import '+module],cwd=target/directory,env=dict(os.environ,PYTHONDONTWRITEBYTECODE=''),check=True)
                self.assertTrue((target/directory/'__pycache__').is_dir())
            for name in ('native/version.py','release/Setup.py'):
                path=source/name;path.write_text("VERSION='new'\n");os.utime(path,(1700000000,1700000000))
            marker=root/'existing build/work/verified-download';marker.parent.mkdir();marker.write_text('keep')
            venv=root/'existing build/.venv/keep';venv.parent.mkdir();venv.write_text('keep')
            subprocess.run([str(self.helper),'--sync-source-kit',str(source),str(target)],check=True)
            for directory,module in [('native','version'),('release','Setup')]:
                self.assertFalse((target/directory/'__pycache__').exists())
                result=subprocess.check_output([sys.executable,'-c','import '+module+'; print('+module+'.VERSION)'],cwd=target/directory,text=True)
                self.assertEqual(result.strip(),'new')
            self.assertEqual(marker.read_text(),'keep');self.assertEqual(venv.read_text(),'keep')
    def test_incomplete_source_kit_is_rejected_before_copy(self):
        with tempfile.TemporaryDirectory(prefix='supermelee-incomplete-') as folder:
            root=Path(folder);target=root/'existing';target.mkdir()
            marker=target/'keep';marker.write_text('keep')
            result=subprocess.run([str(self.helper),'--sync-source-kit',str(root/'missing'),str(target)],capture_output=True,text=True)
            self.assertNotEqual(result.returncode,0);self.assertIn('complete source kit is missing',result.stderr)
            self.assertEqual(list(target.iterdir()),[marker])
    def test_compiled_helper_recognizes_the_sound_tool_failure(self):
        with tempfile.TemporaryDirectory(prefix='supermelee-diagnostic-') as folder:
            output=Path(folder)/'output';output.write_text('gmake: i686-w64-mingw32-as: No such file or directory')
            result=subprocess.check_output([str(self.helper),'--diagnose-output',str(output)],text=True)
            self.assertEqual(json.loads(result)['code'],'mac-sound-tools')
    def test_compiled_helper_requires_a_real_installed_app_before_reporting_success(self):
        with tempfile.TemporaryDirectory(prefix='supermelee-installed-') as folder:
            root=Path(folder);app=root/'Applications/Super Melee 64.app';contents=app/'Contents'
            (contents/'MacOS').mkdir(parents=True)
            (contents/'Info.plist').write_bytes(plistlib.dumps(dict(CFBundleIdentifier='dev.supermelee64.launcher',CFBundleExecutable='Game',CFBundlePackageType='APPL')))
            executable=contents/'MacOS/Game';executable.write_text('#!/bin/sh\nexit 0\n');executable.chmod(0o755)
            report=root/'native-app-build.json'
            report.write_text(json.dumps(dict(app=str(app),installed=True,signature_verified=True,startup_verified=True)))
            result=subprocess.check_output([str(self.helper),'--installed-app',str(report)],text=True)
            self.assertEqual(result.strip(),str(app))
            for values in [dict(installed=False,signature_verified=True,startup_verified=True),dict(installed=True,signature_verified=False,startup_verified=True),dict(installed=True,signature_verified=True,startup_verified=False)]:
                report.write_text(json.dumps(dict(app=str(app),**values)))
                self.assertEqual(subprocess.run([str(self.helper),'--installed-app',str(report)]).returncode,2)
            report.write_text(json.dumps(dict(app=str(app),installed=True,signature_verified=True,startup_verified=True)))
            executable.unlink()
            self.assertEqual(subprocess.run([str(self.helper),'--installed-app',str(report)]).returncode,2)
    def library_fixture(self,root,token):
        vendor=root/'vendor prefix';lib=vendor/'lib';lib.mkdir(parents=True)
        (vendor/'bin').mkdir()
        leaf=lib/'libleaf.1.dylib';middle=lib/'libmiddle.1.dylib';original=vendor/'bin/game'
        (root/'leaf.c').write_text('int leaf(void){return 40;}\n')
        (root/'middle.c').write_text('extern int leaf(void);int middle(void){return leaf()+2;}\n')
        (root/'main.c').write_text('extern int middle(void);int main(void){return middle()!=42;}\n')
        subprocess.run(['clang','-dynamiclib',str(root/'leaf.c'),'-o',str(leaf),'-Wl,-install_name,'+token+'/libleaf.1.dylib'],check=True)
        subprocess.run(['clang','-dynamiclib',str(root/'middle.c'),str(leaf),'-o',str(middle),'-Wl,-install_name,'+str(middle),'-Wl,-rpath,@loader_path/../lib','-Wl,-rpath,.'],check=True)
        subprocess.run(['clang',str(root/'main.c'),str(middle),'-o',str(original),'-Wl,-rpath,'+str(lib)],check=True)
        app=root/'Relocated.app';frameworks=app/'Contents/Frameworks';frameworks.mkdir(parents=True)
        exe=app/'Contents/MacOS/game';exe.parent.mkdir();shutil.copy2(original,exe)
        return vendor,leaf,original,exe,frameworks
    def test_bundled_transitive_libraries_run_without_the_original_installation(self):
        for token in ('@rpath','@loader_path','@executable_path/../lib'):
            with self.subTest(token=token),tempfile.TemporaryDirectory(prefix='supermelee-dylibs-') as folder:
                vendor,leaf,original,exe,frameworks=self.library_fixture(Path(folder),token)
                libraries=mac_app.bundle_libraries(exe,frameworks,origins={exe:original})
                self.assertEqual(libraries,['libleaf.1.dylib','libmiddle.1.dylib'])
                mac_app.run('codesign','--force','--sign','-',exe)
                vendor.rename(vendor.with_name('original-installation-removed'))
                environment={key:value for key,value in os.environ.items() if not key.startswith('DYLD_')}
                subprocess.run([str(exe)],env=environment,check=True)
    def test_missing_transitive_library_stops_packaging(self):
        with tempfile.TemporaryDirectory(prefix='supermelee-missing-dylib-') as folder:
            vendor,leaf,original,exe,frameworks=self.library_fixture(Path(folder),'@rpath')
            leaf.unlink()
            with self.assertRaisesRegex(ValueError,'Unresolved native library @rpath/libleaf'):
                mac_app.bundle_libraries(exe,frameworks,origins={exe:original})

if __name__=='__main__':unittest.main(verbosity=2)
