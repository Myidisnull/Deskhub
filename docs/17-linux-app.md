# 17 — Ubuntu app (both roles)

The Ubuntu app is **one executable, `deskhub`**, containing both roles like Windows
(`Deskhub.exe`) and macOS (`app.app`): **Share** a screen or **Connect** to another
machine, chosen at runtime. Tree: `client/linux/` — `cpp/` (native layer, no GTK) +
`gtk/` (GTK3 UI). All protocol logic is the shared `core/`; this document covers only
the Linux backends.

> **Status (2026-07-30): both roles run on real hardware.** A session between two real
> machines over LAN works end to end — capture, encode, decode, display, and mouse and
> keyboard injection — so the pipeline in §2–§7 is confirmed, not just compiled. §8
> records what that first run settled and the two things it did not touch.
> Performance numbers here are still **design targets**: nothing has been measured yet.

## 1. Requirements

**Build** (`make bootstrap` installs all of these):

```
libgtk-3-dev libglib2.0-dev libepoxy-dev libegl-dev libgles-dev
libdrm-dev libva-dev libpipewire-0.3-dev libspa-0.2-dev
nasm  pkg-config  cmake  ninja-build
```

`libavcodec-dev` is deliberately **not** in that list. FFmpeg is built from source in a
minimal configuration and linked **statically** — see `scripts/build-ffmpeg.sh` for the
full reasoning, and "Why FFmpeg is vendored" below. `nasm` is what compiles its x86
assembly. `make bootstrap` runs that build; on its own it is `scripts/build-ffmpeg.sh`,
which is a no-op once the stamp matches.

**Run:**

| Need | Package | Which role |
|---|---|---|
| Screen capture permission | `xdg-desktop-portal` + a backend for your compositor: `-gnome`, `-kde` or `-wlr` | host |
| H.264 encoding on the GPU | `va-driver-all` (pulls `intel-media-va-driver` / `mesa-va-drivers`); NVIDIA needs `nvidia-vaapi-driver` | host |
| Injecting mouse/keyboard | write access to `/dev/uinput` — see §7 | host |
| H.264 decoding | nothing — the decoder is compiled into the binary. A VA-API driver moves it onto the GPU; without one it falls back to CPU decoding | client |

`vainfo` is the one-line check that the host role can work at all: it must list
`VAProfileH264Main` (or `High`) with **either** `VAEntrypointEncSlice` or
`VAEntrypointEncSliceLP`. Both are accepted — see §3 on why asking for only the first
one silently excludes most current Intel hardware.

Ubuntu 22.04+ on Wayland is the target. An Xorg session works too **as long as the
portal backend supports it** — the app never talks to X11 or Wayland directly.

**Building on 22.04 needs a newer CMake than the distro ships.** The repo root requires
CMake ≥ 3.25; Ubuntu 22.04 packages 3.22. Use `pip install cmake` or the Kitware APT
repository. 24.04 and later are fine as-is.

**The release binary is built on Ubuntu 22.04 on purpose.** A dynamically linked C++
binary runs on newer distros than the one that built it, never older: building on 24.04
produces something that needs `GLIBCXX_3.4.31`, which 22.04's libstdc++ (max 3.4.30)
does not have, and the app fails to start with a single linker error. Building on 22.04
needs only `GLIBCXX_3.4.29` and therefore runs on 22.04 and everything after it. CI
encodes this: `.github/workflows/build.yml` builds the Ubuntu app on **both**
`ubuntu-22.04` and `ubuntu-latest` but only ships the artifact from the older one — the
newer job exists to cover the newer toolchain and PipeWire 1.x.

**Why FFmpeg is vendored.** That "older builds run on newer" rule holds for glibc and
libstdc++ because their symbols are versioned, and for every other dependency here
because their SONAMEs never change (`libgtk-3.so.0`, `libva.so.2`,
`libpipewire-0.3.so.0`). libavcodec is the exception — it bumps its SONAME with each
FFmpeg major:

| | 22.04 | 24.04 | 26.04 |
|---|---|---|---|
| ships | `libavcodec.so.58` | `.so.60` | `.so.62` |
| `apt-cache policy libavcodec58` | installed | **Candidate: (none)** | **Candidate: (none)** |

