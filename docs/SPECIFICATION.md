**English** · [Tiếng Việt](SPECIFICATION.vi.md)

# Deskhub — Functional Specification

The open-source project is **Deskhub**; the product name shown in the apps is **System Runtime**. This document describes **what** System Runtime does, as experienced by a person using it. It is
a product specification, not a design document: it contains no implementation detail, no
protocol description and no build instructions. Those live in
[`README.md`](../README.md), [`SECURITY.md`](../SECURITY.md) and the source tree.

- **Status:** describes the intended product behaviour. Where a running build still
  differs, the implementation is being aligned to this document — notably the optional
  session-encryption model in section 9.
- **Audience:** anyone who needs to know what the product is supposed to do — testers,
  reviewers, contributors, store listings.

---

## 1. Product summary

System Runtime lets one machine show its screen to other machines on the same network, and lets
those machines drive its mouse and keyboard. It is a single application: the same app
both shares a screen and views someone else's.

There is no installer requirement, no account, no sign-in, no system background service and
no cloud component. Two machines find each other by IP address on a network both can reach.
On Windows and macOS the user may optionally keep the app running in the notification area
after closing the window; that is not a system service.

## 2. Vocabulary

| Term | Meaning |
| --- | --- |
| **Host** | The machine whose screen is being shared. |
| **Client** / **Viewer** | The machine watching a host, and optionally controlling it. |
| **Source** | One shareable display on the host. A host may share several at once. |
| **Session** | One viewer watching one source. Each source opens in its own window. |
| **Passcode** | The 4-digit code a host requires for discovery and admission. Always required; not an encryption key. |
| **Session key** | The secret used when session encryption is on. The host generates it; viewers copy it when escrow is off. |
| **Escrow** | When encryption is on and escrow is on, the host passes the session key to connecting viewers automatically so they need not type it. |

A single machine can be host and client at the same time.

## 3. Roles by platform

| Platform | Can host | Can view |
| --- | :--: | :--: |
| Windows | ✅ | ✅ |
| macOS | ✅ | ✅ |
| Linux | ✅ | ✅ |
| Android | ✅ view-only | ✅ |
| iOS | ✅ view-only | ✅ |

Every platform offers the same client feature set unless stated otherwise in section 12.
Phones and tablets host in **view-only** mode: they stream their screen but never accept
remote input, because no mobile OS lets an ordinary app drive the device.

The app is organised into the same named sections everywhere: **Host**, **Client** and
**Settings**.

---

## 4. Hosting — sharing this machine's screen

| ID | Feature | Description |
| --- | --- | --- |
| H-1 | Display picker | Before sharing, the user ticks which of this machine's displays to expose. At least one must be ticked. |
| H-2 | Multi-display sharing | Several displays can be shared simultaneously; each becomes a separate source that viewers pick between. |
| H-3 | Source limit | A maximum of **8** displays can be shared at once. If the machine has more, the user is warned that only the first 8 will be shared. |
| H-4 | Start / stop sharing | One action starts sharing, one stops it. The current state is always shown (*Not sharing* / *Starting share…* / *Sharing*). |
| H-5 | Stop one display | An individual shared display can be stopped without ending the whole share. |
| H-6 | Connection details | While sharing, the app lists this machine's network addresses and the port viewers must use, so they can be read out or copied. On desktop the *Share on network* choice (T-9) sits on the hosting screen beside this list, and the list shows only the chosen network's address — *All networks* shows every address. While sharing (or starting to share) the choice is locked; stop sharing to change it. |
| H-7 | Live session table | For each shared display the host sees: display name, resolution, number of viewers, capture rate, send rate, bandwidth in use, and round-trip time. Each connected viewer appears as its own row under its display, identified by its display name and address — "Name (ip:port)" — when the viewer has set a name (C-7), or by the bare address otherwise. |
| H-8 | Disconnect a viewer | The host can drop any individual viewer from the session table. |
| H-9 | Viewer limit | At most **5** viewers may watch one host at a time. Further attempts are rejected as busy. |
| H-10 | Failure reporting | If sharing cannot start, the reason is shown to the user rather than failing silently. |

