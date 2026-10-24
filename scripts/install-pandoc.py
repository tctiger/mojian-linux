#!/usr/bin/env python3
"""Download pinned official Pandoc into Mojian's private user directory."""
from pathlib import Path
import hashlib
import io
import os
import platform
import tarfile
import urllib.request
VERSION = '3.12.1'
SHA256 = 'd0c90410e90204c9ca83b8539fac5c7aed01fd537207e4585849f8abc5df20b8'
URL = f'https://github.com/jgm/pandoc/releases/download/{VERSION}/pandoc-{VERSION}-linux-amd64.tar.gz'
if platform.system() != 'Linux' or platform.machine() not in ('x86_64', 'amd64'):
    raise SystemExit('此转换器包仅适用于 Linux x86_64；请为你的平台安装官方 Pandoc。')
print('下载官方 Pandoc', VERSION, flush=True)
with urllib.request.urlopen(URL, timeout=90) as response:
    data = response.read(150_000_001)
if len(data) > 150_000_000 or hashlib.sha256(data).hexdigest() != SHA256:
    raise SystemExit('校验失败，未安装。')
directory = Path.home() / '.local/lib/mojian/bin'
directory.mkdir(parents=True, exist_ok=True)
with tarfile.open(fileobj=io.BytesIO(data), mode='r:gz') as archive:
    member = archive.getmember(f'pandoc-{VERSION}/bin/pandoc')
    if not member.isfile() or member.size > 250_000_000:
        raise SystemExit('转换器归档内容无效。')
    binary = archive.extractfile(member).read()
staged = directory / f'pandoc.install-{os.getpid()}'
try:
    staged.write_bytes(binary)
    staged.chmod(0o755)
    staged.replace(directory / 'pandoc')
finally:
    staged.unlink(missing_ok=True)
(directory.parent / 'PANDOC-SOURCE.txt').write_text(f'Pandoc {VERSION}\n{URL}\nSHA256 {SHA256}\nOfficial GPL sources: https://github.com/jgm/pandoc/tree/{VERSION}\n')
print('已安装：', directory / 'pandoc')
