# NetSwitch · 网卡切换器

轻量原生 Windows 网卡切换工具。使用 C++ / Win32 / GDI+，无需 .NET、浏览器内核或额外运行库。

[下载安装包](https://github.com/AimeeAngelia/NetSwitch/releases/latest) · [反馈问题](https://github.com/AimeeAngelia/NetSwitch/issues) · [GPL-3.0](LICENSE)

## 功能

- 跟随 Windows 深浅色的卡片界面，只显示网卡名称、状态、本机 IPv4 和切换按钮。
- 实体网卡与全部普通网卡两种范围，后者包含 VMware / VPN 等虚拟网卡。
- 先启用目标，再禁用范围内其他网卡；中途失败时尝试恢复原启用状态。
- 监听系统地址和接口通知，自动刷新 IP，并在变化后短延迟复核；待机没有定时网卡轮询。
- 窗口、托盘菜单和每个用户独立的登录自启设置。
- 安装时授权后台服务一次，日常启动和切换使用普通权限。

## 安装与使用

支持 Windows 10 1703 及更高版本、Windows 11，x64 构建。

1. 从 Releases 下载 `NetSwitch-2.1.0-Setup-x64.exe` 并运行。
2. 安装程序将应用和后台服务放入 Program Files，创建开始菜单入口，并注册 Windows 卸载项。
3. 打开软件，选择“实体网卡”或“全部网卡”，点击对应卡片的“切换”。

目标未连接网络时仍会切换。Wi-Fi 热点连接、VPN 登录由 Windows 或相应客户端完成。“已连接”指网卡连接状态，本软件不检测互联网访问。

点击 X 隐藏到托盘；双击托盘图标或再次启动程序可唤回窗口。托盘右键菜单提供切换、设置和退出。登录自启默认关闭。

安装包和程序当前没有代码签名；发布页提供 SHA-256 校验文件。

## 服务与用户范围

服务名为 `LocalNetSwitch`，开机启动，空闲时等待本地请求。本版本允许本机 Users / Administrators 组的用户切换整机网卡，因此操作会影响这台电脑的其他会话。

服务文件位于受保护的 Program Files 目录。通信仅使用本地命名管道，拒绝远程管道连接；服务端核对客户端身份、网卡 GUID 和操作范围。客户端也会核对管道服务进程与 SCM 登记。协议不提供执行任意命令或写入任意文件的功能。

安装、更新和卸载需要管理员授权。可从 Windows“已安装的应用”卸载软件；设置中的“卸载”仅移除后台服务。

## 从源码构建

需要 Visual Studio 2022 C++ Build Tools 和 Windows 10/11 SDK。

```powershell
.\build.cmd
```

输出为 `build\NetSwitch.exe`，VC 运行时静态链接。

制作安装包需要 Inno Setup 7.1.1：

```powershell
powershell -File .\scripts\build-installer.ps1 -IsccPath 'C:\工具目录\ISCC.exe'
```

生成 `dist\NetSwitch-2.1.0-Setup-x64.exe`。安装器脚本使用 Inno Setup 的系统主题样式，并检查服务安装结果。

## 验证

模拟测试不修改真实网卡：

```powershell
$result = Join-Path $PWD 'build\tests.json'
$p = Start-Process .\build\NetSwitch.exe -ArgumentList '--self-test', ('"' + $result + '"') -Wait -PassThru
Get-Content $result
if ($p.ExitCode -ne 0) { throw 'Tests failed' }
```

16 项测试覆盖范围保护、离线目标、请求校验、部分失败、恢复顺序、恢复失败报告、最终状态核对和 IPC 响应初始化。GitHub Actions 也会构建、运行模拟测试并验证安装/卸载流程；安装器烟雾测试不切换网卡。

本机验证只覆盖实体网卡启用/禁用和系统通知刷新，未覆盖所有驱动、VPN 客户端及 Wi-Fi DHCP 地址变化。资源占用取决于系统环境，不作为固定上限保证。

## 源码

- `core.cpp`：Windows 网卡和本机地址接口、切换与恢复。
- `service.cpp`：服务、本地 IPC、身份和范围检查、安装准备。
- `ui.cpp`：卡片、系统主题、托盘和事件刷新。
- `tests.cpp`：模拟测试和显式参数的实机测试入口。
- `installer/`、`scripts/`：安装器与构建脚本。

Copyright (C) 2026 AimeeAngelia。依据 **GPL-3.0-only** 发布，完整条款见 [LICENSE](LICENSE)。
