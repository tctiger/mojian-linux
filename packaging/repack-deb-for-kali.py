#!/usr/bin/env python3
"""Adjust Ubuntu Qt package names for Kali; requires dpkg-deb (Linux).

The executable is unchanged. This is a dependency metadata adaptation, not a
native Kali build or a guarantee of runtime compatibility.
"""
import argparse
import re
import subprocess
import tempfile
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('package', type=Path)
parser.add_argument('output', type=Path)
args = parser.parse_args()
source = args.package.resolve()
output = args.output.resolve()
if source == output:
    parser.error('Output must differ from the original package')
names = ('libqt6gui6', 'libqt6network6', 'libqt6printsupport6', 'libqt6widgets6')
with tempfile.TemporaryDirectory(prefix='mojian-kali-') as directory:
    root = Path(directory) / 'package'
    subprocess.run(['dpkg-deb', '--raw-extract', str(source), str(root)], check=True)
    control = root / 'DEBIAN/control'
    text = control.read_text()
    if not re.search(r'^Package: mojian$', text, re.M):
        parser.error('Expected a mojian package')
    for name in names:
        # Preserve symbol-generated minimum versions; only adjust package names.
        text, count = re.subn(r'\b' + name + r't64\b', name, text)
        if count != 1:
            parser.error('Expected exactly one dependency on ' + name + 't64')
    text = re.sub(r'^(Version: .+)$', r'\1+kali1', text, flags=re.M)
    control.write_text(text)
    output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(['dpkg-deb', '--build', '--root-owner-group', str(root), str(output)], check=True)
print(output)
