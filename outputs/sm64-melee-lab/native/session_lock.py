"""Recover exited launchers without disturbing a live game or native test."""
import os
import subprocess
if os.name=='nt':
    import msvcrt,ctypes
else:
    import fcntl
from contextlib import contextmanager


@contextmanager
def guard(path):
    # This file remains in place so concurrent stale-lock recovery always
    # serializes against the same inode, including across unlink/recreate.
    with path.with_name(path.name + '.guard').open('a') as stream:
        if os.name=='nt':
            if stream.tell()==0:stream.write('0');stream.flush()
            stream.seek(0);msvcrt.locking(stream.fileno(),msvcrt.LK_LOCK,1)
        else:fcntl.flock(stream,fcntl.LOCK_EX)
        try:
            yield
        finally:
            if os.name=='nt':stream.seek(0);msvcrt.locking(stream.fileno(),msvcrt.LK_UNLCK,1)
            else:fcntl.flock(stream,fcntl.LOCK_UN)


def process_alive(pid):
    if pid <= 1:
        return True  # Unknown/invalid ownership is not permission to remove it.
    if os.name=='nt':
        kernel=ctypes.WinDLL('kernel32',use_last_error=True)
        kernel.OpenProcess.restype=ctypes.c_void_p;kernel.OpenProcess.argtypes=[ctypes.c_ulong,ctypes.c_int,ctypes.c_ulong]
        kernel.GetExitCodeProcess.argtypes=[ctypes.c_void_p,ctypes.POINTER(ctypes.c_ulong)];kernel.CloseHandle.argtypes=[ctypes.c_void_p]
        handle=kernel.OpenProcess(0x1000,False,pid)
        if not handle:return ctypes.get_last_error()!=87
        status=ctypes.c_ulong();ok=kernel.GetExitCodeProcess(handle,ctypes.byref(status));kernel.CloseHandle(handle)
        return not ok or status.value==259
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


def acquire(path, protected_commands=()):
    """Return this lock's inode token, or raise FileExistsError if occupied."""
    with guard(path):
        return _acquire(path, protected_commands)


def _acquire(path, protected_commands):
    for _ in range(8):
        try:
            with path.open('x') as stream:
                stream.write(str(os.getpid()))
                stream.flush()
                return os.fstat(stream.fileno()).st_ino
        except FileExistsError:
            try:
                with path.open('rb') as stream:
                    contents = stream.read()
                    inode = os.fstat(stream.fileno()).st_ino
                owner = int(contents)
            except FileNotFoundError:
                continue
            except ValueError:
                raise FileExistsError('Native lock has unknown ownership')
            if process_alive(owner):
                raise FileExistsError(f'Native launcher {owner} is still running')
            # A launcher can exit while its child survives. Do not rebuild or
            # replace files mapped by that game, even when the owner is gone.
            commands = subprocess.check_output(['ps', '-axo', 'args='], text=True).splitlines() if os.name!='nt' else []
            if any(command.strip().startswith(prefix)
                   for command in commands for prefix in protected_commands):
                raise FileExistsError('A native game is still running')
            try:
                # Recheck ownership before removing a stale file: another
                # launcher may have recovered it while ps was running.
                with path.open('rb') as stream:
                    if os.fstat(stream.fileno()).st_ino != inode or stream.read() != contents:
                        continue
                path.unlink()
            except FileNotFoundError:
                continue
            print('Recovered a stale lock from an exited game launcher.', flush=True)
    raise FileExistsError('Native launcher lock changed repeatedly; try again')


def release(path, inode):
    """Only remove the file this process acquired."""
    with guard(path):
        _release(path, inode)


def _release(path, inode):
    try:
        with path.open('rb') as stream:
            owned = (os.fstat(stream.fileno()).st_ino == inode
                     and stream.read() == str(os.getpid()).encode())
        if owned:
            path.unlink()
    except FileNotFoundError:
        pass
