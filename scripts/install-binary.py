#!/usr/bin/env python3
"""Install a prebuilt Mojian bundle atomically, without closing old windows."""
from pathlib import Path
import argparse
import datetime
import os
import shutil
parser = argparse.ArgumentParser(description='安装墨笺 Linux 用户级应用')
parser.add_argument('--prefix', type=Path, default=Path.home() / '.local')
args = parser.parse_args()
root = Path(__file__).resolve().parent.parent
source = root / 'bin/mojian'
if not source.is_file():
    raise SystemExit('缺少 bin/mojian，请解压完整二进制发布包。')
prefix = args.prefix.expanduser().resolve()
exe = prefix / 'bin/mojian'
exe.parent.mkdir(parents=True, exist_ok=True)
if exe.exists():
    backup = prefix / 'lib/mojian/backups' / ('mojian-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f'))
    backup.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(exe, backup)
staged = exe.with_name('mojian.install-' + str(os.getpid()))
try:
    shutil.copy2(source, staged)
    staged.chmod(0o755)
    staged.replace(exe)
finally:
    staged.unlink(missing_ok=True)
menu = prefix / 'share/applications/io.mojian.Editor.desktop'
menu.parent.mkdir(parents=True, exist_ok=True)
menu.write_text((root / 'resources/io.mojian.Editor.desktop').read_text().replace('Exec=mojian %F', 'Exec="' + str(exe) + '" %F'))
icon = prefix / 'share/icons/hicolor/512x512/apps/mojian.png'
icon.parent.mkdir(parents=True, exist_ok=True)
shutil.copy2(root / 'resources/mojian.png', icon)
licenses = prefix / 'share/licenses/mojian'
licenses.mkdir(parents=True, exist_ok=True)
shutil.copy2(root / 'resources/ThirdPartyNotices.txt', licenses)
for name in ('LICENSE', 'NOTICE'):
    shutil.copy2(root / name, licenses)
command = shutil.which('update-desktop-database')
if command:
    import subprocess
    subprocess.run([command, str(menu.parent)], check=False)
print('已安装：', exe)
print('运行中的旧窗口已保留，请先保存文档再退出旧版。')
