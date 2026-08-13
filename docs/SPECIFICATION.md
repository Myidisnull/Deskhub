**English** · [Tiếng Việt](SPECIFICATION.vi.md)

# Deskhub — Functional Specification

This document describes **what** Deskhub does, as experienced by a person using it. It is
a product specification, not a design document: it contains no implementation detail, no
protocol description and no build instructions. Those live in
[`README.md`](../README.md), [`SECURITY.md`](../SECURITY.md) and the source tree.

- **Status:** describes the behaviour of the current code.
- **Audience:** anyone who needs to know what the product is supposed to do — testers,
  reviewers, contributors, store listings.

---

## 1. Product summary

Deskhub lets one machine show its screen to other machines on the same network, and lets
those machines drive its mouse and keyboard. It is a single application: the same app
both shares a screen and views someone else's.

There is no installer requirement, no account, no sign-in, no background service and no
cloud component. Two machines find each other by IP address on a network both can reach.

## 2. Vocabulary

| Term | Meaning |
| --- | --- |
| **Host** | The machine whose screen is being shared. |
| **Client** / **Viewer** | The machine watching a host, and optionally controlling it. |
| **Source** | One shareable display on the host. A host may share several at once. |
| **Session** | One viewer watching one source. Each source opens in its own window. |
| **Passcode** | The 4-digit code a host requires before a viewer is admitted. |

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
| C-3 | Control opt-out | Before connecting, the viewer can untick *control the remote machine* to watch without sending any input. |
| C-4 | Source picker | If the host is sharing more than one display, the viewer is asked which to view. Picking several opens several windows. If the host shares exactly one display, it opens immediately. |
| C-5 | Clear failures | If the host cannot be reached, is not sharing, or refuses the passcode, the viewer is told which — with the address named in the message. |
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
| D-7 | Remembered passcode | The passcode used for a device is remembered with it, so reconnecting does not require retyping it. It is stored obscured, which is convenience — not protection (see section 9). |
| D-8 | Forget a device | A recent device can be removed from the list. |

## 7. Viewing a session

| ID | Feature | Description |
| --- | --- | --- |
| V-1 | Fit to window | The remote screen is scaled to fit the window, preserving aspect ratio, with the window sized to the source on open. |
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
| I-5 | Touch trackpad (mobile) | On phones and tablets the video acts as a trackpad: drag moves the pointer, tap clicks, hold-and-drag drags, and a vertical two-finger drag scrolls. |
| I-6 | Pointer / pan mode (mobile) | A toggle switches between moving the remote pointer and panning a zoomed view. |
| I-7 | On-screen keyboard (mobile) | The device keyboard can be shown or hidden on demand and types into the remote machine. |
| I-8 | Hotkey bar (mobile) | Shortcut buttons for keys a touch keyboard makes awkward: `Esc`, `Tab`, `Enter`, the four arrows, `Del`, `Ctrl+C`, `Ctrl+V`. |
| I-9 | Host always wins | Input from the person physically at the host machine takes precedence over every remote viewer. |
| I-10 | One driver at a time | Only one viewer controls the mouse and keyboard at a time. The earliest to have joined wins contention; other viewers' input is ignored until the current driver has been idle for **1 second**. |
| I-11 | View-only enforcement | When the host has disabled control, or the viewer chose to watch only, no input reaches the host and the viewer window states that it is view-only. |

## 9. Access control and safety

| ID | Feature | Description |
| --- | --- | --- |
| S-1 | No encryption | Deskhub does not encrypt anything. It is intended for trusted networks or a VPN. This is stated in the app and documented in [`SECURITY.md`](../SECURITY.md). |
| S-2 | Mandatory passcode | Every host requires a 4-digit passcode. One is generated at random on first launch; the user can change it but cannot leave it blank or switch it off. |
| S-3 | Passcode gates discovery | A host that requires a passcode will not even reveal what it is sharing without it. |
| S-4 | Lockout on repeated failure | **3** wrong passcode attempts lock the host against further attempts for **30 seconds**. |
| S-5 | Control switch | The host can share with *viewers can control this machine* turned off, making every session view-only regardless of what viewers request. |
| S-6 | Consent to capture | On platforms that require it, the operating system's own permission prompts and screen-picker dialogs are used; Deskhub cannot capture without the user granting it. |
| S-7 | Explicit sharing only | Nothing is shared until the user starts a share. Closing or stopping ends all sessions. |

## 10. Settings

Settings are per machine, persist across restarts, and apply the next time sharing
starts. Phones and tablets expose only the network port (T-4) — which also decides which
port the network scan knocks on — plus the passcode (T-5) on their sharing screen; they
host with the built-in defaults for everything else.

| ID | Setting | Range | Default |
| --- | --- | --- | --- |
| T-1 | Frame rate | 1 – 240 fps | 60 |
| T-2 | Bitrate | 1 – 1000 Mbps | 20 |
| T-3 | Quality | 720p · 1080p · 1440p · Native | 1080p |
| T-4 | Network port | 1 – 65535 | 47777 |
| T-5 | Passcode | exactly 4 digits | generated at random on first launch |
| T-6 | Viewers can control this machine | on / off | on |
| T-9 | Share on network | All networks · one of this machine's addresses | All networks |
| T-11 | Start sharing when the app opens | on / off | off |
| T-13 | Start Deskhub when you log in | on / off | off |
| T-15 | Keep running in the background | on / off | off |
| T-17 | Sync clipboard text | on / off | off |

