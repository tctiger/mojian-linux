# Ubuntu 与 Arch 发行包

版本 `1.0.0-2`，均为 x86_64（Debian 架构名 amd64）。Ubuntu 包在 Ubuntu 24.04 LTS / Qt 6.4.2 内从源码构建；Arch 包在目标 Arch / Qt 6.12 内由 makepkg 构建。包未发布到公共仓库，也未使用私有签名密钥签名；同时提供 SHA-256 校验文件。

## 安装

Ubuntu **24.04 LTS amd64**：在包所在目录运行：

```sh
sudo apt install ./mojian_1.0.0-2_amd64.deb
```

apt 自动解析所需库，默认还安装推荐的 Graphviz、Hunspell、Secret Service 工具、Poppler、Pandoc 和中文字体，以及 Wayland/GTK 桌面集成。不支持 Ubuntu 22.04 的系统 Qt 6.2。其他 Ubuntu 版本未逐一验证。

Arch Linux x86_64（Qt 6.12 或更新的兼容版本）：

```sh
sudo pacman -U ./mojian-1.0.0-2-x86_64.pkg.tar.zst
```

Arch 的可选增强依赖：

```sh
sudo pacman -S --needed graphviz hunspell libsecret poppler pandoc-cli qt6-translations qt6-wayland
```

Pandoc 也可沿用已安装的官方私有二进制 `~/.local/lib/mojian/bin/pandoc`。fcitx5 用户安装对应 Qt 输入法前端（Ubuntu：fcitx5-frontend-qt6；Arch：fcitx5-qt），应用不修改全局输入法设置。

安装后菜单名称为“墨笺 / Mojian”，可直接运行 `/usr/bin/mojian`。按 F1 查看完整中文帮助。

### 已有用户目录安装

包安装不结束运行中的旧程序。先保存并退出旧窗口，再从菜单或 `/usr/bin/mojian` 打开系统包版本。菜单使用 `/usr/bin/mojian`，避免被旧的 `~/.local/bin/mojian` 遮蔽；终端的裸命令 `mojian` 仍遵守你的 PATH。包不会删除用户目录安装、个人文档或设置。

## 卸载

```sh
# Ubuntu
sudo apt remove mojian
# Arch
sudo pacman -R mojian
```

程序文件、菜单和图标由包管理器管理。个人文档、恢复记录和用户设置不会由卸载脚本删除。包不含自动上传、启动或清理用户目录的脚本。

## 构建

```sh
python3 packaging/build-source.py
```

输出共用源码归档及含精确 SHA-256 的 `PKGBUILD`。Arch 将两者放在同一目录运行 `makepkg`。Ubuntu 源码中已附 `debian/`，具备 control 声明的依赖后运行 `DEB_BUILD_OPTIONS=parallel=2 dpkg-buildpackage -us -uc -b`；构建会执行既有两套 Qt 回归。

Ubuntu 构建镜像定义在 `ubuntu-24.04.Dockerfile`。容器只挂载独立打包目录，不挂载用户 home、显示服务器或 Docker socket。验证脚本分别使用私有 pacman 根目录或一次性 Ubuntu 容器检查安装、启动、重装和卸载。
