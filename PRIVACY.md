**English** · [中文](PRIVACY.zh.md) · [Tiếng Việt](PRIVACY.vi.md)

# Deskhub Privacy Policy

_Effective date: August 14, 2026 — Version 1.10_

> Translations: [`PRIVACY.zh.md`](PRIVACY.zh.md) (中文), [`PRIVACY.vi.md`](PRIVACY.vi.md)
> (Tiếng Việt). This English version is the authoritative one.

## 1. Introduction

This Privacy Policy describes how **System Runtime** (open-source project **Deskhub**; "the app", "we") handles
information when you use the System Runtime / Deskhub mobile applications (iOS, Android) and the
desktop applications for Windows, macOS and Linux (together, "the
Software").

System Runtime is a remote desktop application: it streams the screen of one of your
computers to another device of yours and lets you control that computer with
mouse, keyboard and touch input.

The Software is developed and published by an individual developer:

- **Developer:** Manh Pham
- **Contact:** manhpv151090@gmail.com
- **Project page:** https://github.com/manhpham90vn/Deskhub

## 2. The short version

**System Runtime does not collect, store, sell, or share any personal data. We do not
operate any servers, and no data about you or your usage ever reaches us or any
third party through the Software.** There are no user accounts, no analytics,
no crash reporting, no advertising, and no third-party SDKs embedded in the
Software.

## 3. Information the Software processes

To function, the Software must process certain data **entirely on and between
your own devices**. None of it is transmitted to the developer or to any third
party.

| Data | Purpose | Where it goes | Retention |
|---|---|---|---|
| Screen content of the shared computer (video frames) | Displaying that screen on your other device | Sent directly between your two devices; when *Encrypt session traffic* is on, session payloads are encrypted by System Runtime, otherwise only by your own network/VPN layer if any | Never stored; exists only in memory during the session |
| Mouse, keyboard, and touch input | Controlling the shared computer from your other device | Sent directly from the viewing device to the shared computer (encrypted with the session when encryption is on) | Never stored; discarded after injection |
| The address (IP/hostname) you type | Connecting to the other machine | Stays on the device you typed it on | Kept locally until you change it |
| The last 10 addresses you connected to, the time of each, the passcode you used for each, and when applicable whether the connection used encryption and the session key used | Filling in the *Recent devices* list so you can reconnect with one click | Written to `recent-devices.txt` in the app's own folder on your device — `%USERPROFILE%\.system-runtime` on Windows, `~/.system-runtime` on macOS and Linux, the app sandbox on iOS and Android | Kept until you connect to 10 newer addresses, or you delete the file |
| Your sharing preferences (frame rate, bitrate, resolution cap, port, which network address to share on, whether viewers may control the machine, the clipboard-sync, start-with-OS, auto-share and background-mode toggles, log retention limits, optional custom log directory, the passcode you ask viewers for, and when used the session-encryption toggles, key lifetime, escrow choice and session key) | Restoring your settings the next time you open the app | Written to `ui-settings.txt` in the same folder. On iOS the file lives in the app group container shared by the app and its broadcast extension, so both halves agree on your passcode, port and encryption settings | Kept until you change them or delete the file |
| Clipboard text (only while the clipboard-sync toggle is on and a session is active) | Making text copied on one device pastable on the others | Sent directly between your devices — encrypted with the session when encryption is on, otherwise cleartext like other unencrypted traffic — capped at 32 KiB per copy; only plain text, never images or files | Never stored by System Runtime; lives only in each device's normal system clipboard |
| Diagnostic log files written while the desktop apps run (`system-runtime-*.log`, and older ones compressed to `system-runtime-*.log.gz`) | Local troubleshooting and attaching to bug reports you choose to send | Written under the log directory you choose in Settings, or under `~/.system-runtime/` on macOS and Linux / `%USERPROFILE%\.system-runtime` on Windows when that setting is blank. Shown in Settings on those platforms. Never uploaded automatically | One file per calendar day across launches (appends). Split when a file exceeds the size you set (default 10 MB). Plain logs older than the compress age (default 7 days) become `.log.gz`. Files older than the delete age (default 30 days) are removed. You can change those ages and the folder in Settings, or delete the files yourself |
| Whether a broadcast is currently running, how many viewers are connected, the broadcast extension's own memory use in megabytes, and the text of the last start-up error (iOS only) | Letting the app's sharing screen report the state of the broadcast extension, which iOS runs as a separate process and terminates if it uses too much memory | Written to `broadcast-status.txt` in the same app group container | Deleted when the broadcast ends |
| The device name in the *Your name* field — prefilled with this computer or device's own name (its hostname on Windows and Linux, its computer name on macOS, its device name on iOS, its model on Android) until you edit it | Shown on the host you connect to, next to this device's address, so the person sharing can tell viewers apart | Saved in `ui-settings.txt` in the same folder, and sent to the host when you connect — in the clear, like the rest of the traffic — so this default name is transmitted unless you replace it with a name of your choice; clearing the field only restores the default, which is then saved and sent. The host keeps it only in memory, only for the duration of the connection | Defaults to the computer or device's name; kept until you change it or delete the file. Clearing the field restores the default rather than removing the name |
| Connection statistics (bitrate, packet loss, latency) | Adapting stream quality; shown in the status bar | Exchanged only between your two devices | Never stored; discarded when the session ends |

### 3.1 Peer-to-peer by design

All communication happens **directly between your own two devices** over:

- your local network (Wi-Fi/LAN), or
- a VPN that **you** operate or subscribe to (for example Tailscale), if you
  choose to use one for access over the Internet.

We do not operate relay servers, signaling servers, or any other backend. The
Software has no technical means to send data to the developer.

### 3.2 Data we do NOT process

The Software does not access or process: your name (beyond the device name
described above, which defaults to your computer or device's own name), email address, phone
number, contacts, location, photos, files (other than what is visible on the
PC screen you choose to stream), microphone, camera, advertising identifiers,
or any device identifiers beyond what the operating system needs to run the
app.

### 3.3 Sharing a phone or tablet screen

Android and iOS devices can share their own screen as well as view another
machine's. The stream is **view-only**: incoming mouse and keyboard packets are
discarded, because neither operating system lets an ordinary app drive the
device. What is captured is the **whole screen**, including anything that
appears while sharing — notifications, other apps, banking apps, passwords you
type. On Android the system shows its own recording-consent dialog for every
share and a permanent notification while it runs; on iOS the system broadcast
indicator stays visible. Both are the operating system's own signals, and
either can be used to stop sharing at any time. As on desktop, the video goes
directly to your other device and is never stored or sent to us.

### 3.4 Scope of screen sharing and remote control

Sharing streams the **entire selected display**: everything that appears on
that monitor is visible to the connected viewer, including notifications,
pop-ups, and any window you open while sharing. (Sharing a single application
window was removed on 2026-07-27; the Software now shares whole displays
only.) When you allow remote control, the viewer's input is injected as if
they were sitting at the PC and can reach **any application visible on the
shared display** — it is no longer limited to one window. On any host you can
turn remote control off entirely in Settings, which makes the share view-only:
input that arrives is discarded instead of injected. Two safety
mechanisms remain active whenever control is allowed: if the person at the PC
touches the real mouse or keyboard, remote input pauses ("host wins"), and any
keys held by the remote side are automatically released when the connection
ends or the viewer switches away. Up to five viewers can watch one PC at once,
but only one of them drives the mouse and keyboard at any moment.

## 4. Permissions the apps request

| Platform | Permission | Why |
|---|---|---|
| iOS | Local Network | Required by iOS to send/receive traffic to your PC on the same network. Used only for the streaming session. |
| iOS | Screen recording (broadcast) | Only when you start sharing this device's screen, from the system broadcast picker. iOS asks every time and shows a recording indicator throughout. |
| Android | `INTERNET`, network state | Required to open the UDP connection to your PC. Used only for the streaming session. |
| Android | Screen capture consent (`MediaProjection`) | Only when you start sharing this device's screen. Android asks every time; the answer cannot be remembered. |
| Android | `FOREGROUND_SERVICE`, `FOREGROUND_SERVICE_MEDIA_PROJECTION` | Keeps the share running while the app is in the background or the screen is off. Required by Android for screen capture. |
| Android | `POST_NOTIFICATIONS` | Shows the ongoing notification Android requires while a screen share is running. No other notifications are sent. |

The apps request no other permissions. If a future version needs a new
permission, it will be requested in-context and this policy will be updated.

## 5. Analytics, advertising, and third parties

- **Analytics / telemetry:** none.
- **Crash reporting:** none. Diagnostic logs (`[DIAG]`) exist only on your own
  machine — in the app's console output and, on Windows, macOS and Linux, in
  plain-text (and eventually gzip) files under the log directory you choose in
  Settings, or under `~/.system-runtime/` (`%USERPROFILE%\.system-runtime` on Windows) when
  that setting is blank. Settings also controls how large each file may grow
  and how long old files are kept. They are never uploaded anywhere; they leave
  your device only if you copy and send them yourself, and you can delete those
  files at any time.
- **Advertising:** none.
- **Third-party SDKs:** none. The Software is built only from its own source
  code (available at the project page) and operating-system frameworks.
- **App stores:** the apps are distributed through Apple App Store and Google
  Play. Apple and Google may collect installation/usage statistics under their
  own privacy policies; that collection is outside our control and we receive
  only the aggregated, anonymous statistics those platforms show to every
  developer.
- **Tailscale or other VPNs:** if you choose to connect through a VPN, your
  traffic is handled under that provider's privacy policy. System Runtime neither
  requires nor bundles any VPN.

## 6. Security

- Streaming traffic stays inside your own network or your own VPN tunnel.
  When you use a VPN such as Tailscale, traffic between devices is end-to-end
  encrypted by that VPN (WireGuard).
- On a plain local network, System Runtime does not encrypt session traffic unless you
  turn on *Encrypt session traffic*. Every host requires a 4-digit passcode before
  accepting a connection — one is generated for you on first launch and you can change
  it — but with encryption off that code travels in the clear like the rest of the
  traffic. With encryption on, session video, input and clipboard are encrypted; discovery
  probes stay cleartext; the host-generated session key is copied to viewers unless escrow
  is on. The device name can still travel on clear paths and is displayed on the host, so
  do not put anything sensitive in it. Use System Runtime only on networks you trust, or
  through a VPN, and never expose it to the Internet directly. The full threat model — what
  is protected, what is not, and how to report a vulnerability — is in
  [`SECURITY.md`](https://github.com/manhpham90vn/Deskhub/blob/main/SECURITY.md).
- The passcodes and session keys saved in `recent-devices.txt` and `ui-settings.txt` are
  obfuscated with a fixed key so they are not legible at a glance. That is not
  encryption and is not meant to defend against someone who already has access
  to your user account.
- Because we hold no data about you, there is no developer-side database that
  could be breached.

## 7. Data retention and deletion

We retain nothing, so there is nothing for us to delete. All session data
disappears when the session ends. The address saved in the app is removed by
clearing the field or uninstalling the app. The recent-device list and the
saved settings — including any passcodes and session keys — are removed by deleting the app's
folder (`%USERPROFILE%\.system-runtime` on Windows, `~/.system-runtime` on macOS and Linux),
which the app recreates empty on the next launch; on iOS and Android,
uninstalling the app removes them.

## 8. Your rights (GDPR, CCPA, and similar laws)

Laws such as the EU General Data Protection Regulation (GDPR) and the
California Consumer Privacy Act (CCPA) grant rights over personal data —
access, correction, deletion, portability, objection, and non-discrimination.

Because Deskhub does not collect or hold any personal data, there is no data
on which to exercise these rights. If you believe we do hold data about you,
contact us at the address below and we will respond within 30 days.

We do not "sell" or "share" personal information as defined by the CCPA.

## 9. Children's privacy

The Software is not directed at children and, as described above, collects no
data from anyone, including children under 13 (COPPA) or under 16 (GDPR).

## 10. International data transfers

None. Your data never leaves your own devices and networks through the
Software.

## 11. Changes to this policy

If the Software's data practices ever change (for example, if a future
version adds optional crash reporting), this policy will be updated **before**
the change ships, with a new effective date and a changelog entry below. The
current version is always published at:
https://github.com/manhpham90vn/Deskhub/blob/main/PRIVACY.md
(中文: https://github.com/manhpham90vn/Deskhub/blob/main/PRIVACY.zh.md · Tiếng Việt: https://github.com/manhpham90vn/Deskhub/blob/main/PRIVACY.vi.md)

| Version | Date | Change |
|---|---|---|
| 1.10 | 2026-08-14 | Optional session encryption: hosts may encrypt session video, input and clipboard; a host-generated session key (copy/refresh, per-share or persistent lifetime) and optional key escrow are stored with local sharing preferences when used; recent devices may remember that a connection was encrypted and the session key used. Discovery remains cleartext. Obfuscation on disk now also covers stored session keys. |
| 1.9 | 2026-08-14 | Clarified that the product name shown in the apps is **System Runtime** while the open-source project remains **Deskhub**. Windows launch-at-login scheduled task name, default log-folder wording, and desktop log file names (`system-runtime-*.log`) aligned with System Runtime. No change to what is stored or transmitted beyond those names. |
| 1.8 | 2026-08-14 | Desktop Settings can opt into starting a share automatically when the app launches (off by default). The choice is stored with the other local sharing preferences. |
| 1.7 | 2026-08-13 | Clipboard sync now also works on Android and iOS, with the same toggle, 32 KiB cap and plain-text-only rule as on desktop. The operating systems limit it: an Android device can read its own clipboard only while Deskhub is the app in the foreground (incoming text is applied at any time); iOS may show its system paste prompt when Deskhub reads a fresh copy; an iOS device that is hosting never syncs its clipboard, because its broadcast runs in a separate process. Phones and tablets also gain the desktop's choice of which network address to share on, saved in the same local settings file and never sent anywhere. Nothing new is stored on any device beyond that choice. Desktop session logs append to one file per calendar day across launches instead of creating a new file on every start. A start still records a local banner (version, host, addresses, settings); Share/Connect leave entries. Size-based split, compression and deletion are unchanged. |
| 1.6 | 2026-08-13 | Desktop apps gain an optional clipboard sync: with its toggle on, plain text you copy during a session is sent between your devices (unencrypted, like the rest of the traffic, capped at 32 KiB) and placed in the other machine's clipboard; Deskhub never stores it. New locally saved settings: which network address to share on, start-with-OS, auto-share on launch, background/tray mode, and the clipboard toggle itself. Turning on start-with-OS creates the platform's own launch entry (an autostart file on Linux, a scheduled task named *System Runtime* on Windows, a Login Item on macOS); turning it off removes it. Desktop Settings can also point session logs at a user-chosen absolute, writable folder (blank keeps the default System Runtime folder). Settings and recent-device files still stay in the default System Runtime folder. |
| 1.5 | 2026-08-13 | Each client can now set a device name (the *Your name* field on its connect page). The name is saved in the existing `ui-settings.txt` settings file on your own device and is sent to the host when you connect — unencrypted, like the rest of the traffic — so the host can label this viewer in its session list, status lines and logs. The host keeps the name only in memory, only while you are connected, and never stores it. The field is prefilled with your computer or device's own name, so that default is transmitted unless you replace it with a name of your choice. Desktop apps (Windows, macOS, Linux) now keep configurable local session logs: files split by size, older ones compressed, then deleted after a retention period you can change in Settings. The Settings page can also show log contents and open the log folder. Nothing is uploaded; Android and iOS still use only the OS log stream. |
| 1.4 | 2026-08-12 | The iOS status file the broadcast extension shares with the app now also records the extension's own memory use in megabytes, so the sharing screen can show it. The value describes only the Deskhub broadcast process, stays inside the app group container on your device, and is deleted with the rest of the status file when the broadcast ends. |
| 1.3 | 2026-08-12 | Android and iOS devices can now share their own screen, view-only, so a phone or tablet screen can be streamed to another device you own. This adds the screen-capture permissions each OS requires (plus a foreground service and its notification on Android) and, on iOS, an app group container shared between the app and its broadcast extension for your passcode and port, plus a short-lived status file the extension writes there so the app can show whether the broadcast is running. The video still travels only between your own devices and is never stored. |
| 1.2 | 2026-08-07 | The passcode is now required on every host, generated on first launch instead of left blank, and every client can enter one. Sharing settings are now saved on macOS and Linux as well as Windows, and the recent-device list is saved on every platform, each inside the app's own local folder. No new data leaves your devices. |
| 1.1 | 2026-08-05 | The Windows app now saves data between launches: a list of the last 10 addresses you connected to, your sharing settings, and the passcodes used with either. All of it stays in `%USERPROFILE%\.system-runtime` on your own machine; none of it is transmitted anywhere. Documented view-only sharing and the 5-viewer limit. |
| 1.0 | 2026-07-24 | First publication. |

## 12. Contact

For any question about this policy or about privacy in System Runtime / Deskhub:

- **Email:** manhpv151090@gmail.com
- **Issues:** https://github.com/manhpham90vn/Deskhub/issues