## 5. Connecting — viewing another machine

| ID | Feature | Description |
| --- | --- | --- |
| C-1 | Connect by address | The user types the host's IP address in one field and the UDP port in another, prefilled with the default `47777`. Pasting `192.168.1.10:47777` into the address field still works — its explicit port wins over the port field. Invalid input produces an explanatory hint, not a failure. |
| C-2 | Passcode entry | The user enters the 4 digits shown on the host. A code that is not exactly 4 digits is rejected before connecting; if the address is a known one, its remembered code is used when the field is left empty. The prompt that opens from the device lists also shows the device's UDP port, prefilled and editable. |
| C-2a | Session key entry | When the host has encryption on and escrow off, the viewer must supply the host's session key. The connect UI treats the session key as the primary secret in that mode; the passcode is still checked but is not emphasised for hand-off. When escrow is on, or encryption is off, no session-key field is required. |
| C-3 | Control opt-out | Before connecting, the viewer can untick *control the remote machine* to watch without sending any input. |
| C-4 | Source picker | If the host is sharing more than one display, the viewer is asked which to view. Picking several opens several windows. If the host shares exactly one display, it opens immediately. |
| C-5 | Clear failures | If the host cannot be reached, is not sharing, refuses the passcode, refuses the session key, or requires encryption that this viewer cannot satisfy, the viewer is told which — with the address named in the message. If this machine cannot create a viewing session (for example no usable D3D11 GPU on Windows), that is reported as its own clear failure. |
| C-6 | Session end notice | When a session ends, from either side, the viewer sees why. |
| C-7 | Viewer name | A *Your name* field on the connect page names this device. Until the user first sets a name, it is prefilled with a platform default: the computer's hostname on Windows and Linux (the login username if no hostname is available), the computer's name on macOS, the device name on iOS, and the device model on Android. The field can be edited, and whatever it contains when connecting is what is saved and sent. It can never end up unset: connecting with a cleared field falls back to the platform default above, which refills the field and is what is saved and sent, so a name always accompanies a connection. Hosts show the name next to this machine's address so viewers can be told apart. The name is remembered on this device, holds at most **64** bytes of text, and control characters are removed from it. A host running an older version simply does not show it. |

## 6. Finding machines

| ID | Feature | Description |
| --- | --- | --- |
| D-1 | Network scan | The client scans the local network for machines that are currently sharing and lists them, with progress shown while scanning ("*n* of *m* addresses checked"). When the scan finds nothing, the user is told why a machine may be absent: it appears only while it is sharing. |
| D-2 | Scan bounds | A scan covers at most **512** addresses on the local subnet. If the machine has no local network address, the user is told scanning is not possible. |
| D-3 | Automatic re-scan | The scan repeats periodically, and can be re-run on demand via *Refresh now*. |
| D-4 | Click to connect | Clicking a discovered device starts a connection to it. |
| D-5 | Recent devices | Machines connected to before are kept in a *Recent devices* list — up to **10** — showing address, status, ping and when they were last connected. |
| D-6 | Live status | Each recent device shows **Online**, **Offline** or **Checking…** with a round-trip time, refreshed automatically every **30 seconds** and on demand. |
| D-7 | Remembered secrets | The passcode used for a device is remembered with it. When a connection used encryption, that fact and the session key used are remembered too, so reconnecting can fill them in automatically. All of this is stored obscured, which is convenience — not protection (see section 9). |
| D-8 | Forget a device | A recent device can be removed from the list. |
| D-9 | Reconnect with encryption | Opening a recent device that was last connected with encryption first retries with the remembered passcode and session key. If that fails for a key or encryption reason, the app shows a session-key prompt, explains that the key is wrong or the host has rotated it, and lets the user paste a new key. |

