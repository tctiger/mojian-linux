# 功能覆盖与边界

功能基线源于 macOS 墨笺 3.11.1；本仓库包含 Linux 原生 C++ / Qt 实现和 Ubuntu/Arch 打包配置。

详见 [README](README.md)、[中文帮助](resources/help-zh-CN.md) 和 [验证记录](docs/VALIDATION.md)。

原生 HTML/CSS、数学与 Mermaid 均有范围限制；嵌套、图片和宏具有资源保护。增量解析在全局引用、围栏及不安全结构下回退到完整解析，不承诺所有操作为常数复杂度。
