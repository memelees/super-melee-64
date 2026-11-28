"""Regression tests for the native Windows installer's destructive boundaries."""
from pathlib import Path
import hashlib
import io
import json
import os
import shutil
import subprocess
import tempfile
import tarfile
import unittest
from unittest.mock import patch
import zipfile
import windows_curl
from windows_dependencies import listing_member,safe_member,unzip
from windows_setup import copy_sources,source_files,verify_tools
from setup_diagnostics import diagnose
from windows_extract import extract
from windows_native_build import build_drive

HANDOFF_FIXTURE=r'''
#include <windows.h>
#include <string>
#include <cwchar>
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR args,int){
 if(wcscmp(args,L"child")==0){
  Sleep(700);wchar_t marker[32768];GetEnvironmentVariableW(L"SMB_TEST_MARKER",marker,32768);
  HANDLE out=CreateFileW(marker,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,0,nullptr);
  if(out==INVALID_HANDLE_VALUE)return 3;DWORD written;WriteFile(out,"alive",5,&written,nullptr);CloseHandle(out);return 0;
 }
 wchar_t path[32768];GetModuleFileNameW(nullptr,path,32768);
 std::wstring cmd=L"\""+std::wstring(path)+L"\" child";
 STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};
 if(!CreateProcessW(path,cmd.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi))return 2;
 CloseHandle(pi.hThread);CloseHandle(pi.hProcess);return 0;
}
'''

def check_handoff(frontend,launcher,fixture,root):
    """Use real Windows processes, with a fake game and no copyrighted inputs."""
    root=Path(root);workspace=root/'Workspace';game=workspace/'work/SuperMelee64-Windows/SuperMelee64.exe';game.parent.mkdir(parents=True)
    shutil.copy2(launcher,workspace/'Super Melee 64.exe');shutil.copy2(fixture,game)
    (root/'ready.json').write_text('{}');(root/'installed-version.txt').write_text('development')
    (workspace/'dummy').write_bytes(b'asset-free fixture')
    keys=('SM64_MELEE_LIBRARY','SM64_MELEE_DISC','SM64_MELEE_RESOURCES','SMB_SM64_ROM','SMB_SELECTION_ART')
    marker=root/'replacement-survived.txt'
    (workspace/'SuperMelee64.ini').write_text('[Environment]\n'+''.join(k+'=${ROOT}/dummy\n' for k in keys)+'SMB_TEST_MARKER='+str(marker)+'\n',encoding='utf-16')
    result=subprocess.run([str(frontend),'--data',str(root)],timeout=20)
    if result.returncode!=0 or not marker.is_file() or marker.read_bytes()!=b'alive':
        raise AssertionError('The native launcher killed or failed to wait for its replacement game process')