## 7. Viewing a session

| ID | Feature | Description |
| --- | --- | --- |
| V-1 | Fit to window | The remote screen is scaled to fit the window, preserving aspect ratio, with the window sized to the source on open. On desktop, when the stream's shape genuinely changes mid-session — a phone or tablet host rotating, or a switch to a differently shaped display — the window re-fits itself to the new shape; quality changes at the same shape leave the window alone. |
| V-2 | Zoom and pan | The view can be zoomed up to **5×** and panned. Zoom level is displayed and can be reset in one action. |
| V-3 | Session status | The window shows a live status line: frame rate, bandwidth, round-trip time and end-to-end latency. |
| V-4 | Titled windows | Each viewer window is titled with the source it is showing plus its current status, so multiple sessions are distinguishable. |
| V-5 | Disconnect | The viewer can end the session at any time. |

## 8. Controlling the remote machine

| ID | Feature | Description |
| --- | --- | --- |
| I-1 | Mouse | Movement, left / right / middle / back / forward buttons, and the scroll wheel are sent to the host. |
| I-2 | Keyboard | Key presses and releases are sent, including modifier combinations. |
| I-3 | Pointer lock (desktop) | `F9` locks the mouse to the remote screen for games and other software that expects raw movement; `F9` or `Esc` releases it. The current state is shown in the window title. |
| I-4 | Focus safety | Losing focus releases the pointer lock and any keys still held down, so no key can be left stuck on the host. |
| I-5 | Touch trackpad (mobile) | On phones and tablets the video acts as a trackpad: drag moves the pointer, tap clicks, double-tap right-clicks, hold-and-drag drags, and a vertical two-finger drag scrolls. |
| I-6 | Pointer / pan mode (mobile) | A toggle switches between moving the remote pointer and panning a zoomed view. |
| I-7 | On-screen keyboard (mobile) | The device keyboard can be shown or hidden on demand and types into the remote machine. |
| I-8 | Hotkey bar (mobile) | Shortcut buttons for keys a touch keyboard makes awkward: `Esc`, `Tab`, `Enter`, the four arrows, `Del`, `Ctrl+C`, `Ctrl+V`. |
| I-9 | Host always wins | Input from the person physically at the host machine takes precedence over every remote viewer. |
| I-10 | One driver at a time | Only one viewer controls the mouse and keyboard at a time. The earliest to have joined wins contention; other viewers' input is ignored until the current driver has been idle for **1 second**. |
| I-11 | View-only enforcement | When the host has disabled control, or the viewer chose to watch only, no input reaches the host and the viewer window states that it is view-only. |

## 9. Access control and safety

