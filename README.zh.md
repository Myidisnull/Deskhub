[English](README.md) · **中文** · [Tiếng Việt](README.vi.md)

> 本文是 [`README.md`](README.md) 的中文译本。若有出入，**以英文版为准**。

<div align="center">

# 🖥️ Deskhub

<sub>开源项目 **Deskhub** · 产品名 **System Runtime**</sub>

### 你的电脑，出现在你拥有的每一块屏幕上。

**开源。原生。跨平台。** 远程桌面用起来像坐在本机前一样——又快又「生」，真能拿来远程打游戏；这是普通远程桌面工具做不到的。

[![Release](https://img.shields.io/github/v/release/manhpham90vn/Deskhub?label=release&color=2563eb)](https://github.com/manhpham90vn/Deskhub/releases)
[![License: MIT](https://img.shields.io/github/license/manhpham90vn/Deskhub?color=2563eb)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-2563eb)](CMakeLists.txt)
[![Platforms](https://img.shields.io/badge/runs%20on-Windows%20·%20macOS%20·%20Linux%20·%20Android%20·%20iOS-2563eb)](#-平台)

[![ci](https://github.com/manhpham90vn/Deskhub/actions/workflows/ci.yml/badge.svg)](https://github.com/manhpham90vn/Deskhub/actions/workflows/ci.yml)
[![tests](https://github.com/manhpham90vn/Deskhub/actions/workflow/status/manhpham90vn/Deskhub/ci.yml?branch=main&label=test)](https://github.com/manhpham90vn/Deskhub/actions/workflows/ci.yml)
[![lint](https://github.com/manhpham90vn/Deskhub/actions/workflows/lint.yml/badge.svg)](https://github.com/manhpham90vn/Deskhub/actions/workflows/lint.yml)
[![codeql](https://github.com/manhpham90vn/Deskhub/actions/workflows/codeql.yml/badge.svg)](https://github.com/manhpham90vn/Deskhub/actions/workflows/codeql.yml)
[![nightly](https://github.com/manhpham90vn/Deskhub/actions/workflows/nightly.yml/badge.svg)](https://github.com/manhpham90vn/Deskhub/actions/workflows/nightly.yml)

<img src="docs/imgs/macos_1.png" alt="macOS 上的 System Runtime：正在共享显示器，实时采集/发送速率，一名已连接观众，往返时延 0 ms" width="850">

<sub>一台正在共享的 macOS 主机——每个显示器的实时采集/发送速率与带宽，每位观众单独一行，一键 <b>Stop</b> / <b>Disconnect</b>。是的，RTT 显示的是 <b>0 ms</b>。</sub>

</div>

一套 **C++20 核心**跑遍所有平台——从 Windows 到 iPhone——协议不用重写。共享一块屏幕，在另一台机器上输入 IP，就能开始操控。

| ⚡ 快 | 📦 单文件 | 🎛️ 简单 |
| ------ | ---------- | --------- |
| 采集→显示约 **~3.5 ms**，60 fps。零拷贝显存管线——热路径不碰 CPU。 | 无安装程序、无后台服务、无账号。Windows 整包约一个 **~5.1 MB** 的 exe；macOS 是 **1.9 MB** 的 dmg。 | 各平台同一套三块界面——**Host**、**Client**、**Settings**。**Share** 一块显示器，或 **Connect** 到一个 IP，就这些。手机也能当主机，但只能观看：移动系统不允许普通应用注入输入。 |

## 👀 快速一览

<table>
  <tr>
    <td align="center" width="50%">
      <img src="docs/imgs/macos_2.png" alt="macOS Client 页：按 IP 连接、通行码、网络扫描与带在线状态的最近设备">
      <br><sub><b>Client</b> — 输入 IP，或直接点网络扫描找到的机器。最近设备会带回实时在线/离线状态与 ping。</sub>
    </td>
    <td align="center" width="50%">
      <img src="docs/imgs/macos_3.png" alt="macOS Settings 页：帧率、码率、画质、端口、通行码、仅观看开关与权限状态">
      <br><sub><b>Settings</b> — 帧率、码率、画质上限、端口、必填的 4 位通行码、仅观看开关，以及（在 macOS 上）实时权限状态。</sub>
    </td>
  </tr>
</table>

<p align="center">
  <img src="docs/imgs/ios_1.png" alt="iOS 客户端：按 IP 与通行码连接、网络扫描、最近设备" width="270">
  &nbsp;&nbsp;
  <img src="docs/imgs/ios_2.png" alt="iOS 主机页：4 位通行码、开始共享按钮与供他人连接的 IP" width="270">
</p>
<p align="center"><sub>同一应用在 iPhone 上——扫描网络、点选机器、输入 4 位码……或切到 Host 页共享本机屏幕。</sub></p>

<p align="center">
  <img src="docs/imgs/android_1.png" alt="Android 客户端：按 IP 与通行码连接、网络扫描、最近设备" width="270">
  &nbsp;&nbsp;
  <img src="docs/imgs/android_2.png" alt="Android 主机页：4 位通行码、开始共享按钮与供他人连接的 IP" width="270">
</p>
<p align="center"><sub>Android 上也一样——同一套 Client 与 Host；主机为仅观看屏幕共享，需 Android 10+。</sub></p>

## 💡 为什么用

- 💻 **工作** — 在咖啡馆用弱笔记本或 iPad，跑家里电脑上的 Claude Code、VS Code 或编译。
- 🌐 **任意软件** — 从任意设备驱动 Chrome、Office 或只能在 PC 上跑的软件。
- 🎮 **游戏** — 60 fps、相对鼠标 + DirectInput 扫描码、`F9` 指针锁定。
- 🖥️ **多显示器** — 共享一块或多块显示器，各自独立会话。

## 🚦 平台

| 平台 | 主机 | 客户端 | 状态 |
| -------- | :--: | :----: | ------ |
| **Windows** | ✅ | ✅ | 参考实现 — 日常用于局域网 + Tailscale（公网/NAT） |
| **macOS** | ✅ | ✅ | 双角色可用（ScreenCaptureKit + VideoToolbox + CGEvent） |
| **Android** | ✅ | ✅ | 客户端：视频 + 输入（触控板、键盘）。主机：仅观看屏幕共享（MediaProjection + MediaCodec），Android 10+ — Google Play 测试中 |
| **iOS** | ✅ | ✅ | 客户端：视频 + 输入（触控板、键盘）。主机：通过 Broadcast Upload Extension 仅观看共享（ReplayKit + VideoToolbox）— TestFlight 测试中 |
| **Linux** | ✅ | ✅ | 双角色可用（PipeWire + VA-API + uinput + GTK3）— Ubuntu、Debian、Mint、Fedora、openSUSE、Arch，提供 deb / rpm / 便携二进制；已在两台局域网机器间验证 |

## 🔒 共享屏幕之前

> **⚠️ 会话加密可选，默认关闭。** 每个主机都要求 4 位通行码。加密关闭时流量明文——能抓到一个包的人可读出通行码并取得鼠标与键盘控制。加密开启时，会话视频、输入与剪贴板加密；发现仍为明文。把主机生成的会话密钥复制给观众，或仅在你接受「仅凭通行码即可拿到密钥」时打开 *Escrow key to viewers*。
>
> 请在**你信任的网络**上使用，或走 **VPN**——两边都装
> [Tailscale](https://tailscale.com)，连到 `100.x.y.z` 地址。**切勿端口转发 UDP 47777**，也不要在咖啡馆、酒店、办公室或其它共享 Wi-Fi 上共享屏幕。

安全基线是这样：优先信任网络或 VPN；局域网不完全可信时打开会话加密。完整威胁模型、保护范围与漏洞报告方式见 [`SECURITY.zh.md`](SECURITY.zh.md)。

## 🚀 获取

**🪟 Windows & 🍎 macOS** — 从
**[Releases](https://github.com/manhpham90vn/Deskhub/releases)** 下载单个 `.exe` / `.dmg`——无需安装、无需配置。
Windows 启动时会请求一次管理员权限——以便向提权窗口注入输入——并在你开始共享时自行添加 Windows 防火墙规则。

**🐧 Linux** — 从
[Releases](https://github.com/manhpham90vn/Deskhub/releases) 选取对应发行版的包；deb 与 rpm 内容相同：

| 发行版 | 文件 | 安装 |
| --- | --- | --- |
| Ubuntu、Kubuntu、Debian、Mint | `deskhub-v*-amd64.deb` | `sudo apt install ./deskhub-v*-amd64.deb` |
| Fedora（Workstation 与 KDE spin） | `deskhub-v*-x86_64.rpm` | `sudo dnf install ./deskhub-v*-x86_64.rpm` |
| openSUSE | `deskhub-v*-x86_64.rpm` | `sudo zypper install ./deskhub-v*-x86_64.rpm` |
| Arch 及其它 | `deskhub-v*-linux-x86_64` | `chmod +x deskhub-v*-linux-x86_64 && ./deskhub-v*-linux-x86_64` |

两种包都会带上 `/dev/uinput` 的 udev 规则（下文要求 3），安装后即可注入远程输入——不必改组、不必重新登录。便携二进制可在任意 glibc 2.35+ 的 x86_64 发行版上运行（Ubuntu 22.04、Fedora 36、openSUSE 15.5、当前 Arch）；只动态链接 GTK3、PipeWire 与 libva（常见桌面都有），H.264 解码器已静态编入。

**要连接并观看，装好即可。** 若要**共享本机屏幕**，还需要这三件事：

**1. 屏幕采集 portal。** System Runtime 一律通过 `xdg-desktop-portal` 采集——由它弹出「共享哪块屏幕」对话框。GNOME 与 KDE 在各大发行版上自带 portal 后端——Ubuntu、Kubuntu、Fedora Workstation、Fedora KDE、openSUSE 或带 GNOME/KDE 的 Arch **无需额外操作**。独立窗口管理器则需要装一个：

```bash
sudo apt install xdg-desktop-portal-wlr      # Debian 系上的 sway / river / Wayfire
sudo dnf install xdg-desktop-portal-wlr      # …Fedora
sudo pacman -S xdg-desktop-portal-wlr        # …Arch
```

sway、river、Wayfire 是基于 **wlroots** 的 Wayland 合成器；与 GNOME/KDE 不同，它们不自带 portal 后端，`-wlr` 就是为它们实现屏幕采集的后端（Hyprland 有自己的 `xdg-desktop-portal-hyprland`）。

**2. VA-API 驱动。** H.264 在 GPU 上编码；没有软件回退：

```bash
# Ubuntu / Debian / Mint
sudo apt install va-driver-all vainfo        # NVIDIA 还需: nvidia-vaapi-driver

# Fedora — 自带 Mesa 关闭了 H.264；可用驱动在 RPM Fusion：
sudo dnf install libva-utils
sudo dnf install mesa-va-drivers-freeworld   # AMD（RPM Fusion）
sudo dnf install intel-media-driver          # Intel（RPM Fusion）
sudo dnf install nvidia-vaapi-driver         # NVIDIA（RPM Fusion）

# openSUSE
sudo zypper install libva-utils              # 再加上你 GPU 厂商的 VA-API 驱动

# Arch
sudo pacman -S libva-utils
sudo pacman -S libva-mesa-driver             # AMD · Intel: intel-media-driver · NVIDIA: libva-nvidia-driver

# 然后在每个发行版上：
vainfo | grep -E 'H264.*Enc'                 # 必须至少有一行，否则本机不能当主机
```

**3. 对 `/dev/uinput` 的写权限** — 鼠标与键盘注入靠它。deb/rpm 会替你装好 udev 规则。若用便携二进制，一条命令即可（不必克隆仓库，桌面会话也不必重新登录）：

```bash
curl -fsSL https://raw.githubusercontent.com/manhpham90vn/Deskhub/main/scripts/setup-uinput.sh | sudo bash
```

想先读再交给 sudo？先下载
[`scripts/setup-uinput.sh`](scripts/setup-uinput.sh)——只有十几行。若已检出源码，等价命令是 `make setup-linux-permissions`。

若启用了防火墙，还需放行 UDP 47777（`sudo ufw allow 47777/udp` /
`sudo firewall-cmd --add-port=47777/udp --permanent`）。没有 uinput 权限时应用仍可运行、仍可观看——只是无法向本机注入鼠标/键盘。

**📱 iOS** — 安装 [TestFlight](https://apps.apple.com/app/testflight/id899247664)，然后
加入测试：**[testflight.apple.com/join/7qY7wgpd](https://testflight.apple.com/join/7qY7wgpd)**

**🤖 Android** — 从 [Releases](https://github.com/manhpham90vn/Deskhub/releases) 直接下 APK，
或加入 Play 测试——三步，**须与手机 Play 商店同一 Google 账号**：

1. 加入测试组：[groups.google.com/g/deskhub-test](https://groups.google.com/g/deskhub-test)
2. 成为测试者：[play.google.com/apps/testing/com.manhpham.deskhub](https://play.google.com/apps/testing/com.manhpham.deskhub)
3. 安装（给 Play 几分钟同步）：[play.google.com/store/apps/details?id=com.manhpham.deskhub](https://play.google.com/store/apps/details?id=com.manhpham.deskhub)
   — 然后请保持安装 **14 天以上**（Google 上架公开版的要求）。

## 🕹️ 使用方法

在桌面端，**Host** 选择要暴露的显示器，其 *Share on network* 选项会把主机钉在本机某一个地址上，从而无法从该机接入的其它网络触达——下方地址列表只显示所选网络；若该地址消失，共享会回退到所有网络，并在状态行说明。**Client** 填写
另一台机器的 IP（默认 UDP 47777，除非你改了端口）。同一时间最多 **5 名观众** 观看一台主机；你连过的机器会出现在 **Recent devices**，带实时在线/离线圆点。Client 页的 **Your name** 为本机命名——默认是设备自身名称，可改——以便主机有多名观众时区分他们。

同一时间只有一名观众驱动鼠标与键盘：先加入者在冲突时优先，其余人的输入会被丢弃，直到当前控制者空闲满一秒。坐在主机前的人优先级高于所有观众。每个主机的 **Settings** 含访问控制：观众必须输入的 **4 位通行码**——首次启动生成、随时可改、无法关闭；可选 **Encrypt session traffic**（默认关）及主机生成、可复制/刷新的会话密钥，*Per share* 或 *Persistent* 寿命，以及可选 *Escrow key to viewers*；还有 *Viewers can control this machine*，取消勾选即为 **仅观看**。错误通行码或会话密钥尝试会限速。五个客户端都能输入通行码与会话密钥并按设备记住。细节见 [`SECURITY.zh.md`](SECURITY.zh.md)。

**Settings** 里还有桌面端体验开关。*Start System Runtime when you
log in* 会注册各平台自己的登录自启——Linux 的 autostart、Windows 的计划任务（登录时无 UAC）、macOS 的 Login Item——而 *Start sharing when System Runtime opens*
会在启动时替你按下 Share。*Keep running in the background* 会加入托盘 / 菜单栏图标（显示/隐藏、开始/停止共享、退出），并把窗口关闭变成「隐藏」——每次启动窗口仍会先显示：三项一起开，机器可在登录后自行开始共享，你关窗口后藏进托盘。
*Sync clipboard text* 让一台设备复制的纯文本可在其它设备粘贴
（双向、仅文本、32 KiB 上限，五个客户端都支持——手机或平板仅在 System Runtime 位于前台时才能读取本机剪贴板）——会话加密关闭时走与视频相同的明文通道，不信任的网络请关掉，除非已打开加密。

公网访问：两边跑 [Tailscale](https://tailscale.com)，使用
100.x.y.z IP——切勿端口转发。移动端画面即触控板：
拖 = 移动，点 = 单击，双击 = 右键，长按拖 = 拖拽，**Keys** = 虚拟键盘。

从源码构建：`make bootstrap` 然后 `make build-<os>` — 每个目标都写在
[`Makefile`](Makefile) 顶部。缺陷与反馈：
[issues](https://github.com/manhpham90vn/Deskhub/issues) — 请附带设备型号。

## 📚 文档

下列文档以英文为权威版本，旁有越南语（`*.vi.md`）与中文译本（`*.zh.md`，目前覆盖 README、安全策略与隐私政策）。

- [功能规格](docs/SPECIFICATION.md) — System Runtime 做什么，不含实现细节（[Tiếng Việt](docs/SPECIFICATION.vi.md)）
- [`SECURITY.zh.md`](SECURITY.zh.md) — 威胁模型与漏洞报告（[English](SECURITY.md) · [Tiếng Việt](SECURITY.vi.md)）
- [`PRIVACY.zh.md`](PRIVACY.zh.md) — 隐私政策（[English](PRIVACY.md) · [Tiếng Việt](PRIVACY.vi.md)）
- [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) — 第三方组件（[Tiếng Việt](THIRD_PARTY_NOTICES.vi.md)）

## ✨ 技术要点

- **端到端零拷贝** — 采集直入显存 → NVENC → 硬解 → 渲染；热路径不碰 CPU。
- **专用 UDP 协议** — 无限 GOP + 按需 IDR、XOR FEC、自适应码率。
- **真实输入** — 相对鼠标（Raw Input）+ DirectInput 游戏用的扫描码；主机本地鼠标/键盘始终优先。
- **一套共享核心** — 协议、FEC 与码率控制在 `core/`，编进每个客户端。
- **刻意折腾** — 核心有离线单元测试，CI 跑 ASan、UBSan、TSan；六个 libFuzzer 目标每晚锤线格式、H.264 解析、重组与会话状态机；每次崩溃都会变成回归测试。

## 📄 许可证

MIT — 见 [`LICENSE`](LICENSE)。第三方组件及其声明（含 Linux 应用中静态链接的 LGPL 版 FFmpeg）列于
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)。
