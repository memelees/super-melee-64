"""Exercise stale ownership and surviving-child protection with real processes."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest
from session_lock import acquire, release


class SessionLockTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.path = Path(self.directory.name) / 'native.lock'

    def exited_owner(self):
        return subprocess.check_output(
            [sys.executable, '-c', 'import os; print(os.getpid())'], text=True).strip()

    def test_recovers_exited_owner(self):
        self.path.write_text(self.exited_owner())
        inode = acquire(self.path)
        self.assertEqual(self.path.read_text(), str(os.getpid()))
        release(self.path, inode)
        self.assertFalse(self.path.exists())

    def test_preserves_live_owner(self):
        self.path.write_text(str(os.getpid()))
        with self.assertRaises(FileExistsError):
            acquire(self.path)
        self.assertEqual(self.path.read_text(), str(os.getpid()))

    def test_preserves_surviving_child(self):
        old = self.exited_owner()
        self.path.write_text(old)
        child = subprocess.Popen(['/bin/sleep', '10'])
        try:
            with self.assertRaises(FileExistsError):
                acquire(self.path, ('/bin/sleep 10',))
            self.assertEqual(self.path.read_text(), old)
        finally:
            child.terminate()
            child.wait()

    def test_release_preserves_replaced_lock(self):
        inode = acquire(self.path)
        self.path.unlink()
        self.path.write_text('1')
        release(self.path, inode)
        self.assertEqual(self.path.read_text(), '1')

    def test_concurrent_recovery_has_one_owner(self):
        self.path.write_text(self.exited_owner())
        script = '''import sys,time
from pathlib import Path
from session_lock import acquire,release
try: token=acquire(Path(sys.argv[1]))
except FileExistsError: sys.exit(2)
print("ACQUIRED",flush=True)
time.sleep(.6)
release(Path(sys.argv[1]),token)
'''
        children = [subprocess.Popen([sys.executable, '-c', script, str(self.path)],
                    cwd=Path(__file__).parent, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                    for _ in range(4)]
        results = [child.communicate(timeout=5) for child in children]
        self.assertEqual(sum(child.returncode == 0 for child in children), 1, results)
        self.assertEqual(sum(child.returncode == 2 for child in children), 3, results)


if __name__ == '__main__':
    unittest.main()