| ID | Feature | Description |
| --- | --- | --- |
| S-1 | Optional session encryption | The host may turn on *Encrypt session traffic* (off by default). When on, video, input and clipboard for the session are encrypted end-to-end between host and viewers. Network discovery probes stay unencrypted. Intended for networks that are not fully trusted; a trusted LAN or VPN may leave it off. Details and residual risks are in [`SECURITY.md`](../SECURITY.md). |
| S-2 | Mandatory passcode | Every host requires a 4-digit passcode. One is generated at random on first launch; the user can change it but cannot leave it blank or switch it off. The passcode admits viewers and gates discovery; it is not the session encryption key. |
| S-3 | Passcode gates discovery | A host will not reveal what it is sharing without the correct passcode. |
| S-4 | Lockout on repeated failure | Wrong passcode or session-key attempts from the same source are counted. After **5** failures inside a **60-second** window the host refuses further attempts from that source for **30 seconds**. |
| S-4a | Discovery rate limit | Discovery probes from the same source are rate-limited so the display-list reply cannot be used as an unlimited oracle for guessing the passcode. |
| S-5 | Control switch | The host can share with *viewers can control this machine* turned off, making every session view-only regardless of what viewers request. |
| S-6 | Consent to capture | On platforms that require it, the operating system's own permission prompts and screen-picker dialogs are used; System Runtime cannot capture without the user granting it. |
| S-7 | Explicit sharing only | Nothing is shared until the user starts a share, or until launch if **Start sharing automatically when the app launches** is enabled in Settings. Stopping sharing, or fully quitting the app, ends all sessions. Closing the window to the notification area on Windows, macOS or Linux (when that option is on) leaves an active share running. |
| S-8 | Session key | When encryption is on, the host shows a generated session key with **Copy** and **Refresh**. The key is meant to be copied, not invented by the user. Refreshing invalidates the previous key and drops viewers that depended on it. |
| S-9 | Key lifetime | When encryption is on, the host chooses *Per share* (default) or *Persistent*. *Per share* generates a new session key each time sharing starts and discards it when sharing stops. *Persistent* keeps the same key across shares and restarts until the user refreshes it. |
| S-10 | Escrow key to viewers | When encryption is on, the host may turn on *Escrow key to viewers* (off by default; unavailable while encryption is off). With escrow on, a connecting viewer that presents the correct passcode receives the session key from the host and need not type it. With escrow off, the viewer must supply the session key locally (C-2a). Escrow is a convenience on the local network, not a substitute for a trusted path when hand-carrying the key. |
| S-11 | No plaintext downgrade | A host with encryption on refuses unencrypted admission. Viewers that cannot satisfy encryption are rejected with a clear failure (C-5), never silently accepted in the clear. |

## 10. Settings

Settings are per machine, persist across restarts, and apply the next time sharing
starts. Phones and tablets expose the network port (T-4) — which also decides which
port the network scan knocks on — clipboard sync (T-17), the passcode (T-5), optional
session encryption and its related controls (T-29–T-32), and the network to share on
(T-14) on their sharing screen; they host with the built-in defaults for everything else.

| ID | Setting | Range | Default |
| --- | --- | --- | --- |
| T-1 | Frame rate | 1 – 240 fps | 60 |
| T-2 | Bitrate | 1 – 1000 Mbps | 20 |
| T-3 | Quality | 720p · 1080p · 1440p · Native | 1080p |
| T-4 | Network port | 1 – 65535 | 47777 |
| T-5 | Passcode | exactly 4 digits | generated at random on first launch |
| T-6 | Viewers can control this machine | on / off | on |
| T-9 | Keep running in the background when closed | on / off (Windows, macOS and Linux) | off until the user chooses |
| T-13 | Hide the tray / menu bar icon | on / off (Windows and macOS); shown and enabled only while background running is on; turning background off clears this choice | off |
| T-14 | Share on network | All networks · one of this machine's addresses | All networks |
| T-15 | Start sharing when the app opens | on / off | off |
| T-16 | Start System Runtime when you log in | on / off | off |
| T-17 | Sync clipboard text | on / off | off |
| T-22 | Split log when larger than | 1 – 1024 MB (Windows, macOS, Linux) | 10 |
| T-23 | Compress logs older than | 0 – 3650 days; 0 means never (Windows, macOS, Linux) | 7 |
| T-24 | Delete logs older than | 0 – 3650 days; 0 means never; cannot be earlier than T-23 (Windows, macOS, Linux) | 30 |
| T-25 | Log directory | absolute writable folder, or blank for the default System Runtime folder (Windows, macOS, Linux) | blank (default folder) |
| T-27 | Language | System default · English · 简体中文 · Français · Deutsch · Русский · 日本語 · 한국어 · العربية | System default (follow the operating system) |
| T-29 | Encrypt session traffic | on / off | off |
| T-30 | Session key lifetime | Per share · Persistent; shown only while T-29 is on | Per share |
| T-31 | Escrow key to viewers | on / off; shown and enabled only while T-29 is on; turning T-29 off clears this to off | off |
| T-32 | Session key | generated value with Copy and Refresh; shown only while T-29 is on; not user-invented | generated when encryption is turned on, and again per S-8 / S-9 |

