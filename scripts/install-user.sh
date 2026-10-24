#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
if [ ! -f build/CMakeCache.txt ] || ! grep -qx 'CMAKE_BUILD_TYPE:STRING=Release' build/CMakeCache.txt; then
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
fi
cmake --build build -j2
python3 - <<'INSTALLPY'
from pathlib import Path
import datetime
import os
import shutil
base = Path.home() / '.local'
exe = base / 'bin/mojian'
exe.parent.mkdir(parents=True, exist_ok=True)
if exe.exists():
    backup = base / 'lib/mojian/backups' / ('mojian-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f'))
    backup.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(exe, backup)
staged = exe.with_name('mojian.install-' + str(os.getpid()))
try:
    shutil.copy2('build/mojian', staged)
    staged.chmod(0o755)
    staged.replace(exe)
finally:
    staged.unlink(missing_ok=True)
menu = base / 'share/applications/io.mojian.Editor.desktop'
menu.parent.mkdir(parents=True, exist_ok=True)
menu.write_text(Path('resources/io.mojian.Editor.desktop').read_text().replace('Exec=mojian %F', 'Exec="' + str(exe) + '" %F'))
icon = base / 'share/icons/hicolor/512x512/apps/mojian.png'
icon.parent.mkdir(parents=True, exist_ok=True)
shutil.copy2('resources/mojian.png', icon)
licenses = base / 'share/licenses/mojian'
licenses.mkdir(parents=True, exist_ok=True)
shutil.copy2('resources/ThirdPartyNotices.txt', licenses)
for name in ('LICENSE', 'NOTICE'):
    shutil.copy2(name, licenses)
INSTALLPY
if command -v update-desktop-database >/dev/null 2>&1; then update-desktop-database "$HOME/.local/share/applications"; fi
printf '\n已安装到用户目录，运行 %s/.local/bin/mojian\n' "$HOME"