So a 22.04 build asked for `.so.58` on a machine where no package can provide it, and
simply did not start. `dlopen`-ing whichever SONAME exists does **not** fix this: the
code reads `AVCodecContext` and `AVFrame` fields directly (`ctx->get_format`,
`f->data[3]`), whose offsets come from the headers at *compile* time while the struct is
allocated by the library at *run* time. Those layouts differ across majors — `avcodec.h`
guards `AVCodecContext.properties` with `#if FF_API_CODEC_PROPS` (major < 63), and it
sits mid-struct, so everything after it shifts. That failure is silent corruption, which
is worse than not starting.

`scripts/build-ffmpeg.sh` therefore builds FFmpeg 8.0 with `--disable-everything` plus
exactly `--enable-decoder=h264 --enable-hwaccel=h264_vaapi`, and links the two resulting
`.a` files in. Compile-time and run-time versions are then the same by construction. The
binary grows from ~530 KB to ~2.6 MB and `readelf -d` shows no `libav*` at all — CI
asserts that on every build. The configuration reports `LGPL version 2.1 or later` (no
`--enable-gpl`, no libx264), and since this repo ships the build script and the sources
are public, the LGPL relinking requirement is satisfied.

(FFmpeg **7.1** cannot build in this configuration: `CONFIG_H264_SEI` pulls in
`h2645_sei.o`, which calls `ff_aom_uninit_film_grain_params`, but `aom_film_grain.o` is
only compiled when HEVC is enabled — the link fails on a missing symbol. 8.0 moved
`aom_film_grain.o` into `CONFIG_H264_SEI`. Hence the pin.)

## 2. Capture — xdg-desktop-portal + PipeWire

This is the biggest structural difference from the other two desktop platforms, and it
inverts the source-selection flow.

Wayland does not let an application read the screen, full stop. The only path is to ask
the compositor through `xdg-desktop-portal` over D-Bus:

```
CreateSession → SelectSources → Start → OpenPipeWireRemote
                                  │
                                  └── the compositor shows a SYSTEM DIALOG
```

`Start` is where the user picks which monitors to share. So on Ubuntu,
`GetShareSources()` (`capture/SourceEnum.h`) does **not** enumerate — it *runs the
dialog* and returns what the user already chose. Consequences for anyone touching the
UI:

- Calling `GetShareSources()` **is** the Share button, not a step before it. Never call
  it on a refresh timer.
- The dialog appears **every time** sharing starts. The portal's `restore_token` is used
  (kept in memory only), which shortens the dialog on repeat use but does not remove it.
- Changing which monitors are shared requires a new portal session, i.e. Stop then Share.
  That is why `ShareWindow` has no add/remove-source buttons — a restriction the other
  platforms adopted by choice and Wayland enforces.

Frames then arrive over **PipeWire**, one `pw_thread_loop` per monitor
(`capture/ScreenCapture.h`), which mirrors "one queue per SCStream" on macOS.

Four portal/PipeWire traps are written up in the source and worth knowing before
editing: subscribing to the `Response` signal **before** issuing the call
(`capture/PortalScreenCast.cpp`), draining the buffer queue to keep only the newest
frame, the two-step DMA-BUF modifier fixation, and picking the target node through
`pw_stream_connect`'s `target_id` rather than the `PW_KEY_TARGET_OBJECT` property
(`capture/ScreenCapture.cpp`).

That last one is the nastiest, because it compiles either way: `PW_KEY_TARGET_OBJECT`
is the documented, non-deprecated route, but `pw_stream` only started honouring it in
PipeWire 0.3.64 while Ubuntu 22.04 ships 0.3.48 — where the constant exists, the build
succeeds, and the stream silently attaches to the default node instead of the screen the
user picked. `target_id` is honoured by every version we support.

