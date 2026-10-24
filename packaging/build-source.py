#!/usr/bin/env python3
"""Create the common distro packaging source and a checksum-pinned PKGBUILD."""
from pathlib import Path
import argparse
import hashlib
import tarfile
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parent.parent
out = args.output or root / 'dist/packages'
out.mkdir(parents=True, exist_ok=True)
source = out / 'mojian-1.0.0.tar.gz'
with tarfile.open(source, 'w:gz') as archive:
    for name in ['LICENSE', 'NOTICE', 'CMakeLists.txt', 'resources.qrc', 'src', 'resources', 'Vendor', 'tests', 'scripts', 'docs', 'README.md', 'PARITY.md', 'TEST-REPORT.md', 'CHANGELOG-1.0.0.md', 'packaging']:
        archive.add(root / name, arcname='mojian-1.0.0/' + name)
    archive.add(root / 'packaging/debian', arcname='mojian-1.0.0/debian')
checksum = hashlib.sha256(source.read_bytes()).hexdigest()
recipe = (root / 'packaging/arch/PKGBUILD.in').read_text().replace('@SOURCE_SHA256@', checksum)
(out / 'PKGBUILD').write_text(recipe)
(out / (source.name + '.sha256')).write_text(checksum + '  ' + source.name + '\n')
print(source)
print(checksum)
