# 🖥️ Deskhub

> **Your whole PC, in your hand — anywhere.** Control any app on your machine remotely —
> code with **Claude Code** or **VS Code**, browse **Chrome**, edit documents, or play
> **demanding games** — from your phone, tablet, another laptop, or straight from a browser.
> **Millisecond** latency, end-to-end **hardware encode/decode**, everything in **one file**.

A **low-latency, cross-platform** remote desktop/app with an **AnyDesk-style** architecture —
but fast enough, and raw enough at the input layer, to **actually play games** (relative mouse +
DirectInput scancodes), which ordinary remote desktop tools can't pull off. The technical
differentiator: **a single C++20 core that runs everywhere** — from Windows to iPhone to a
Chrome tab — with zero protocol rewrites.

| ⚡ Latency | 🖥️ Pipeline | 🌐 Platforms | 📦 Deployment |
|-----------|-------------|-------------|---------------|
| **~3.5 ms** capture→display<br>(loopback, measured on RTX 5070 Ti) | **Zero-copy VRAM**, HW encode+decode, 60 fps | **3 hosts + 6 clients** from one `core/` | **One app per OS** — plug and play |

> **New:** the **iOS and Android clients now do both video and input** — virtual trackpad +
> virtual keyboard, controlling a real Windows machine from your phone over LAN or Tailscale.
> Public testing is open: [Join the mobile beta](#-join-the-mobile-beta).

## 💡 What it's for

- **Remote work** — open Claude Code, VS Code, or a terminal on your home PC and code/run
  builds from a weak laptop or an iPad at a café.
- **Browsing & app control** — drive Chrome, Office, or PC-only software from any device.
- **Gaming** — the most demanding case: 60 fps, low latency, relative mouse + DirectInput keys.
- **Share one window, not the whole screen** — pick exactly the app you want to expose;
  the rest of your machine stays private.

## 🚦 Status

**Windows is the reference implementation — full pipeline, verified in real use across 2
machines over LAN and via Tailscale**, i.e. it works **over the Internet/NAT**, not just on a
local network. Grab a prebuilt binary from
[Releases](https://github.com/manhpham90vn/Deskhub/releases).

| Platform | Host | Client | Status |
|----------|:----:|:------:|--------|
| **Windows** | ✅ | ✅ | **Real-world use across 2 machines over LAN + via Tailscale** (Internet/NAT) — video + input |
| **Android** | — | ✅ | **Video + input** (virtual trackpad, virtual keyboard, shortcut keys) — in testing on Google Play |
| **iOS** | — | ✅ | **Video + input** (virtual trackpad, virtual keyboard) — in testing via TestFlight |
| **macOS** | 🔶 | 🔶 | **Both roles implemented** (ScreenCaptureKit + VideoToolbox + CGEvent) — not yet verified on two physical machines |
| **Web** | — | 📐 | Designed, not yet implemented |
| **Ubuntu** | ⬜ | ⬜ | Not started |

Phase details + per-platform roadmap: [`docs/05-roadmap.md`](docs/05-roadmap.md).

## 📱 Join the mobile beta

The Android and iOS builds are **open to everyone**. Both run the full client feature set:
view the stream from a Windows machine and **actually control it** — drag on the screen like
a trackpad to move the mouse, tap to click, and bring up the virtual keyboard to type.
Feedback on smoothness/latency on your real device and network is very welcome.

### 🤖 Android — Google Play

Currently in **closed testing**; three steps required:

**Step 1 — join the Google Group.** Mandatory: Play only installs the app for accounts in
the tester group.

➡️ **[groups.google.com/g/deskhub-test](https://groups.google.com/g/deskhub-test)** → click **Join group**

**Step 2 — become a tester.**

➡️ **[play.google.com/apps/testing/com.manhpham.deskhub](https://play.google.com/apps/testing/com.manhpham.deskhub)** → click **Become a tester**

**Step 3 — install the app.** Use the *Download it on Google Play* button right on the page
from step 2, or open:

➡️ **[play.google.com/store/apps/details?id=com.manhpham.deskhub](https://play.google.com/store/apps/details?id=com.manhpham.deskhub)**

A few notes to save you time:

- Use **the same Google account that is signed in to the Play Store on your phone** for all
  three steps. With the wrong account, step 3 reports that the app can't be found.
- After step 2, Play needs **a few minutes** to sync — if the app doesn't show up yet, wait
  and reload the page.
- If you can, please **keep the app installed for at least 14 days**: Google requires 12
  testers continuously opted in for 14 days before the app can go public.

### 🍎 iOS — TestFlight

Just one step: install [TestFlight](https://apps.apple.com/app/testflight/id899247664) from
the App Store and open the invite link.

➡️ **[testflight.apple.com/join/7qY7wgpd](https://testflight.apple.com/join/7qY7wgpd)**

> ⏳ The link is pending Apple's review of the public beta — in the meantime it may say
> "This beta isn't accepting any new testers". Please try again in a few days.

### 💬 Feedback

Found a bug or have a suggestion? Open an
[issue](https://github.com/manhpham90vn/Deskhub/issues) — including your device model and OS
version helps a lot.

## 🚀 Download & run

**Fastest path — prebuilt binary** (Windows):

➡️ **[github.com/manhpham90vn/Deskhub/releases](https://github.com/manhpham90vn/Deskhub/releases)**

Download the latest release and run `Deskhub.exe` — no installation needed. On the host
machine, open the firewall once (command below). To use it **over the Internet**, enable
[Tailscale](https://tailscale.com) on both machines and connect using the Tailscale IP
(100.x.y.z).

The app opens on the **Home** screen, which shows this machine's address per network
adapter. **Share** lists the windows and displays you can expose (pick one or more →
become the host); **Connect** takes an `ip[:port]` — or a host discovered on the LAN — to
view and control it. Light/dark theme and English/Vietnamese can be switched in place. On
the host machine, open the firewall once:

```
netsh advfirewall firewall add rule name="Deskhub" dir=in action=allow protocol=udp localport=47777
```

Want to build from source instead? See the technical docs:
[`docs/README.md`](docs/README.md).

## 🎮 Remote control

Input is enabled by default: typing / moving the mouse over the client's preview window
controls the host machine.

- `F9` captures/releases the mouse (pointer lock) — **required for FPS games** (sends
  relative mouse motion instead of absolute coordinates).
- On the host machine, **click the shared window once** so it has focus: input is only
  injected while that window is in the foreground (by design — otherwise the remote user
  would type into your other apps).
- If the game/app runs elevated, run the host **as administrator**, or input gets blocked
  by UIPI.

**On iOS/Android** the video frame acts as a trackpad, and the cursor is always clamped to
the video area:

- **Drag a finger** — move the cursor. **Tap** — left click. **Double tap** — right click.
- **Hold then drag** — hold the left button and move (drag windows, select text); lifting
  your finger releases it.
- The **Keys** button opens the virtual keyboard for typing; the header row provides
  `Esc` / `Tab` / `Enter` / `F9` (keys the virtual keyboard doesn't have).

## ✨ Under the hood

- **Zero-copy end to end** — Windows Graphics Capture delivers frames straight into VRAM →
  NVENC encodes on the GPU → hardware decode → render. The hot path **never touches the CPU**.
- **Purpose-built realtime protocol** — UDP, infinite GOP + **on-demand IDR** (no wasted
  keyframes), **XOR FEC** to patch packet loss, **adaptive bitrate** driven by network feedback.
- **Hybrid transport** — native clients use UDP; the web uses **QUIC/WebTransport** (same
  datagram model, same `core/` compiled to **WASM**). No WebRTC, no WebSocket.
- **Automatic GPU selection** — NVIDIA (NVENC) → Intel/AMD → Media Foundation fallback.
- **Real remote input** — relative mouse (Pointer Lock/Raw Input) + **scancodes** for
  DirectInput games, three layers of stuck-key protection.

## 📚 Documentation

Full technical documentation lives in [`docs/`](docs/README.md) — cross-platform
architecture, the network protocol, the platform & transport matrix, build-from-source
instructions, and the roadmap. Start at [`docs/README.md`](docs/README.md).