⚠ **`DRM_FORMAT_MOD_INVALID` is a negotiation sentinel, not a layout.** The modifier
priority list (`kModifiers`) offers `LINEAR` **before** `INVALID`, and the order is
load-bearing. `INVALID` means "unspecified — use the implicit layout", which is widely
accepted while *negotiating* but tells the import side nothing about how the buffer is
arranged. Feeding it to `VADRMPRIMESurfaceDescriptor::drm_format_modifier` — the
explicit-modifier path — hands the driver a meaningless value; iHD rejects it with
`ALLOCATION_FAILED` at every resolution. `LINEAR` is unambiguous to both ends and always
imports. `INVALID` is still accepted as a last resort for compositors that do not offer
`LINEAR`, and `ImportDmaBuf` then routes it through the legacy `MEM_TYPE_DRM_PRIME` path
(`VASurfaceAttribExternalBuffers`, which has no modifier field) — the honest way to say
"unspecified". Both paths are verified pixel-exact at 1920×1080, 2560×1440, 3440×1440
and 3840×2160.

The symptom this produced is worth recognising, because it names no cause: capture runs
at full speed, `evt=enc_fail ms=0` repeats for every frame, and there is exactly **one**
explanatory log line — `ImportDmaBuf` deliberately logs once, not per frame, so it
scrolls away and everything after it is silent. When encode fails 100% with `ms=0`,
suspect frame import before suspecting the encoder.

## 3. Encode — VA-API, written directly

`encode/VaEncoder.h`. One `VADisplay` for the whole process, opened from a DRM render
node (`/dev/dri/renderD128…`), so it works identically under Wayland, Xorg, or no
graphical session at all.

The colour path runs entirely on the GPU:

```
dma-buf RGB ──import──► VA surface RGB ──VPP──► VA surface NV12 ──encode──► H.264
```

Low-latency settings match the other platforms: no B-frames (`ip_period = 1`), **infinite
GOP with on-demand IDR** (`intra_period = intra_idr_period = 0` — the driver never
inserts a keyframe on its own; `AgentLoop` decides, driven by the client's
`REQUEST_KEYFRAME`), CBR, exactly one reference frame, and an HRD buffer of half a
second so a single IDR cannot balloon and congest the link.

