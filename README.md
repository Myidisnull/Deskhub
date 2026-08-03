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

## 🔒 Before you share a screen

> **⚠️ Deskhub has no password and no encryption of its own. Anyone who can reach UDP
> 47777 on a sharing machine gets full mouse and keyboard control of it.**
>
> Run it on a **network you trust**, or over a **VPN** — install
> [Tailscale](https://tailscale.com) on both machines and connect to the `100.x.y.z`
> address. **Never port-forward UDP 47777**, and don't share your screen on café,
> hotel, office or any other shared Wi-Fi.

That is the whole security model: Deskhub borrows its encryption and its identity check
from the layer underneath it. Read [`SECURITY.md`](SECURITY.md) for the full threat
model, what is and isn't protected, and how to report a vulnerability.

## 🚀 Get it

**🪟 Windows & 🍎 macOS** — grab a single `.exe` / `.dmg` from
**[Releases](https://github.com/manhpham90vn/Deskhub/releases)** — no install, no setup.
On Windows, sharing prompts for admin once and the app configures the firewall by itself.

**🐧 Ubuntu** — grab the `.deb` from [Releases](https://github.com/manhpham90vn/Deskhub/releases):

```bash
sudo apt install ./deskhub_*_amd64.deb
```

The package ships the `/dev/uinput` udev rule (step 3 below), so remote input works right
after install — no group change, no re-login. A portable single binary is also attached;
**to connect and view it needs nothing** — it links only against GTK3, PipeWire and libva,
which a stock Ubuntu 22.04+ desktop already has, and the H.264 decoder is compiled in:

```bash
chmod +x deskhub && ./deskhub
```

**To share this machine's screen** you need three more things — the deb covers the third,
the portable binary does not:

```bash
# 1. Portal backend — on Wayland an app cannot read the screen; the portal asks for you.
sudo apt install xdg-desktop-portal xdg-desktop-portal-gnome   # KDE: -kde · sway/wlroots: -wlr

# 2. VA-API driver — H.264 is encoded on the GPU, there is no software fallback.
sudo apt install va-driver-all vainfo        # NVIDIA also needs: nvidia-vaapi-driver
vainfo | grep -E 'H264.*Enc'                 # must print ≥1 line, or this machine cannot host

# 3. PORTABLE BINARY ONLY — write access to /dev/uinput, how mouse and keyboard get injected.
echo 'KERNEL=="uinput", MODE="0660", GROUP="input", OPTIONS+="static_node=uinput"' \
  | sudo tee /etc/udev/rules.d/60-deskhub-uinput.rules
sudo udevadm control --reload-rules && sudo udevadm trigger
sudo usermod -aG input "$USER"               # then LOG OUT and back in
```

Building from source? Step 3 is just `make setup-linux-permissions`, or build the package
yourself with `make dist-linux`. If you enabled `ufw`,
also `sudo ufw allow 47777/udp`. Without the permission grant the app still runs and can
still view — it just cannot inject mouse/keyboard into this machine.

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
with no password asked and no view-only mode, so only share on a network you trust.
Over the Internet: run [Tailscale](https://tailscale.com) on both machines and use the
100.x.y.z IP — never a port-forward. On mobile the video frame is a trackpad:
drag = move, tap = click, hold-drag = drag, **Keys** = virtual keyboard.

Build from source: `make bootstrap` then `make build-<os>` — every target is documented
at the top of the [`Makefile`](Makefile). Bugs & feedback:
[issues](https://github.com/manhpham90vn/Deskhub/issues) — include your device model.

## ✨ Under the hood

- **Zero-copy end to end** — capture straight into VRAM → NVENC → HW decode → render; the hot path never touches the CPU.
- **Purpose-built UDP protocol** — infinite GOP + on-demand IDR, XOR FEC, adaptive bitrate.
- **Real input** — relative mouse (Raw Input) + scancodes for DirectInput games; host's own mouse/keyboard always wins.
- **Hybrid transport** — native = UDP; web = QUIC/WebTransport with the same core compiled to WASM.

## 📄 License

MIT — see [`LICENSE`](LICENSE). Third-party components and their notices (including the
statically linked LGPL build of FFmpeg in the Linux app) are listed in
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
