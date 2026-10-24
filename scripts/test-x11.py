#!/usr/bin/env python3
"""Run UI checks in a private X11 server without touching the user's desktop."""
from pathlib import Path
import json
import os
import select
import subprocess
import tempfile
import time

root = Path(__file__).resolve().parent.parent
checks = []
def check(condition, message):
    if not condition:
        raise AssertionError(message)
    checks.append(message)
    print('PASS:', message, flush=True)

def wait_until(predicate, message, timeout=8):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.08)
    raise AssertionError(message)

with tempfile.TemporaryDirectory(prefix='mojian-x11-') as temp:
    work = Path(temp)
    read_fd, write_fd = os.pipe()
    with (root / 'xvfb.log').open('w') as server_log:
        server = subprocess.Popen(['Xvfb', '-displayfd', str(write_fd), '-screen', '0', '1600x1000x24', '-nolisten', 'tcp', '-ac'], pass_fds=(write_fd,), stdout=server_log, stderr=server_log)
        os.close(write_fd)
        app = None
        try:
            if not select.select([read_fd], [], [], 10)[0]:
                raise RuntimeError('Xvfb did not start')
            display = os.read(read_fd, 64).decode().strip()
            os.close(read_fd)
            env = dict(os.environ, DISPLAY=':' + display, QT_QPA_PLATFORM='xcb', XDG_CONFIG_HOME=str(work / 'config'), XDG_DATA_HOME=str(work / 'data'), XDG_RUNTIME_DIR=str(work / 'runtime'))
            env.pop('XAUTHORITY', None)
            (work / 'runtime').mkdir(mode=0o700)
            subprocess.run([str(root / 'build/mojian_tests'), '-o', str(root / 'test-x11-results.txt') + ',txt'], cwd=root, env=env, check=True, timeout=60)
            check(True, 'Native Qt tests passed with X11/xcb platform')
            subprocess.run([str(root / 'build/mojian_parity_tests'), '-o', str(root / 'parity-x11-results.txt') + ',txt'], cwd=root, env=env, check=True, timeout=90)
            check(True, 'Complete standards and feature parity tests passed on X11')
            with (root / 'app-integration.log').open('w') as app_log:
                app = subprocess.Popen([str(root / 'build/mojian')], cwd=root, env=env, stdout=app_log, stderr=app_log)
                def windows():
                    result = subprocess.run(['xdotool', 'search', '--onlyvisible', '--pid', str(app.pid)], env=env, capture_output=True, text=True)
                    return result.stdout.split()
                def launch(*args):
                    subprocess.run([str(root / 'build/mojian'), *args], env=env, check=True, timeout=10)
                wait_until(lambda: len(windows()) == 1, 'Initial app must have one window')
                check(len(windows()) == 1, 'First launch creates exactly one document')
                launch()
                wait_until(lambda: len(windows()) == 2, 'Second launch must add one document')
                time.sleep(0.3)
                check(len(windows()) == 2, 'Second launch forwards to same process and adds exactly one document')
                wid = windows()[-1]
                subprocess.run(['xdotool', 'windowfocus', '--sync', wid, 'key', '--clearmodifiers', 'ctrl+n'], env=env, check=True)
                wait_until(lambda: len(windows()) == 3, 'Ctrl+N must add one document')
                time.sleep(0.3)
                check(len(windows()) == 3, 'Ctrl+N on an open app creates exactly one new document')
                note = work / 'sample.md'
                note.write_text('# 中文文件\n\n---\n\n**bold** 和 *中文斜体*。\n')
                launch(str(note))
                wait_until(lambda: len(windows()) == 4, 'Opening a file must add one document')
                check(len(windows()) == 4, 'Opening a file through the second process creates one document')
                launch(str(note))
                time.sleep(0.4)
                check(len(windows()) == 4, 'Opening the same file again restores its window without duplication')
                check(app.poll() is None, 'Main application remains running throughout integration checks')
                # Crash/recovery happens only in this test's private data directory.
                target = next(w for w in windows() if 'sample.md' in subprocess.check_output(['xdotool', 'getwindowname', w], env=env, text=True))
                subprocess.run(['xdotool', 'windowfocus', '--sync', target, 'key', '--clearmodifiers', 'ctrl+End'], env=env, check=True)
                subprocess.run(['xdotool', 'type', '--clearmodifiers', 'RECOVERY-CHECK'], env=env, check=True)
                recovery = work / 'data/Mojian/mojian/recovery'
                wait_until(lambda: recovery.exists() and list(recovery.glob('*.json')), 'Recovery snapshot not written', timeout=25)
                check(True, 'Unsaved text receives a recovery snapshot')
                app.kill(); app.wait(timeout=5)
                app = subprocess.Popen([str(root / 'build/mojian')], cwd=root, env=env, stdout=app_log, stderr=app_log)
                wait_until(lambda: len(windows()) >= 1, 'Recovery dialog did not appear')
                dialog = windows()[0]
                subprocess.run(['xdotool', 'windowfocus', '--sync', dialog, 'key', '--clearmodifiers', 'Return'], env=env, check=True)
                def restored():
                    return any('sample.md' in subprocess.check_output(['xdotool', 'getwindowname', w], env=env, text=True) for w in windows())
                wait_until(restored, 'Recovered document window did not appear')
                check(True, 'Crash restart restores the modified document')
                target = next(w for w in windows() if 'sample.md' in subprocess.check_output(['xdotool', 'getwindowname', w], env=env, text=True))
                subprocess.run(['xdotool', 'windowfocus', '--sync', target, 'key', '--clearmodifiers', 'ctrl+s'], env=env, check=True)
                wait_until(lambda: 'RECOVERY-CHECK' in note.read_text(), 'Restored document could not be saved')
                check(True, 'Recovered content survives native saving')
                wait_until(lambda: not list(recovery.glob('*.json')), 'Saved recovery record was not cleared')
                check(True, 'Saving clears the obsolete recovery snapshot')

        finally:
            if app is not None and app.poll() is None:
                app.terminate()
                try: app.wait(timeout=5)
                except subprocess.TimeoutExpired: app.kill(); app.wait()
            server.terminate()
            try: server.wait(timeout=5)
            except subprocess.TimeoutExpired: server.kill(); server.wait()
(root / 'x11-integration-results.json').write_text(json.dumps({'passed': len(checks), 'checks': checks}, ensure_ascii=False, indent=2))
print(f'All {len(checks)} X11 integration checks passed.')
