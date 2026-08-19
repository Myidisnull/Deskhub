**English** · [Tiếng Việt](PRIVACY.vi.md)

# Deskhub Privacy Policy

_Effective date: August 19, 2026 — Version 2.1_

> A Vietnamese translation is available at [`PRIVACY.vi.md`](PRIVACY.vi.md). This English
> version is the authoritative one.

## 1. Introduction

This Privacy Policy describes how **Deskhub** ("the app", "we") handles
information when you use the Deskhub mobile applications (iOS, Android) and the
Deskhub desktop applications for Windows, macOS and Linux (together, "the
Software").

Deskhub is a remote desktop application: it streams the screen of one of your
computers to another device of yours and lets you control that computer with
mouse, keyboard and touch input.

The Software is developed and published by an individual developer:

- **Developer:** Manh Pham
- **Contact:** manhpv151090@gmail.com
- **Project page:** https://github.com/manhpham90vn/Deskhub

## 2. The short version

**Deskhub does not collect, store, sell, or share any personal data. We do not
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
| Screen content of the shared computer (video frames) | Displaying that screen on your other device | Sent directly between your two devices, encrypted in transit (QUIC/TLS) | Never stored; exists only in memory during the session |
| Sound the shared computer is playing (only while it shares sound and a viewer asks for it) | Letting the person watching hear that computer | Sent directly between your two devices, encrypted in transit (QUIC/TLS), as compressed audio | Never stored; exists only in memory during the session |
| Mouse, keyboard, and touch input | Controlling the shared computer from your other device | Sent directly from the viewing device to the shared computer, encrypted in transit (QUIC/TLS) | Never stored; discarded after injection |
| This machine's key pair — a private key and self-signed certificate created on first run | Proving this machine's identity to machines it connects to; people see it as a fingerprint (`SHA256:…`) | Written to `host_key.pem` and `host_cert.pem` in the app's own folder; only the public half (the certificate) is presented to machines you connect to | Kept until you delete the files; deleting them gives the machine a new identity, and machines that knew the old one will warn |
| The keys of hosts this device has trusted (fingerprint, address, label, first/last seen) | Recognising a known host and warning loudly if its key ever changes | Written to `known_hosts` in the same folder; never transmitted | Kept until you delete the file |
| The machines paired with this host — their key fingerprint, the name they sent, when they paired and were last seen | Letting paired machines reconnect without a passcode, and listing them on the Devices page so you can forget them | Written to `paired_devices` in the same folder; never transmitted | Kept until you forget the machine on the Devices page or delete the file |
| A random salt for the passcode verifier | Turning the passcode into the value the pairing handshake checks, so the code itself never travels | Written to `auth_salt` in the same folder; the salt is sent to a connecting machine during the handshake (it is not a secret) | Kept until you delete the file |
| The address (IP/hostname) you type | Connecting to the other machine | Stays on the device you typed it on | Kept locally until you change it |
| The last 10 addresses you connected to, the time of each, and the passcode you used for each | Filling in the *Recent devices* list so you can reconnect with one click | Written to `recent-devices.txt` in the app's own folder on your device — `%USERPROFILE%\.deskhub` on Windows, `~/.deskhub` on macOS and Linux, the app sandbox on iOS and Android | Kept until you connect to 10 newer addresses, or you delete the file |
| Your sharing preferences (frame rate, bitrate, resolution cap, port, which network address to share on, whether viewers may control the machine, the clipboard-sync, keep-awake, start-with-OS, auto-share and background-mode toggles, and the passcode you ask viewers for) | Restoring your settings the next time you open the app | Written to `ui-settings.txt` in the same folder. On iOS the file lives in the app group container shared by the app and its broadcast extension, so both halves agree on your passcode and port | Kept until you change them or delete the file |
| The screen-permission token the Linux desktop issues after you pick displays in its screen-sharing dialog (Linux only) | Letting later shares reuse your choice silently, so the dialog appears only the first time | Written to `portal-restore-token.txt` in the same folder; the token is meaningful only to your own desktop session on this machine and is never transmitted | Replaced after each share; removed when you press *Choose screens again* or delete the file |
| Clipboard text (only while the clipboard-sync toggle is on and a session is active) | Making text copied on one device pastable on the others | Sent directly between your devices, encrypted in transit (QUIC/TLS), capped at 32 KiB per copy; only plain text, never images or files | Never stored by Deskhub; lives only in each device's normal system clipboard |
| Whether a broadcast is currently running, how many viewers are connected, the broadcast extension's own memory use in megabytes, and the text of the last start-up error (iOS only) | Letting the app's sharing screen report the state of the broadcast extension, which iOS runs as a separate process and terminates if it uses too much memory | Written to `broadcast-status.txt` in the same app group container | Deleted when the broadcast ends |
| The device name in the *Your name* field — prefilled with this computer or device's own name (its hostname on Windows and Linux, its computer name on macOS, its device name on iOS, its model on Android) until you edit it | Shown on the host you connect to, next to this device's address, so the person sharing can tell viewers apart | Saved in `ui-settings.txt` in the same folder, and sent to the host when you connect — encrypted in transit, but displayed on the host's screen and written into its logs — so this default name is transmitted unless you replace it with a name of your choice; clearing the field only restores the default, which is then saved and sent. When the machines pair, the host also stores the name in its `paired_devices` list until you are forgotten there | Defaults to the computer or device's name; kept until you change it or delete the file. Clearing the field restores the default rather than removing the name |
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

Sharing sound needs no permission of its own on the desktops: it captures what the
computer itself is playing, not a microphone. Deskhub never records a microphone and asks
for no microphone permission on any platform.

The apps request no other permissions. If a future version needs a new
permission, it will be requested in-context and this policy will be updated.

## 5. Analytics, advertising, and third parties

- **Analytics / telemetry:** none.
- **Crash reporting:** none. Diagnostic logs (`[DIAG]`) exist only on your own
  machine — in the app's console output and, on Windows, macOS and Linux, in
  plain-text files under `~/.deskhub/` (`%USERPROFILE%\.deskhub` on Windows).
  They are never uploaded anywhere; they leave your device only if you copy and
  send them yourself, and you can delete that folder at any time.
- **Advertising:** none.
- **Third-party SDKs:** none. The Software is built only from its own source
  code (available at the project page) and operating-system frameworks.
- **App stores:** the apps are distributed through Apple App Store and Google
  Play. Apple and Google may collect installation/usage statistics under their
  own privacy policies; that collection is outside our control and we receive
  only the aggregated, anonymous statistics those platforms show to every
  developer.
- **Tailscale or other VPNs:** if you choose to connect through a VPN, your
  traffic is handled under that provider's privacy policy. Deskhub neither
  requires nor bundles any VPN.

## 6. Security

- Streaming traffic stays inside your own network or your own VPN tunnel.
  When you use a VPN such as Tailscale, traffic between devices is end-to-end
  encrypted by that VPN (WireGuard).
- Deskhub encrypts its session traffic — video, control, input, clipboard and
  terminal data all run over QUIC/TLS between your devices. Admission is
  decided by a pairing handshake: an unknown machine must prove it knows the
  host's optional 4-digit passcode (the code itself is never transmitted) or be
  approved by the person at the host. The device name is encrypted in transit
  but displayed on the host, so do not put anything sensitive in it. Never
  expose Deskhub to the Internet directly. The full threat model — what is
  protected, what is not, and how to report a vulnerability — is in
  [`SECURITY.md`](https://github.com/manhpham90vn/Deskhub/blob/main/SECURITY.md).
- The passcodes saved in `recent-devices.txt` and `ui-settings.txt` are
  obfuscated with a fixed key so they are not legible at a glance. That is not
  encryption and is not meant to defend against someone who already has access
  to your user account.
- Because we hold no data about you, there is no developer-side database that
  could be breached.

## 7. Data retention and deletion

We retain nothing, so there is nothing for us to delete. All session data
disappears when the session ends. The address saved in the app is removed by
clearing the field or uninstalling the app. The recent-device list and the
saved settings — including any passcodes — are removed by deleting the app's
folder (`%USERPROFILE%\.deskhub` on Windows, `~/.deskhub` on macOS and Linux),
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

| Version | Date | Change |
|---|---|---|
| 2.1 | 2026-08-19 | Sharing a screen can now share that computer's **sound** as well. What is captured is the mix the computer's own speakers are playing — never a microphone; Deskhub has no two-way audio and asks for no microphone permission. The audio is compressed, sent directly to the people watching over the same encrypted transport as the picture, and never stored. It travels only when the machine sharing has *Share this device's sound* on **and** a viewer has *Play the sound of the device you are watching* on; either switch turns it off. Both switches are saved with your other preferences in `ui-settings.txt` and are on by default. |
| 2.0 | 2026-08-15 | Sessions now run over an encrypted transport (QUIC/TLS) — video, input, clipboard and terminal traffic alike — and machines are admitted by pairing. New data stored on your own device, all in the app's folder and never sent to us: a key pair that is this machine's identity (`host_key.pem`, `host_cert.pem`), the keys of hosts you have trusted (`known_hosts`), the machines allowed into this host (`paired_devices` — key fingerprint, the name each sent, timestamps), and a non-secret salt (`auth_salt`). The passcode is now optional and is never transmitted: the pairing handshake proves it without sending it. |
| 1.9 | 2026-08-14 | On Linux, the screen choice made in the desktop's screen-sharing dialog is now remembered: the permission token the desktop issues is saved to `portal-restore-token.txt` so later shares skip the dialog. The token only works for your own desktop session on this machine, is never transmitted, is replaced after each share, and is removed when you press *Choose screens again* or delete the file. |
| 1.8 | 2026-08-14 | New *keep awake* toggle (on by default): while you are sharing or viewing, the app asks the operating system not to sleep the machine or turn off the display, and releases that request when the session ends. Only the on/off choice is stored, in the same local settings file; nothing about it is transmitted, and no system sleep settings are changed. |
| 1.7 | 2026-08-13 | Clipboard sync now also works on Android and iOS, with the same toggle, 32 KiB cap and plain-text-only rule as on desktop. The operating systems limit it: an Android device can read its own clipboard only while Deskhub is the app in the foreground (incoming text is applied at any time); iOS may show its system paste prompt when Deskhub reads a fresh copy; an iOS device that is hosting never syncs its clipboard, because its broadcast runs in a separate process. Phones and tablets also gain the desktop's choice of which network address to share on, saved in the same local settings file and never sent anywhere. Nothing new is stored on any device beyond that choice. |
| 1.6 | 2026-08-13 | Desktop apps gain an optional clipboard sync: with its toggle on, plain text you copy during a session is sent between your devices (unencrypted, like the rest of the traffic, capped at 32 KiB) and placed in the other machine's clipboard; Deskhub never stores it. New locally saved settings: which network address to share on, start-with-OS, auto-share on launch, background/tray mode, and the clipboard toggle itself. Turning on start-with-OS creates the platform's own launch entry (an autostart file on Linux, a scheduled task named *Deskhub* on Windows, a Login Item on macOS); turning it off removes it. |
| 1.5 | 2026-08-13 | Each client can now set a device name (the *Your name* field on its connect page). The name is saved in the existing `ui-settings.txt` settings file on your own device and is sent to the host when you connect — unencrypted, like the rest of the traffic — so the host can label this viewer in its session list, status lines and logs. The host keeps the name only in memory, only while you are connected, and never stores it. The field is prefilled with your computer or device's own name, so that default is transmitted unless you replace it with a name of your choice. |
| 1.4 | 2026-08-12 | The iOS status file the broadcast extension shares with the app now also records the extension's own memory use in megabytes, so the sharing screen can show it. The value describes only the Deskhub broadcast process, stays inside the app group container on your device, and is deleted with the rest of the status file when the broadcast ends. |
| 1.3 | 2026-08-12 | Android and iOS devices can now share their own screen, view-only, so a phone or tablet screen can be streamed to another device you own. This adds the screen-capture permissions each OS requires (plus a foreground service and its notification on Android) and, on iOS, an app group container shared between the app and its broadcast extension for your passcode and port, plus a short-lived status file the extension writes there so the app can show whether the broadcast is running. The video still travels only between your own devices and is never stored. |
| 1.2 | 2026-08-07 | The passcode is now required on every host, generated on first launch instead of left blank, and every client can enter one. Sharing settings are now saved on macOS and Linux as well as Windows, and the recent-device list is saved on every platform, each inside the app's own local folder. No new data leaves your devices. |
| 1.1 | 2026-08-05 | The Windows app now saves data between launches: a list of the last 10 addresses you connected to, your sharing settings, and the passcodes used with either. All of it stays in `%USERPROFILE%\.deskhub` on your own machine; none of it is transmitted anywhere. Documented view-only sharing and the 5-viewer limit. |
| 1.0 | 2026-07-24 | First publication. |

## 12. Contact

For any question about this policy or about privacy in Deskhub:

- **Email:** manhpv151090@gmail.com
- **Issues:** https://github.com/manhpham90vn/Deskhub/issues
