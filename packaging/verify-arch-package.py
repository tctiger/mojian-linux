#!/usr/bin/env python3
"""Check a package with an isolated pacman database, never the host database."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
import tempfile
import time
parser = argparse.ArgumentParser()
parser.add_argument('package', type=Path)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
package = args.package.resolve()
checks = []
def run(argv, **kwargs):
    result = subprocess.run(argv, capture_output=True, text=True, **kwargs)
    if result.returncode:
        print(result.stdout, result.stderr, flush=True)
        result.check_returncode()
    return result
info = run(['pacman', '-Qip', str(package)]).stdout
files = run(['pacman', '-Qlp', str(package)]).stdout
assert 'usr/bin/mojian' in files and 'io.mojian.Editor.desktop' in files
assert 'help-zh-CN.md' in files and 'licenses/mojian/LICENSE' in files
checks.append('Native metadata and executable, menu, icon, help, license paths verified')
with tempfile.TemporaryDirectory(prefix='mojian-pacman-') as folder:
    root = Path(folder)
    database = root / 'var/lib/pacman'
    database.mkdir(parents=True)
    base = ['fakeroot', 'pacman', '--root', str(root), '--dbpath', str(database), '--logfile', str(root / 'pacman.log'), '--noconfirm']
    run(base + ['-U', '-dd', '--noscriptlet', str(package)])
    checks.append('Install succeeded in a private root and private pacman database')
    binary = root / 'usr/bin/mojian'
    version = run([str(binary), '--version']).stdout.strip()
    assert version.startswith('Mojian Linux 1.0.0')
    run(['desktop-file-validate', str(root / 'usr/share/applications/io.mojian.Editor.desktop')])
    assert 'not found' not in run(['ldd', str(binary)]).stdout
    checks.append('Installed version, desktop file and shared libraries verified')
    user = root / 'home/test-user'
    config = user / '.config/Mojian'
    config.mkdir(parents=True)
    sentinel = config / 'mojian.conf'
    sentinel.write_text('[General]\nuserSentinel=keep\n')
    note = user / 'sample.md'
    note.write_text('# Keep my document\n')
    runtime = root / 'private-runtime'
    runtime.mkdir(mode=0o700)
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen', XDG_RUNTIME_DIR=str(runtime), XDG_CONFIG_HOME=str(user / '.config'), XDG_DATA_HOME=str(user / '.local/share'))
    process = subprocess.Popen([str(binary)], env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        time.sleep(2)
        assert process.poll() is None, process.communicate()
        checks.append('Installed GUI starts and remains alive in an isolated offscreen session')
    finally:
        if process.poll() is None:
            process.terminate()
            process.communicate(timeout=10)
    # Reinstall upgrades the managed executable, without accessing the real user's files.
    run(base + ['-U', '-dd', '--noscriptlet', str(package)])
    checks.append('Reinstall succeeds')
    run(base + ['-R', '-dd', '--noscriptlet', 'mojian'])
    assert not binary.exists()
    assert not (root / 'usr/share/applications/io.mojian.Editor.desktop').exists()
    assert note.read_text() == '# Keep my document\n'
    assert 'userSentinel=keep' in sentinel.read_text()
    checks.append('Uninstall removes managed files and preserves user documents and settings')
args.output.write_text(json.dumps({'passed': len(checks), 'checks': checks, 'package': package.name, 'sha256': hashlib.sha256(package.read_bytes()).hexdigest(), 'metadata': info}, ensure_ascii=False, indent=2))
print(json.dumps({'passed': len(checks), 'checks': checks}, ensure_ascii=False, indent=2))
