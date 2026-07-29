# 17 — Ubuntu app (both roles)

The Ubuntu app is **one executable, `deskhub`**, containing both roles like Windows
(`Deskhub.exe`) and macOS (`app.app`): **Share** a screen or **Connect** to another
machine, chosen at runtime. Tree: `client/linux/` — `cpp/` (native layer, no GTK) +
`gtk/` (GTK3 UI). All protocol logic is the shared `core/`; this document covers only
the Linux backends.

> **Status (2026-07-29): code complete, NOT yet run on a real machine.** Everything
> here compiles clean (`-Wall -Wextra`, zero warnings) and links, and `core_tests`
> passes, but the pipeline has never produced a frame on real hardware. §8 lists what
> still has to be verified. Treat every performance number as a design target, not a
> measurement.

## 1. Requirements

**Build** (`make bootstrap` installs all of these):

```
libgtk-3-dev libglib2.0-dev libepoxy-dev libegl-dev libgles-dev
libdrm-dev libva-dev libpipewire-0.3-dev libspa-0.2-dev
libavcodec-dev libavutil-dev  pkg-config  cmake  ninja-build
```

**Run:**

| Need | Package | Which role |
|---|---|---|
| Screen capture permission | `xdg-desktop-portal` + a backend for your compositor: `-gnome`, `-kde` or `-wlr` | host |
| H.264 encoding on the GPU | `va-driver-all` (pulls `intel-media-va-driver` / `mesa-va-drivers`); NVIDIA needs `nvidia-vaapi-driver` | host |
| Injecting mouse/keyboard | write access to `/dev/uinput` — see §7 | host |
| H.264 decoding | `libavcodec` + a VA-API driver (falls back to CPU decoding without one) | client |

`vainfo` is the one-line check that the host role can work at all: it must list
`VAProfileH264Main` with `VAEntrypointEncSlice`.

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
  that is ~33 MB per frame; expect roughly 20–30 fps instead of 60. The share window and
  the `[DIAG] zerocopy=` field say which path is live.
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

## 8. What still has to be verified on real hardware

Nothing below has been exercised — no GPU, compositor, or `/dev/uinput` exists in the
build container. In rough order of risk:

1. **VA-API encode produces a decodable stream.** The hand-written SPS/PPS must agree
   with what the driver actually encodes. Failure mode: the first IDR decodes and
   everything after it is garbage.
2. **DMA-BUF import into VA-API**, and the two-step modifier fixation with PipeWire.
   Failure mode: `ImportDmaBuf` fails every frame and the log says the driver and the
   compositor disagree on buffer layout.
3. **`vaExportSurfaceHandle` → EGLImage** on the client. Failure mode: black window with
   one `eglCreateImageKHR failed` line.
4. **uinput coordinate mapping** on single- and multi-monitor hosts.
5. **Two machines over LAN** — the milestone every other platform calls M3.
6. Multi-monitor sharing (M3 for GĐ6, still open on Windows too).

## 9. Where to go deeper

- `01-architecture.md` — system model and the per-OS backend matrix.
- `02-agent.md` / `03-client.md` — role internals, platform-independent.
- `06-transport.md`, `07-input.md` — the parts `core/` owns and Linux reuses unchanged.
- `09-diagnostics.md` — the `[DIAG]` lines this app emits, same catalogue as the others.
- `14-macos-app.md` — the closest sibling; the Linux `AgentLoop`/`ClientLoop` are ports
  of it, and reading them side by side is the fastest way to see what changed.
