#!/usr/bin/env python3
"""Run with: python3 tests/smoke.py ./mc (Linux, no extra packages)."""
import fcntl
import os
from pathlib import Path
import pty
import select
import signal
import struct
import sys
import tempfile
import termios
import time

binary = str(Path(sys.argv[1] if len(sys.argv) > 1 else './mc').resolve())
with tempfile.TemporaryDirectory(prefix='mc-smoke-') as temporary:
    root = Path(temporary)
    left, right = root / 'left', root / 'right'
    left.mkdir()
    right.mkdir()
    child = left / 'child dir'
    child.mkdir()
    unusual = "quote a'b$(touch INJECTED)"
    (left / unusual).write_text('literal filename')
    (left / 'one.log').write_text('one')
    (left / 'two.log').write_text('two')
    (left / 'other.txt').write_text('other')
    pid, terminal = pty.fork()
    if pid == 0:
        os.environ.update(TERM='xterm', LC_ALL='C.UTF-8')
        os.execl(binary, binary, str(left), str(right))
    fcntl.ioctl(terminal, termios.TIOCSWINSZ, struct.pack('HHHH', 30, 120, 0, 0))
    transcript = bytearray()

    def drain(duration=0.15):
        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            if select.select([terminal], [], [], max(0, deadline-time.monotonic()))[0]:
                try:
                    transcript.extend(os.read(terminal, 65536))
                except OSError:
                    return

    def send(keys):
        os.write(terminal, keys.encode() if isinstance(keys, str) else keys)
        drain()

    def expect(path, text):
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            if path.exists() and path.read_text() == text:
                return
            drain(0.1)
        raise AssertionError(f'{path}: expected {text!r}; terminal tail {transcript[-3000:]!r}')

    try:
        drain(1)
        send('pwd > startup\n')
        expect(left / 'startup', str(left) + '\n')
        send(b'\x15')  # Ctrl+U, active content follows the swap.
        send('pwd > swapped\n')
        expect(left / 'swapped', str(left) + '\n')
        send('\t')
        send('pwd > right-panel\n')
        expect(right / 'right-panel', str(right) + '\n')
        send('\t')
        send('printf x >> history\n')
        expect(left / 'history', 'x')
        send('printf y >> history\n')
        expect(left / 'history', 'xy')
        send(b'\x1bp\n')
        expect(left / 'history', 'xyy')
        send(b'\x1bp\x1bp\n')
        expect(left / 'history', 'xyyx')
        send('printf z >> history')
        send(b'\x1bp\x1bn\n')
        expect(left / 'history', 'xyyxz')
        send(b'\x1bsquote')
        send(b'\x1b')  # Leave panel prefix search before typing a command.
        send("printf '%s' ")
        send(b'\x1b\r')
        send('> quoted-name\n')
        expect(left / 'quoted-name', unusual)
        assert not (left / 'INJECTED').exists()
        send("printf '%s' ")
        send(b'\x1ba')
        send('> quoted-path\n')
        expect(left / 'quoted-path', str(left) + '/')
        send('+')
        send(b'\x7f*.log\n')  # Replace the default '*' mask.
        send(b'\x1b[15~')  # F5, then accept the other panel's path.
        send('\n')
        expect(right / 'one.log', 'one')
        expect(right / 'two.log', 'two')
        assert not (right / 'other.txt').exists()
        send("cd 'child dir'\n")
        send('pwd > changed\n')
        expect(child / 'changed', str(child) + '\n')
        send(b'\x1bg')
        send(str(left) + '\n')
        send('pwd > goto\n')
        expect(left / 'goto', str(left) + '\n')
        send(b'\x1b[3~')  # Delete must remain distinct from Alt+G.
        send(b'\x1b[21~')  # F10
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            ended, status = os.waitpid(pid, os.WNOHANG)
            if ended:
                assert os.waitstatus_to_exitcode(status) == 0
                pid = 0
                break
            drain(0.1)
        assert not pid, 'mc did not exit'
    finally:
        if pid:
            os.kill(pid, signal.SIGKILL)
            os.waitpid(pid, 0)
        os.close(terminal)
print('Terminal: initial paths, panel swap, Alt+P/N, shell quoting, masks, cd and Alt+G passed.')
