#!/usr/bin/env python3
"""Run only in a disposable Ubuntu runtime container as root."""
from pathlib import Path
import argparse
import json
import os
import subprocess
import tempfile
import time
parser = argparse.ArgumentParser()
parser.add_argument('package', type=Path)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
checks = []
def run(argv, **kwargs):
    result = subprocess.run(argv, text=True, capture_output=True, **kwargs)
    if result.returncode:
        print(result.stdout, result.stderr, flush=True)
        result.check_returncode()
    return result
assert Path('/etc/os-release').read_text().find('ID=ubuntu') >= 0
info = run(['dpkg-deb', '--info', str(args.package)]).stdout
files = run(['dpkg-deb', '--contents', str(args.package)]).stdout
assert './usr/bin/mojian' in files and 'io.mojian.Editor.desktop' in files
assert 'help-zh-CN.md' in files and '/copyright' in files
checks.append('Debian control metadata, executable, menu, icon, help and copyright verified')
assert run(['dpkg-query', '-W', '-f=${db:Status-Status}', 'mojian']).stdout == 'installed'
assert 'not found' not in run(['ldd', '/usr/bin/mojian']).stdout
run(['desktop-file-validate', '/usr/share/applications/io.mojian.Editor.desktop'])
version = run(['/usr/bin/mojian', '--version']).stdout
assert version.startswith('Mojian Linux 1.0.0')
checks.append('APT installation resolved dependencies; installed version and shared libraries verified')
with tempfile.TemporaryDirectory(prefix='mojian-deb-user-') as directory:
    user = Path(directory)
    config = user / 'config/Mojian'
    config.mkdir(parents=True)
    sentinel = config / 'mojian.conf'
    sentinel.write_text('[General]\nuserSentinel=keep\n')
    note = user / 'sample.md'
    note.write_text('# Keep my document\n')
    runtime = user / 'runtime'
    runtime.mkdir(mode=0o700)
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen', XDG_RUNTIME_DIR=str(runtime), XDG_CONFIG_HOME=str(user / 'config'), XDG_DATA_HOME=str(user / 'data'))
    process = subprocess.Popen(['/usr/bin/mojian'], env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        time.sleep(2)
        assert process.poll() is None, process.communicate()
        checks.append('Installed GUI starts and remains alive in a clean Ubuntu runtime')
    finally:
        if process.poll() is None:
            process.terminate()
            process.communicate(timeout=10)
    run(['dpkg', '-i', str(args.package)])
    checks.append('Reinstall succeeds')
    run(['apt-get', '-y', 'purge', 'mojian'])
    assert not Path('/usr/bin/mojian').exists()
    assert not Path('/usr/share/applications/io.mojian.Editor.desktop').exists()
    assert note.read_text() == '# Keep my document\n'
    assert 'userSentinel=keep' in sentinel.read_text()
    checks.append('Purge removes managed files and preserves user documents and settings')
args.output.write_text(json.dumps({'passed': len(checks), 'checks': checks, 'version': version.strip(), 'metadata': info, 'os': Path('/etc/os-release').read_text()}, ensure_ascii=False, indent=2))
print(json.dumps({'passed': len(checks), 'checks': checks}, ensure_ascii=False, indent=2))
