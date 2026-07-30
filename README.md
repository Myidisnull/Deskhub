# 🖥️ Deskhub

> **Open-source. Native. Cross-platform. Remote desktop that feels local.**

One **C++20 core** runs everywhere — Windows to iPhone to a Chrome tab — zero protocol
rewrites. Fast and raw enough to **actually play games remotely**, which ordinary remote
desktop tools can't pull off.

| ⚡ Fast | 📦 One file | 🎛️ Simple |
| ------ | ---------- | --------- |
| **~3.5 ms** capture→display, 60 fps. Zero-copy VRAM pipeline — the hot path never touches the CPU. | No installer, no background service, no account. The entire Windows app is one **~280 KB** exe; macOS is a **1.9 MB** dmg. | Two buttons: **Share** a display or **Connect** to an IP. That's the entire UI. |

## 💡 Why

- 💻 **Work** — run Claude Code, VS Code, or builds on your home PC from a weak laptop or an iPad at a café.
- 🌐 **Anything** — drive Chrome, Office, or PC-only software from any device.
- 🎮 **Games** — 60 fps, relative mouse + DirectInput scancodes, `F9` pointer lock.
- 🖥️ **Multi-monitor** — share one or several displays, each as its own session.

## 🚦 Platforms

| Platform | Host | Client | Status |
| -------- | :--: | :----: | ------ |
| **Windows** | ✅ | ✅ | Reference implementation — daily use over LAN + Tailscale (Internet/NAT) |
| **macOS** | ✅ | ✅ | Both roles working (ScreenCaptureKit + VideoToolbox + CGEvent) |
| **Android** | — | ✅ | Video + input (trackpad, keyboard) — testing on Google Play |
| **iOS** | — | ✅ | Video + input (trackpad, keyboard) — testing via TestFlight |
| **Ubuntu** | ✅ | ✅ | Both roles working (PipeWire + VA-API + uinput + GTK3) — verified between two machines over LAN |
| **Web** | — | 📐 | Designed (QUIC/WebTransport + WASM), not yet implemented |

Roadmap: [`docs/05-roadmap.md`](docs/05-roadmap.md)

## 🚀 Get it

**🪟 Windows & 🍎 macOS** — grab a single `.exe` / `.dmg` from
**[Releases](https://github.com/manhpham90vn/Deskhub/releases)** — no install, no setup.
On Windows, sharing prompts for admin once and the app configures the firewall by itself.

**🐧 Ubuntu** — a single binary from [Releases](https://github.com/manhpham90vn/Deskhub/releases).
**To connect and view, that is all** — it links only against GTK3, PipeWire and libva, which
a stock Ubuntu 22.04+ desktop already has, and the H.264 decoder is compiled in:

```bash
chmod +x deskhub && ./deskhub
```

**To share this machine's screen** you need three more things — Linux gives none of them
away by default:

```bash
# 1. Portal backend — on Wayland an app cannot read the screen; the portal asks for you.
sudo apt install xdg-desktop-portal xdg-desktop-portal-gnome   # KDE: -kde · sway/wlroots: -wlr

# 2. VA-API driver — H.264 is encoded on the GPU, there is no software fallback.
sudo apt install va-driver-all vainfo        # NVIDIA also needs: nvidia-vaapi-driver
vainfo | grep -E 'H264.*Enc'                 # must print ≥1 line, or this machine cannot host

# 3. Write access to /dev/uinput — how mouse and keyboard get injected.
echo 'KERNEL=="uinput", MODE="0660", GROUP="input", OPTIONS+="static_node=uinput"' \
  | sudo tee /etc/udev/rules.d/60-deskhub-uinput.rules
sudo udevadm control --reload-rules && sudo udevadm trigger
sudo usermod -aG input "$USER"               # then LOG OUT and back in
```

Building from source? Step 3 is just `make setup-linux-permissions`. If you enabled `ufw`,
also `sudo ufw allow 47777/udp`. Details and current limits:
[`docs/17-linux-app.md`](docs/17-linux-app.md).

**📱 iOS** — install [TestFlight](https://apps.apple.com/app/testflight/id899247664), then
join the beta: **[testflight.apple.com/join/7qY7wgpd](https://testflight.apple.com/join/7qY7wgpd)**

**🤖 Android** — direct APK from [Releases](https://github.com/manhpham90vn/Deskhub/releases),
or join the Play beta — three steps, **same Google account** as your phone's Play Store:

1. Join the tester group: [groups.google.com/g/deskhub-test](https://groups.google.com/g/deskhub-test)
2. Become a tester: [play.google.com/apps/testing/com.manhpham.deskhub](https://play.google.com/apps/testing/com.manhpham.deskhub)
3. Install (give Play a few minutes to sync): [play.google.com/store/apps/details?id=com.manhpham.deskhub](https://play.google.com/store/apps/details?id=com.manhpham.deskhub)
   — then please keep it installed **14+ days** (Google's requirement to go public).

**Using it:** on desktop, **Share** picks the display(s) to expose; **Connect** takes the
other machine's IP (port is always UDP 47777) — whoever connects gets mouse and keyboard,
there is no view-only mode. Over the Internet: run [Tailscale](https://tailscale.com) on
both machines and use the 100.x.y.z IP. On mobile the video frame is a trackpad:
drag = move, tap = click, hold-drag = drag, **Keys** = virtual keyboard.

Build from source: [`docs/README.md`](docs/README.md) · Bugs & feedback:
[issues](https://github.com/manhpham90vn/Deskhub/issues) — include your device model.

## ✨ Under the hood

- **Zero-copy end to end** — capture straight into VRAM → NVENC → HW decode → render; the hot path never touches the CPU.
- **Purpose-built UDP protocol** — infinite GOP + on-demand IDR, XOR FEC, adaptive bitrate.
- **Real input** — relative mouse (Raw Input) + scancodes for DirectInput games; host's own mouse/keyboard always wins.
- **Hybrid transport** — native = UDP; web = QUIC/WebTransport with the same core compiled to WASM.

Full docs: [`docs/README.md`](docs/README.md)
