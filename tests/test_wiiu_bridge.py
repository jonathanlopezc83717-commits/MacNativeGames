"""Wii U GamePad bridge (tools/wiiu-gamepad-bridge): VPAD state becomes HID report bytes, and a
MiisendU datagram chain is reassembled before the parse (split JSON, --print-reports loopback)."""
import json
import os
import select
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1] / 'tools/wiiu-gamepad-bridge'


class Pipe:
    """Lines from a child pipe before a deadline. os.read keeps its own buffer: read(1) on a
    TextIOWrapper swallows the chunk, after which select never fires again."""

    def __init__(self, stream):
        self.fd = stream.fileno()
        self.buf = b''

    def readline(self, deadline):
        while b'\n' not in self.buf:
            if time.monotonic() >= deadline:
                return None
            ready, _, _ = select.select([self.fd], [], [], 0.1)
            if not ready:
                continue
            chunk = os.read(self.fd, 4096)
            if not chunk:
                return None
            self.buf += chunk
        line, _, self.buf = self.buf.partition(b'\n')
        return line.decode()


@unittest.skipUnless(sys.platform == 'darwin' and shutil.which('xcrun'), 'needs macOS and the Xcode command line tools')
class WiiuBridgeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        out = Path(cls.tmp.name)
        cls.probe = out / 'wiiu_bridge_probe'
        cls.bridge = out / 'wiiu_bridge'
        probe_build = subprocess.run(
            ['xcrun', 'clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
             str(TOOLS.parent.parent / 'tests/wiiu_bridge_probe.c'), '-o', str(cls.probe)],
            capture_output=True, text=True, timeout=120)
        if probe_build.returncode:
            raise AssertionError(f'probe build failed: {probe_build.stderr[-2000:]}')
        bridge_build = subprocess.run(
            ['xcrun', 'clang', '-Wall', '-Wextra', '-Werror', str(TOOLS / 'wiiu_bridge.m'),
             '-framework', 'Foundation', '-framework', 'IOKit', '-o', str(cls.bridge)],
            capture_output=True, text=True, timeout=120)
        if bridge_build.returncode:
            raise AssertionError(f'bridge build failed: {bridge_build.stderr[-2000:]}')

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_probe(self):
        run = subprocess.run([str(self.probe)], capture_output=True, text=True, timeout=30)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertEqual(json.loads(run.stdout)['status'], 'pass')

    def _line(self, pipe, proc, deadline, what):
        line = pipe.readline(deadline)
        self.assertIsNotNone(line, f'no {what} before timeout (rc={proc.poll()})')
        return line

    def test_loopback(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe_sock:
            probe_sock.bind(('127.0.0.1', 0))
            port = probe_sock.getsockname()[1]
        proc = subprocess.Popen([str(self.bridge), '--port', str(port), '--print-reports'],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            err, out = Pipe(proc.stderr), Pipe(proc.stdout)
            deadline = time.monotonic() + 15
            ready = ''
            while time.monotonic() < deadline:
                line = err.readline(deadline)
                if line is None:
                    break
                ready += line
                if 'listening' in line:
                    break
            self.assertIn('listening', ready, f'the bridge did not start: {ready}')

            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as send:
                send.settimeout(5)
                # one complete message: A held, left stick right, right stick down
                send.sendto(json.dumps({'wiiUGamePad': {'hold': 0x8000, 'lStickX': 1.0, 'lStickY': 0.0,
                                                        'rStickX': 0.0, 'rStickY': -1.0}}).encode(),
                            ('127.0.0.1', port))
                self.assertEqual(self._line(out, proc, time.monotonic() + 5, 'report'), '010008ff8080ff')
                # the same shape of message split across two datagrams, as udp.cpp does past 1400 bytes
                full = json.dumps({'wiiUGamePad': {'hold': 0x4000 | 0x0200, 'lStickX': 0.0, 'lStickY': 0.0,
                                                   'rStickX': 0.0, 'rStickY': 0.0}})
                cut = len(full) // 2
                send.sendto(full[:cut].encode(), ('127.0.0.1', port))
                send.sendto(full[cut:].encode(), ('127.0.0.1', port))
                self.assertEqual(self._line(out, proc, time.monotonic() + 5, 'report'), '02000080808080')
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=5)
            proc.stdout.close()
            proc.stderr.close()


if __name__ == '__main__':
    unittest.main()