**Two encode entrypoints, and both must be probed.** `VAEntrypointEncSlice` is the
classic path (VME, running on the GPU's execution units); `VAEntrypointEncSliceLP` is
the low-power path (VDEnc, a dedicated fixed-function block). Intel dropped VME for
H.264 at Gen11 — on Ice Lake, Tiger Lake and Rocket Lake (the UHD 7xx parts) the iHD
driver advertises **only** `EncSliceLP`. `VaDisplay` prefers `EncSlice` and falls back
to `EncSliceLP`, and `VaEncoder` must pass back the same entrypoint `VaDisplay` settled
on, because `vaCreateConfig` validates the `(profile, entrypoint)` pair rather than
inferring it. Probing only `EncSlice` rejects perfectly capable hardware and — worse —
reports it as a missing driver.

⚠ **`EncSliceLP` on iHD is CQP-only**, so rate control is ours to do. Measured on
Rocket Lake / iHD 26.1.2: the LP entrypoint rejects `VA_RC_CBR`, `VA_RC_VBR` and
`VA_RC_VCM`, leaving `VA_RC_CQP`. Left alone that makes the target bitrate purely
advisory, and the failure it produced was not subtle: a 3440×1440 session sent a steady
~14 Mbps while `BitrateController` ratcheted the target 20 → 15 → 11 → … → 1.0 Mbps
trying to clear 20–28% loss. Congestion control had no actuator, so nothing improved;
the client asked for a keyframe every 250ms (`ClientSession::RequestKeyframe`), every
IDR was ~360 KB / 313 packets regardless of the target, and each retry re-flooded the
link it was reacting to.

`VaEncoder` therefore runs a **software QP loop whenever `cqpMode_` is set** (and stays
out of the way when the driver has real CBR). Three things make it work, each of which
was arrived at by measuring a failure:

- **`slice_qp_delta` is the knob, not `pic_init_qp`.** iHD's LP path ignores the
  picture-parameter `pic_init_qp` entirely — QP 20→42 via the slice delta moves output
  64→13 Mbps, while changing `pic_init_qp` moves nothing. `kPicInitQp` must stay equal
  to the `pic_init_qp_minus26 = 0` that `BuildParameterSets` writes into the PPS, since
  the delta is relative to it.
- **Filter the signal, integrate once.** P-frame budget comes from *elapsed wall time*
  (capture is damage-driven and was observed swinging 2–137 fps, so a nominal-fps budget
  overshoots badly at the top end), and the controller tracks an EMA of
  `ln(bytes / budget)`, stepping QP at most ±1 per frame. Two earlier attempts are
  written up in the source as ⚠ warnings: per-frame error chasing (oscillated, and made
  measured bitrate non-monotonic in the target) and adding buffer fullness into QP each
  frame (integral windup — QP railed at 45 while frames were 354 B against a 10 417 B
  budget).
- **IDRs get their own time-based budget.** Inheriting the P-frame QP is not enough: on
  a quiet desktop the P loop correctly drops QP to the floor, and IDRs inherited it, so
  across a 1→20 Mbps target range IDR size moved only 224 KB → 149 KB. Since IDRs are
  over half the bytes on a mostly-static screen — and are the burst that started the
  spiral — `IdrQp()` sizes each IDR from the previous one against `kIdrSeconds` of
  target bitrate, a one-step correction rather than a filtered one because IDRs are
  sparse and a client in trouble is asking four times a second.

Measured after the change at 3440×1440: QP settles 34 / 28 / 16 for 1 / 2 / 5 Mbps
targets (monotonic, as it must be), busy content tracks the target to 0.91–1.10 at
5–10 Mbps, and IDRs scale 64–75 KB at 1 Mbps against 216–224 KB at 20 Mbps.

**Known limit:** QP is the only actuator. At low targets on high-entropy content it
saturates at `kQpMax` and the encoder simply cannot reach the target — the remaining
levers would be dropping frames or scaling resolution, neither of which is implemented.
A QP pinned at `kQpMax` in the DIAG line (`VaEncoder::currentQp`) is the signal that
this is what is happening.

**We write SPS/PPS ourselves** (`encode/BitWriter.h`). VA-API encodes the slice, but
drivers differ on whether they emit parameter sets and essentially none repeat them per
IDR — which the protocol requires, because a client joining mid-stream has no other way
to get them. The consequence to respect: the constants describing the stream
(`kLog2MaxFrameNumMinus4`, `kLog2MaxPocLsbMinus4`, `kMaxRefFrames`) feed **both** the
packed SPS and `VAEncSequenceParameterBufferH264`. Change one side only and the client
decodes garbage from the second frame on, with symptoms that look exactly like packet
loss.

There is **no software fallback** for encoding, unlike Windows (NVENC → Media
Foundation → WARP). A machine without VA-API H.264 encode cannot host; it can still
view. CPU encoding at 60 fps would not meet the project's latency target, so a clear
error beats a bad experience.

## 4. Decode + render

**Decode** is `libavcodec` with the VA-API hwaccel (`decode/AvDecoder.h`) — the one place
the Linux port uses a third-party codec library, and the asymmetry is deliberate.
Encoding on raw VA-API means filling in structs for a stream whose parameters we choose.
Decoding on raw VA-API means parsing SPS/PPS/slice headers, computing POC, and managing
the DPB and reference picture lists by hand — half an H.264 decoder, for something
libavcodec has done correctly for years. libavcodec still decodes on the GPU here; it
only owns parsing and DPB management.

**Render** is `render/VideoRenderer.h`, an OpenGL path with a hard thread split:

| Thread | Touches |
|---|---|
| Decode | libva (`vaExportSurfaceHandle`), libav |
| GTK main | OpenGL, EGL |

Cutting there is what avoids a lock shared between libva and GL. Hardware frames stay in
VRAM end to end:

```
VASurface ──vaExportSurfaceHandle──► dma-buf ──eglCreateImageKHR──► EGLImage ──► texture
```

The fragment shader converts BT.709 **limited range** YUV→RGB, which must match the
`colour_description` `VaEncoder` writes into the SPS. Changing one side alone gives a
faint colour cast — bad enough to blame on the monitor, not obvious enough to notice.

## 5. Input injection — /dev/uinput

`input/InputInjector.h`. XTest is Xorg-only and cannot reach native Wayland clients; the
RemoteDesktop portal costs a second permission dialog and is relative-only on several
compositors. `uinput` creates virtual devices at the kernel level, so the compositor
cannot tell them from real hardware and input reaches every application.

**Three virtual devices, and that is mandatory.** libinput classifies a device by the
event types it declares, and a device with both relative and absolute axes is treated as
a plain mouse with its absolute axes ignored:

1. `Deskhub Keyboard` — `EV_KEY`, every key `LinuxKeyMap` can translate.
2. `Deskhub Mouse` — `EV_REL` + buttons + wheel. **All** clicks come from here.
3. `Deskhub Absolute Mouse` — `EV_ABS` (0..65535) + a declared-but-never-emitted
   `BTN_LEFT`, without which libinput would classify it as a tablet.

Absolute coordinates go through two nested conversions, because the wire carries
positions inside the *shared screen* while uinput spans the *whole desktop*:

```
normalized 0..65535 → position in the shared screen → global desktop position
                    → renormalized against the desktop bounding box → ABS value
```

Skip the middle step and on a two-monitor host the cursor always drifts to the left
monitor and only covers half the distance. Screen position/size come from the portal;
the desktop bounding box is measured with GDK in `ShareWindow.cpp` and passed down
through `AgentOptions` (the C++ layer stays GTK-free).

"Host wins" (`input/LocalInputMonitor.h`) reads `/dev/input/event*` directly and skips
any device whose name starts with `Deskhub` — simpler than the equivalents on Windows
(`LLMHF_INJECTED`) and macOS (stamping `kCGEventSourceUserData`), because here we named
the devices ourselves. Without read access it degrades to inert rather than failing: the
person at the machine simply loses priority.

## 6. Known limitations

- **Pointer lock (F9) is approximate.** True pointer lock needs the Wayland
  `pointer-constraints` + `relative-pointer` protocols, which GTK3 does not expose. The
  app hides the cursor, grabs the seat, and warps the pointer back to centre — which
  works on X11/XWayland but is a no-op on native Wayland, so the cursor can still reach
  the window edge and deltas stop there. Fixing it properly means dropping GTK3 or
  calling libwayland directly.
- **Mapped-memory fallback is slow.** If the compositor and the GPU driver cannot agree
  on a DMA-BUF modifier, frames arrive in RAM and have to be copied to the GPU. At 4K
  that is ~33 MB per frame; expect roughly 20–30 fps instead of 60. Each row's tooltip in
  the sharing window, and the `[DIAG] zerocopy=` field, say which path is live.
- **`SO_RCVBUF` is capped by the kernel.** Linux clamps it to `net.core.rmem_max`
  (often ~208 KB) — see §7.
- **The portal dialog is not parented to the Deskhub window**, because anchoring it
  needs an `xdg-foreign` handle that GTK3 does not expose. Cosmetic only.
- **No auto-restart of a portal session.** If the user presses "Stop sharing" on the
  compositor's own indicator, the session ends; press Share again.

## 7. One-time system setup

```bash
make setup-linux-permissions   # udev rule for /dev/uinput + add you to the 'input' group
# then LOG OUT and back in — group changes only apply to new sessions
```

That installs `/etc/udev/rules.d/60-deskhub-uinput.rules` and adds you to `input`, which
covers both `/dev/uinput` (injection) and `/dev/input/event*` ("host wins"). Without it
the app still runs and can still **view**; it just cannot inject into this machine.

Two optional tunings for high-bitrate streaming:

```bash
sudo sysctl -w net.core.rmem_max=8388608   # let the 4 MB SO_RCVBUF request stick
sudo ufw allow 47777/udp                   # only if ufw is enabled (off by default)
```

## 8. What the first run on real hardware settled

A two-machine LAN session (2026-07-30) exercised the whole path in both directions, which
retires the five risks this section used to list. Each was a place where the code could
only be *believed* correct until a real GPU and compositor were involved:

1. **VA-API encode produces a decodable stream** — the hand-written SPS/PPS agrees with
   what the driver emits. (Feared failure: first IDR decodes, everything after is garbage.)
2. **DMA-BUF import into VA-API**, including the two-step modifier fixation with PipeWire.
   (Feared failure: `ImportDmaBuf` fails every frame, driver and compositor disagreeing
   on buffer layout.)
3. **`vaExportSurfaceHandle` → EGLImage** on the client. (Feared failure: black window
   plus one `eglCreateImageKHR failed` line.)
4. **uinput coordinate mapping** — on a single-monitor host.
5. **Two machines over LAN** — the milestone every other platform calls M3.

Still open, because that run did not touch them:

- **uinput mapping on a multi-monitor host** — only the single-monitor case was exercised.
- **Multi-monitor sharing** (M3 for GĐ6, still open on Windows too).
- **Every performance number in this document.** They are design targets; no latency,
  bitrate, or frame-time measurement has been taken on Linux.

## 9. UI and flow — deliberately identical to the Windows app

The GTK3 UI is a port of `client/windows/win32/`, screen for screen, so that it does not
matter which machine you are sitting at: `MainWindow` ↔ `MainMenuWindow`, `ShareWindow` ↔
`SessionWindow`, `ViewerWindow` ↔ `Viewer` + `ViewerInput`.

**Main window** — two boxes and an Exit button, fixed size:

| Box | Contents |
|---|---|
| *Host mode* | this machine's IPv4 addresses, one **Copy** button per row · `UDP port 47777` · **FPS** · **Bitrate (Mbps)** · **Share…** |
| *Client mode* | **Host machine IP address** entry (Enter = Connect) · **Connect** |

There is no port field and no view-only checkbox: the port is `kDeskhubPort` and
mouse/keyboard are always shared, so either one would be a control with a single setting.

**The main window hides for the duration of a session** and comes back when it ends —
`ShowWindow(SW_HIDE)` / `SW_SHOW` around the blocking `RunAgent()` / `RunViewer()` on
Windows. Nothing can block here (GTK owns the main thread), so the same effect is built
from callbacks: `ShareWindow` fires one when it closes, and every `ViewerWindow` fires one
when it is destroyed. `MainWindow::openViewers_` counts the open viewers and restores the
window when the last one goes — what `g_openFrames` does in `Viewer.cpp`.

**Choosing what to view is multi-select**, like `SourcePickerDialog`: every source starts
selected and *each one you pick opens its own window*. A host sharing exactly one source
skips the dialog.

**Viewer windows are nothing but video.** Stats and the F9 hint live in the *title bar*
(`Deskhub - viewing: <source> — <stats> · <hint>`), the window resizes itself to the
negotiated video size on the first frame, and closing it ends the session — no overlay, no
Disconnect button. F10 (pause input) is the one control Windows does not have; it stays
because a Wayland pointer grab needs a visible way out.

Two differences are forced by the platform rather than chosen:

- **Share always goes through the compositor's dialog** (§2). Windows shares every attached
  display the moment the button is pressed; here the portal asks first. Picking more than
  `kMaxSources` (8) screens truncates to the first 8 with a warning, the same way Windows
  truncates its display list.
- **The sharing window cannot place itself** on native Wayland. It asks for the bottom-right
  corner of the work area as `SessionWindow` does, which X11/XWayland honours and a Wayland
  compositor ignores.

Per-source stats (fps, kbps, RTT, zero-copy) are not on the face of the sharing window,
because they are not on the Windows one either — they are on each row's **tooltip**, so the
layout matches and no diagnostic information is lost.

## 10. Where to go deeper

- `01-architecture.md` — system model and the per-OS backend matrix.
- `02-agent.md` / `03-client.md` — role internals, platform-independent.
- `06-transport.md`, `07-input.md` — the parts `core/` owns and Linux reuses unchanged.
- `09-diagnostics.md` — the `[DIAG]` lines this app emits, same catalogue as the others.
- `14-macos-app.md` — the closest sibling; the Linux `AgentLoop`/`ClientLoop` are ports
  of it, and reading them side by side is the fastest way to see what changed.