| ID | Feature | Description |
| --- | --- | --- |
| T-7 | Automatic quality | Stream quality adapts on its own to the available network capacity within the configured limits; no user action is required when conditions change. |
| T-8 | Validation | Out-of-range or non-numeric values are rejected and the previous value kept, rather than applied. |
| T-10 | Network fallback | Desktop only. When a specific network is chosen (T-9), the host is reachable only through that address. If that address no longer exists when sharing starts, the host shares on all networks instead and says so in the sharing status. A saved address that is currently unavailable is still listed, marked *not connected*. |
| T-12 | Auto-share on launch | Desktop only. With T-11 on, opening the app goes straight to the Host page and starts sharing with the saved settings, exactly as if the user had pressed Share. The platform rules still apply: Linux first shows the desktop's screen-sharing dialog (P-3), and macOS still requires its permissions (P-2). |
| T-14 | Launch at login | Desktop only. With T-13 on: Linux writes an autostart entry into `~/.config/autostart`; Windows registers a scheduled task named *Deskhub* that starts the app elevated at logon, so no UAC prompt appears; macOS registers a Login Item the user can also see in System Settings. Turning it off removes that artifact again. The checkbox always shows what the operating system reports, not merely what was last saved. |
| T-16 | Background mode | Desktop only. With T-15 on, a tray / menu-bar icon appears with *Show/Hide window*, *Start/Stop sharing* and *Quit*; closing the window hides the app instead of quitting it, and sharing continues in the background. The window always appears on launch and hides only when the user closes it, so T-13 + T-11 + T-15 together start sharing at login with the window shown until it is closed. On macOS the Dock icon disappears while the window is hidden. On Linux the tray needs a StatusNotifier host (standard on KDE; GNOME needs the AppIndicator extension) — without one, closing the window still quits, so the app can never become unreachable. |
| T-18 | Clipboard sync | Desktop only. With T-17 on, plain text copied on any machine in the session appears on the others within a couple of seconds, in both directions; the host relays a viewer's copy to the other viewers. Text is capped at 32 KiB (longer copies are cut at a whole character); images, files and formatting are never transferred. The host's toggle governs the session: with it off, the host ignores and never sends clipboard data. Each machine also needs its own toggle on to read or write its local clipboard. |

## 11. Status and troubleshooting

| ID | Feature | Description |
| --- | --- | --- |
| G-1 | Live host statistics | Per-display and per-viewer figures for capture rate, send rate, bandwidth and round-trip time. |
| G-2 | Live client statistics | Per-session frame rate, bandwidth, round-trip time and end-to-end latency. |
| G-3 | Session logs | On Windows, macOS and Linux each run writes a log file to the user's Deskhub folder, for attaching to bug reports. Android and iOS write their diagnostics to the operating system's own log stream instead and leave no file behind. |
| G-4 | Version and project link | The app displays its version and links to the project page. |

## 12. Platform-specific behaviour

| ID | Platform | Behaviour |
| --- | --- | --- |
| P-1 | Windows | The app asks for administrator rights once at start, which is what allows it to type into elevated windows. It adds its own firewall rule when sharing begins. |
| P-2 | macOS | Shows a **Permissions** panel with the live grant state of *Screen Recording* (needed to share) and *Accessibility* (needed to accept remote input), a button to request each, and a shortcut into System Settings. Some keystrokes are silently blocked by macOS unless Accessibility is granted. |
| P-3 | Linux | Displays are chosen in the desktop's own screen-sharing dialog after pressing Share, rather than in the app. Sharing additionally requires the system to permit input injection. |
| P-4 | Android / iOS | Hosting is **view-only**: the device streams its screen and silently drops every control packet, because neither OS lets an app inject input system-wide. The whole screen is shared as a single source, so the display picker, multi-display sharing and per-display stop (H-1, H-2, H-3, H-5) do not apply. The session UI is touch-first: trackpad gestures, zoom controls, hotkey bar, on-screen keyboard, display switcher and **End**. |
| P-5 | Android | Sharing needs the system screen-recording consent dialog, which is granted per share and cannot be remembered. While sharing, an ongoing notification is shown and the stream survives the app going to the background or the screen turning off. Stopping the share from the system notification ends the session. |
| P-6 | iOS | Sharing is started from an in-app **Start sharing** button which opens the system broadcast sheet, because iOS requires that sheet to confirm every broadcast, and runs in a separate broadcast process so it continues after the app is closed. The sharing screen reports the number of connected viewers — listing the names of those that have set one (C-7) — and the broadcast process's current memory use — iOS ends a broadcast that grows past its memory limit — without the per-viewer table of H-7, and viewers cannot be dropped individually (H-8). A system event that ends the broadcast — an incoming call, for instance — ends the session. |

## 13. Explicitly out of scope

Deskhub does **not** provide, and this specification does not cover:

- Audio streaming.
- File transfer or remote printing.
- Clipboard sync beyond plain text (images, files, rich text).
- Any account, directory, presence or invitation system.
- Relay, rendezvous or NAT-traversal service — reaching a host over the internet is the
  user's responsibility (for example via a VPN).
- Session recording.
- Unattended access, wake-on-LAN, or remote power control.
- Encryption, authentication of machine identity, or transport-level integrity.
- Multi-user administration, roles, or audit trails.