| ID | Feature | Description |
| --- | --- | --- |
| T-7 | Automatic quality | Stream quality adapts on its own to the available network capacity within the configured limits; no user action is required when conditions change. |
| T-8 | Validation | Out-of-range or non-numeric values are rejected and the previous value kept, rather than applied. An unusable log directory is rejected the same way. |
| T-10 | First-close background prompt | On Windows and macOS, the first time the main window is closed before the background preference has been recorded, the app asks whether to keep running in the background. **Yes** is selected by default. **Confirm** records the choice and applies it; **Close** leaves the preference unrecorded so the prompt appears again next time, and quits for this close. |
| T-11 | Persistent tray while background is on | While the background setting is on and the tray is not hidden, the notification-area / menu-bar icon stays visible even with the main window open. Closing the window to the background removes the app from the taskbar / Dock; restore via the tray icon, or by launching System Runtime again. A short notice is shown when the tray icon is visible. |
| T-12 | Quit confirmation while busy | On Windows and macOS, fully quitting while **Share** or a **Connect** viewer session is active asks for confirmation first. Closing the window to the background does not. |
| T-18 | Network fallback | When a specific network is chosen (T-14), the host is reachable only through that address. If that address no longer exists when sharing starts, the host shares on all networks instead and says so in the sharing status. A saved address that is currently unavailable is still listed, marked *not connected*. |
| T-19 | Auto-share on launch | Desktop only. With T-15 on, opening the app goes straight to the Host page and starts sharing with the saved settings, exactly as if the user had pressed Share. The platform rules still apply: Linux first shows the desktop's screen-sharing dialog (P-3), and macOS still requires its permissions (P-2). |
| T-20 | Launch at login | Desktop only. With T-16 on: Linux writes an autostart entry into `~/.config/autostart`; Windows registers a scheduled task named *System Runtime* that starts the app elevated at logon, so no UAC prompt appears; macOS registers a Login Item the user can also see in System Settings. Turning it off removes that artifact again. The checkbox always shows what the operating system reports, not merely what was last saved. |
| T-21 | Clipboard sync | With T-17 on, plain text copied on any machine in the session appears on the others within a couple of seconds, in both directions; the host relays a viewer's copy to the other viewers. Text is capped at 32 KiB (longer copies are cut at a whole character); images, files and formatting are never transferred. The host's toggle governs the session: with it off, the host ignores and never sends clipboard data. Each machine also needs its own toggle on to read or write its local clipboard. On Android and iOS the operating system constrains this: an Android device picks up its own copies only while System Runtime is the app in the foreground, though incoming text is applied at any time; an iOS viewer may show the system paste prompt when System Runtime reads a fresh copy; and an iOS device that is hosting does not take part at all, because its broadcast runs in a separate process without clipboard access. |
| T-26 | Log details | On Windows, macOS and Linux the Settings page lists local log files, shows their contents, and can open the log folder. Compressed `.log.gz` files appear in the list but are opened from the folder rather than shown inline. |
| T-28 | Language preference | The Settings page offers a language choice (T-27). **System default** follows the operating system's locale and maps common tags such as `zh-CN`, `fr-FR` and `ja` onto the supported list, falling back to English when the tag is unknown. An explicit choice is stored and applied the next time the app starts; changing it while the app is open updates newly shown strings immediately, while labels already drawn on the main window may need a restart. |
| T-33 | Session encryption controls | With T-29 on, Settings (and the sharing screen on phones and tablets) show the current session key (T-32), lifetime (T-30) and escrow (T-31). Turning encryption off hides those controls and forces escrow off. Copy places the key on the local clipboard; Refresh follows S-8. |

## 11. Status and troubleshooting