class WindowsSetupTests(unittest.TestCase):
    def test_cancelled_sound_pair_can_resume_without_touching_complete_pairs(self):
        import sys
        sys.path.insert(0,str(Path(__file__).parent.parent/'native'))
        from build_host import repair_sound_byproducts
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);sound=root/'sound';sound.mkdir()
            (sound/'sequences.bin').write_bytes(b'left by failed build')
            (sound/'sound_data.ctl').write_bytes(b'complete');(sound/'sound_data.tbl').write_bytes(b'complete')
            repair_sound_byproducts(root)
            self.assertFalse((sound/'sequences.bin').exists());self.assertEqual((sound/'sound_data.ctl').read_bytes(),b'complete')
    def test_bundled_adapter_overrides_are_idempotent_and_reversible(self):
        import sys
        sys.path.insert(0,str(Path(__file__).parent.parent/'native'))
        from build_bootstrap import extract,configure_local_sources
        with tempfile.TemporaryDirectory() as d:
            root=Path(d)/'adapter';archive=Path(__file__).parent/'SmashRoyale-bootstrap.zip'
            extract(archive,root);configure_local_sources(root)
            before={p.relative_to(root):p.read_bytes() for p in root.rglob('*') if p.is_file()}
            configure_local_sources(root)
            self.assertEqual(before,{p.relative_to(root):p.read_bytes() for p in root.rglob('*') if p.is_file()})
            extract(archive,root) # Must round-trip exactly to every pinned source hash.
    @unittest.skipUnless(os.name=='nt','Uses Windows short names and per-user drive mappings')
    def test_unicode_build_folder_uses_a_valid_ascii_alias_and_removes_mapping(self):
        with tempfile.TemporaryDirectory(prefix='SM64 Unicode é ') as d:
            root=Path(d)/'folder with spaces';root.mkdir()
            with build_drive(root) as alias:
                self.assertTrue(str(alias).isascii());self.assertFalse(any(c.isspace() for c in str(alias)))
                self.assertTrue(os.path.samefile(alias,root));(alias/'verified.txt').write_bytes(b'local')
            self.assertEqual((root/'verified.txt').read_bytes(),b'local')
    def test_tar_internal_links_are_copied_and_escaping_links_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);archive=root/'safe.tar'
            with tarfile.open(archive,'w') as t:
                entry=tarfile.TarInfo('pkg/tool');entry.size=4;t.addfile(entry,io.BytesIO(b'data'))
                link=tarfile.TarInfo('pkg/alias');link.type=tarfile.SYMTYPE;link.linkname='tool';t.addfile(link)
            extract(archive,root/'out');self.assertEqual((root/'out/pkg/alias').read_bytes(),b'data');self.assertFalse((root/'out/pkg/alias').is_symlink())
            with tarfile.open(archive,'w') as t:
                link=tarfile.TarInfo('pkg/alias');link.type=tarfile.SYMTYPE;link.linkname='../../escape';t.addfile(link)
            with self.assertRaises(ValueError):extract(archive,root/'blocked')
    def test_windows_crash_is_not_misreported_as_optional_package_missing(self):
        error=subprocess.CalledProcessError(0xc0000005,['cmake'],output="-- Optional package 'zlib' not found")
        report=diagnose(error,version='test')
        self.assertEqual(report['code'],'native-tool-crash');self.assertIn('0xC0000005',report['message'])
    @unittest.skipUnless(os.name=='nt','Requires the native Windows CI toolchain')
    def test_actual_native_frontend_compiles_and_executes(self):
        vswhere=Path(os.environ.get('ProgramFiles(x86)','C:/Program Files (x86)'))/'Microsoft Visual Studio/Installer/vswhere.exe'
        if not vswhere.is_file():self.skipTest('Visual Studio build tools are not installed on this developer machine')
        installation=subprocess.check_output([str(vswhere),'-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],text=True).strip()
        self.assertTrue(installation,'Windows CI must have the native C++ toolchain')
        with tempfile.TemporaryDirectory(prefix='SM64 setup Unicode é ') as d:
            root=Path(d);source=Path(__file__).parent/'WindowsInstaller.cpp';exe=root/'SuperMelee64.exe';batch=root/'compile.cmd'
            fixture=root/'handoff.cpp';fixture.write_text(HANDOFF_FIXTURE)
            legacy=source.parent/'WindowsLauncher.cpp'
            batch.write_text('@echo off\nchcp 65001 >nul\ncall "'+installation+'\\Common7\\Tools\\VsDevCmd.bat" -arch=x64 -host_arch=x64\nif errorlevel 1 exit /b 1\n'+
                'cl /nologo /utf-8 /std:c++17 /EHsc /O2 /MT /DUNICODE /D_UNICODE "'+str(source)+'" /Fe:"'+str(exe)+'" shell32.lib ole32.lib uuid.lib user32.lib gdi32.lib comctl32.lib comdlg32.lib /link /SUBSYSTEM:WINDOWS\nif errorlevel 1 exit /b 1\n'+
                'cl /nologo /utf-8 /std:c++17 /EHsc /O2 /MT /DUNICODE /D_UNICODE "'+str(legacy)+'" /Fe:"'+str(root/'legacy.exe')+'" shell32.lib ole32.lib uuid.lib user32.lib /link /SUBSYSTEM:WINDOWS\nif errorlevel 1 exit /b 1\n'+
                'cl /nologo /utf-8 /std:c++17 /EHsc /O2 /MT /DUNICODE /D_UNICODE "'+str(fixture)+'" /Fe:"'+str(root/'fixture.exe')+'" /link /SUBSYSTEM:WINDOWS\nexit /b %ERRORLEVEL%\n',encoding='utf-8')
            result=subprocess.run(['cmd.exe','/c',str(batch)],cwd=root,capture_output=True,text=True,errors='replace',timeout=120)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertEqual(subprocess.run([str(exe),'--self-test'],timeout=15).returncode,0)
            self.assertEqual(subprocess.run([str(exe),'--ui-self-test','--data',str(root/'ui-test')],timeout=15).returncode,0)
            # The CI account is disposable. Do not create RoamingAppData profile
            # folders when a developer runs this test on their personal Windows PC.
            if os.environ.get('GITHUB_ACTIONS')=='true':check_handoff(exe,root/'legacy.exe',root/'fixture.exe',root/'handoff-data')
    def source(self,path,content):
        path.mkdir(exist_ok=True);(path/'README.md').write_bytes(content)
        manifest=dict(source_only=True,git_history_included=False,files={'README.md':hashlib.sha256(content).hexdigest()})
        (path/'PUBLIC-SOURCE-MANIFEST.json').write_text(json.dumps(manifest))
    def test_update_preserves_inputs_saves_and_unknown_files(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);source=root/'source';work=root/'work';self.source(source,b'one');copy_sources(source,work)
            (work/'save.bin').write_bytes(b'save');(work/'Melee.iso').write_bytes(b'owned')
            self.source(source,b'two');copy_sources(source,work)
            self.assertEqual((work/'README.md').read_bytes(),b'two')
            self.assertEqual((work/'save.bin').read_bytes(),b'save');self.assertEqual((work/'Melee.iso').read_bytes(),b'owned')
    def test_windows_line_endings_do_not_break_resuming_a_known_source(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);source=root/'source';work=root/'work';self.source(source,b'one\ntwo\n');copy_sources(source,work)
            (work/'README.md').write_bytes(b'one\r\ntwo\r\n');self.source(source,b'new\n');copy_sources(source,work)
            self.assertEqual((work/'README.md').read_bytes(),b'new\n')
    def test_edited_source_conflict_is_preserved_before_any_replacement(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);source=root/'source';work=root/'work';self.source(source,b'one');copy_sources(source,work)
            (work/'README.md').write_bytes(b'custom edit');self.source(source,b'two')
            with self.assertRaisesRegex(RuntimeError,'Preserving edited'):copy_sources(source,work)
            self.assertEqual((work/'README.md').read_bytes(),b'custom edit')
    def test_changed_source_or_tool_manifest_is_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);self.source(root,b'one');(root/'README.md').write_bytes(b'changed')
            with self.assertRaisesRegex(RuntimeError,'verification'):source_files(root)
            tools=root/'Tools';tools.mkdir();(tools/'manifest.json').write_text(json.dumps(dict(files={'../escape':'0'*64})))
            with self.assertRaises(ValueError):verify_tools(root)
    def test_archive_cannot_escape_or_use_windows_device_names(self):
        for name in ('../x','/x','C:/x','foo\\x','NUL.txt','x/CON','x/COM9.exe','x. /y','x /y'):
            with self.subTest(name=name),self.assertRaises(ValueError):safe_member(name)
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);archive=root/'bad.zip'
            with zipfile.ZipFile(archive,'w') as z:z.writestr('../escape',b'x')
            with self.assertRaises(ValueError):unzip(archive,root/'out')
            self.assertFalse((root/'escape').exists())
    def test_seven_zip_native_listing_paths_are_validated_after_normalisation(self):
        # 7-Zip prints "mingw64\bin\gcc.exe" on Windows, though the archive
        # member is a plain relative path. Normalise before judging it.
        for name in ('mingw64\\bin\\gcc.exe','cmake-3.31.8-windows-x86_64/bin/cmake.exe','ninja.exe'):
            with self.subTest(name=name):
                self.assertNotIn('\\',str(listing_member(name)))
        for name in ('..\\x','C:\\x','\\\\server\\share\\x','\\x','x\\NUL','x\\..'):
            with self.subTest(name=name),self.assertRaises(ValueError):listing_member(name)
    def test_status_ui_reports_a_live_heartbeat_during_silent_steps(self):
        import time
        import windows_setup
        with tempfile.TemporaryDirectory() as d:
            windows_setup.publish_ui(d,'Compile the native fighter engine',30)
            self.assertEqual(Path(d,'setup-ui.status').read_text().splitlines(),
                             ['30','Compile the native fighter engine'])
            try:
                windows_setup.start_status_heartbeat(0.05)
                deadline=time.monotonic()+5
                while time.monotonic()<deadline:
                    if 'still working' in Path(d,'setup-ui.status').read_text():
                        break
                    time.sleep(0.05)
                lines=Path(d,'setup-ui.status').read_text().splitlines()
            finally:
                windows_setup.STATUS_STOP.set()
            self.assertEqual(lines[0],'30')
            self.assertTrue(lines[1].startswith('Compile the native fighter engine (still working, '),lines[1])
    def test_https_options_cannot_disable_tls_or_run_commands(self):
        for args in (['-k','https://example.test','-o','x'],['http://example.test','-o','x'],['--proto','=all','https://example.test','-o','x']):
            with self.assertRaises(ValueError):windows_curl.options(args)
        options=windows_curl.options(['-fL','--continue-at','-','--max-filesize','42','-o','x','https://example.test'])
        self.assertTrue(options['resume']);self.assertEqual(options['limit'],42)
    def test_ignored_http_range_restarts_instead_of_appending(self):
        class Response(io.BytesIO):
            url='https://example.test/file';status=200
        with tempfile.TemporaryDirectory() as d:
            target=Path(d)/'download';target.write_bytes(b'old partial')
            with patch('urllib.request.urlopen',return_value=Response(b'new whole file')):
                self.assertEqual(windows_curl.download('https://example.test/file',target,resume=True),0)
            self.assertEqual(target.read_bytes(),b'new whole file')
    def test_download_size_limit_rejects_oversized_response(self):
        class Response(io.BytesIO):
            url='https://example.test/file';status=200
        with tempfile.TemporaryDirectory() as d,patch('urllib.request.urlopen',return_value=Response(b'too large')):
            with self.assertRaisesRegex(ValueError,'pinned size'):windows_curl.download('https://example.test/file',Path(d)/'download',limit=3)

if __name__=='__main__':unittest.main()