| ID | Feature | Description |
| --- | --- | --- |
| G-1 | Live host statistics | Per-display and per-viewer figures for capture rate, send rate, bandwidth and round-trip time. |
| G-2 | Live client statistics | Per-session frame rate, bandwidth, round-trip time and end-to-end latency. |
| G-3 | Session logs | On Windows, macOS and Linux the app appends to one log file per calendar day in the configured log directory (the user's System Runtime folder by default), for attaching to bug reports. A process start writes a short banner with version, host identity, local addresses and current settings; Share and Connect also leave an entry. A new file is started only when the current one grows past the configured size (the full file is archived with a timestamp). Older files are compressed and later deleted according to the Settings retention values. Changing the log directory affects new writes only; existing files stay where they are. Android and iOS write their diagnostics to the operating system's own log stream instead and leave no file behind. |
| G-4 | Version and project link | The app displays its version and links to the project page. |

## 12. Platform-specific behaviour

| ID | Platform | Behaviour |
| --- | --- | --- |
| P-1 | Windows | The app asks for administrator rights once at start, which is what allows it to type into elevated windows. It adds its own firewall rule when sharing begins. Only one instance may run; a second launch shows a notice and exits. When the background setting is on, a notification-area icon is always shown; left-click restores the window and right-click offers **Restore** / **Exit**. Closing the window while background is on shows a short balloon that System Runtime is still running. |
| P-2 | macOS | Shows a **Permissions** panel with the live grant state of *Screen Recording* (needed to share) and *Accessibility* (needed to accept remote input), a button to request each, and a shortcut into System Settings. Some keystrokes are silently blocked by macOS unless Accessibility is granted. Only one instance may run; a second launch shows a notice and exits. When the background setting is on, a menu-bar icon is always shown; left-click restores the window and right-click offers **Restore** / **Exit**. Closing the window while background is on shows a short notice that System Runtime is still running. |
| P-3 | Linux | Displays are chosen in the desktop's own screen-sharing dialog after pressing Share, rather than in the app. Sharing additionally requires the system to permit input injection. |
| P-4 | Android / iOS | Hosting is **view-only**: the device streams its screen and silently drops every control packet, because neither OS lets an app inject input system-wide. The whole screen is shared as a single source, so the display picker, multi-display sharing and per-display stop (H-1, H-2, H-3, H-5) do not apply. Turning the device turns the stream with it: what viewers see stays the right way up, and their window re-fits to the new shape (V-1). The session UI is touch-first: trackpad gestures, zoom controls, hotkey bar, on-screen keyboard, display switcher and **End**. |
| P-5 | Android | Sharing needs the system screen-recording consent dialog, which is granted per share and cannot be remembered. While sharing, an ongoing notification is shown and the stream survives the app going to the background or the screen turning off. Stopping the share from the system notification ends the session. |
| P-6 | iOS | Sharing is started from an in-app **Start sharing** button which opens the system broadcast sheet, because iOS requires that sheet to confirm every broadcast, and runs in a separate broadcast process so it continues after the app is closed. The sharing screen reports the number of connected viewers — listing the names of those that have set one (C-7) — and the broadcast process's current memory use — iOS ends a broadcast that grows past its memory limit — without the per-viewer table of H-7, and viewers cannot be dropped individually (H-8). A system event that ends the broadcast — an incoming call, for instance — ends the session. |

## 13. Explicitly out of scope

System Runtime does **not** provide, and this specification does not cover:

- Audio streaming.
- File transfer or remote printing.
- Clipboard sync beyond plain text (images, files, rich text).
- Any account, directory, presence or invitation system.
- Relay, rendezvous or NAT-traversal service — reaching a host over the internet is the
  user's responsibility (for example via a VPN).
- Session recording.
- Unattended access, wake-on-LAN, or remote power control.
- Authentication of machine identity beyond the passcode and optional session key,
  mutual device attestation, or a public-key directory.
- Multi-user administration, roles, or audit trails.
