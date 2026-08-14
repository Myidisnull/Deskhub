**English** · [中文](SECURITY.zh.md) · [Tiếng Việt](SECURITY.vi.md)

# Deskhub Security Policy

_Last updated: August 14, 2026_

The open-source project is **Deskhub**; the product name shown in the apps is **System Runtime**. This policy covers both.

## ⚠️ Read this first

**Session encryption is optional and off by default.** Every host still requires a
4-digit passcode. With encryption off, traffic travels in the clear: anyone who can
capture a packet reads the passcode and can take mouse and keyboard control of the
sharing machine. With encryption on, session video, input and clipboard are encrypted;
discovery probes stay cleartext. The host generates a session key you copy to viewers
unless *Escrow key to viewers* is on (also off by default), in which case the host
hands the key to anyone who presents the passcode.

Deskhub is still built for a network you already trust, or a VPN underneath it. It is
**not** built to survive exposure to the open Internet.

So there is exactly one hard rule:

> **Never port-forward UDP 47777. Never expose a sharing machine to the Internet
> directly. For remote access, use a VPN — [Tailscale](https://tailscale.com) is what
> this project is tested against — and connect to the `100.x.y.z` address.**

If you follow that rule, System Runtime is safe to use. If you break it, you are handing
your machine to the Internet. Turn on session encryption when the LAN is not fully
trusted; leave escrow off when you can hand-carry the session key. See
[`docs/SPECIFICATION.md`](docs/SPECIFICATION.md) §9 for the product rules.

## Threat model

### What Deskhub protects against

| | |
|---|---|
| Data reaching the developer | Nothing does. There are no servers, no accounts, no telemetry, no third-party SDKs. See [`PRIVACY.md`](PRIVACY.md). |
| A remote viewer fighting you for the machine | "Host wins": the moment you touch the real mouse or keyboard, remote input is paused (Windows, macOS and Linux hosts alike). |
| Keys left stuck down | Any key the remote side is holding is released automatically when the session ends or the viewer switches away. |
| A stranger who cannot sniff your traffic | Every host requires a passcode. Wrong passcode or session-key attempts from one source are limited (**5** failures in **60 seconds**, then **30 seconds** locked). Discovery probes are rate-limited so the display-list reply is not an unlimited oracle. The 4-digit space is still small: a determined scanner on the LAN remains a real risk if encryption is off or escrow is on. |
| Optional encrypted sessions | With *Encrypt session traffic* on, video, input and clipboard for the session are AEAD-encrypted. Viewers need the session key unless escrow is on. An encrypting host refuses plaintext admission. Discovery stays cleartext. |
| Viewers fighting each other for the mouse | Up to 5 viewers may watch one host, but only one drives input: the earliest to have joined wins, and a later viewer's input is dropped until the earlier one has been idle for a second. A 6th viewer is rejected as `Busy`. |
| A viewer you only want to show the screen to | View-only sharing, available on every host, drops input packets at the host before anything is injected — it is not enforced by asking the client to behave. Android and iOS hosts are view-only unconditionally. |
| A phone left sharing by accident | The operating system, not Deskhub, is the backstop: Android keeps a permanent notification up and re-asks for recording consent on every single share, and iOS keeps its broadcast indicator visible. Either can stop the share without opening the app. |
| Malformed packets | Every field is bounds-checked before it is read. The parsers are covered by unit tests, run under AddressSanitizer, UndefinedBehaviorSanitizer and ThreadSanitizer in CI, and fuzzed nightly with libFuzzer — six targets covering the wire format, H.264 parsing, packet reassembly, session state machines and UI text. Crashes found by fuzzing are kept in-repo as regression tests, and new coverage is folded back into the seed corpus. |

### What Deskhub does **not** protect against

This is the honest list. The passcode alone does not solve any of it; optional session
encryption narrows some of it but not all:

- **No strong machine identity.** The passcode is 4 digits compared for equality. With
  encryption off it travels in the clear inside `Hello`. With encryption on and escrow
  off, the session key is the real secret — but there is still no pairing step, no host
  approval prompt, and no address allowlist.
- **Encryption is optional.** Default traffic is plaintext UDP. Anyone who can capture
  an unencrypted session can watch the screen and read keystrokes. Turning encryption on
  covers session payloads only; discovery probes stay cleartext.
- **Escrow weakens hand-off.** With escrow on, anyone who can present the passcode
  receives the session key from the host. Prefer hand-carrying the key when the LAN is
  hostile.
- **The device name travels in the clear** on discovery and admission paths that are not
  covered by session encryption. The *Your name* a viewer sends is shown on the host and
  written into the host's logs. It defaults to the machine's own hostname or device name —
  which on a personal machine often contains its owner's real name. Treat it as public to
  the network.
- **No DoS resistance.** Flooding the port will disrupt a session; rate limits only
  blunt guessing, not floods.
- **The discovery beacon still answers.** A `LIST_SOURCES` probe needs no session and
  gets a reply from any source address; so does a `PING`. The passcode only empties the
  reply. Rate limiting reduces oracle abuse; it does not hide that a Deskhub host is
  present.
- **A viewer slot frees itself after 5 seconds of silence.** If your viewer drops off,
  its slot reopens and the next admitted `Hello` takes it.
- **Sharing exposes the entire display.** Not one window: every notification, popup and
  window on that monitor. See [`PRIVACY.md` §3.4](PRIVACY.md).
- **A phone or tablet host exposes the whole phone.** Android and iOS can host too, and
  what they stream is the entire screen — banking apps, one-time codes, messages, every
  password you type while sharing. Mobile hosts are always view-only, which removes the
  remote-control risk but none of the exposure risk.

## Where it is safe to run

✅ **Safe**

- A home or personal LAN where you control every device on it.
- A Tailscale tailnet (or another WireGuard/VPN tunnel) that only your own devices have
  joined. The VPN still supplies network-level identity even when Deskhub session
  encryption is on.
- A machine that is only ever a *client* (phone, tablet, laptop that never shares its
  screen). Clients accept no inbound sessions.

❌ **Unsafe — do not do this**

- Port-forwarding UDP 47777 through your router, or putting a sharing machine in a DMZ.
- Sharing your screen on café, hotel, airport, campus, coworking or conference Wi-Fi.
- Sharing on an office or shared-house LAN where you do not trust every other device.
- Any network with guest devices, IoT devices you did not configure, or roommates'
  machines you do not administer.
- Exposing the port through a cloud VM's public interface or a public tunnel service.

By default the socket binds to all interfaces (`INADDR_ANY`), so it is reachable on
every network the machine is attached to — including one you forgot it was joined to.
The **Share on network** setting narrows this: pick one of the machine's addresses and
the host binds only that interface, so machines on the other networks cannot even
reach the port. Two caveats: if the chosen address no longer exists when you start
sharing (cable unplugged, DHCP gave you a new address), Deskhub falls back to all
interfaces and says so in the sharing status — check the banner if you rely on this;
and binding one interface also stops loopback (`127.0.0.1`) viewers on the same
machine. On Windows the app runs elevated from the moment it starts (it asks once, so
that it can inject input into elevated windows) and opens the firewall rule for you
when you share — the rule covers the whole app on every profile, so a narrowed bind
does not narrow the firewall; that convenience is also what makes the rule above
matter.

## What an attacker on the same network can do

If someone is on the same LAN as a machine that is sharing its screen, and Deskhub is
running, they can:

1. Discover it by scanning for UDP 47777. The passcode blanks the list of displays and
   resolutions they get back, but the machine still answers the probe, so it still gives
   itself away.
2. With **encryption off**: read your passcode off the wire and connect with mouse and
   keyboard. From there: open a browser, read your email, install software, exfiltrate
   files. Passively record the session and reconstruct screen and keystrokes offline.
   Clipboard sync, if enabled, rides the same clear channel.
3. With **encryption on and escrow off**: they still see discovery traffic, but session
   payloads need the session key. Guessing the key is not practical; stealing a copied
   key or shoulder-surfing remains possible.
4. With **encryption on and escrow on**: presenting the passcode is enough to obtain the
   session key from the host — treat escrow like “passcode alone admits encrypted
   sessions.”

The "host wins" behaviour limits mischief while you are *sitting at* the machine. It
does nothing while you are away from it, which is when it matters.

## Hardening checklist

If you want to keep using Deskhub as it is today, these are worth doing:

- [ ] Run Tailscale on both machines and connect only over the `100.x.y.z` address.
- [ ] Confirm your router has **no** port-forward or UPnP mapping for UDP 47777.
- [ ] Change the generated passcode in Settings to something a viewer will not guess from
      your habits, and untick *Viewers can control this machine* whenever you only need
      someone to watch.
- [ ] On networks you do not fully trust, turn on *Encrypt session traffic*, leave
      *Escrow key to viewers* off, and copy the session key out of band. Prefer *Per share*
      key lifetime for one-off shares.
- [ ] Quit Deskhub when you are not actively using it. It does not run as a background
      service — closing it closes the hole.
- [ ] On Linux, if you use `ufw`, scope the rule instead of opening it wide:
      `sudo ufw allow from 100.64.0.0/10 to any port 47777 proto udp` rather than
      `sudo ufw allow 47777/udp`.
- [ ] Do not leave a share running on a laptop that you carry onto other networks.
- [ ] Lock your machine when you walk away, so an unattended session cannot be taken
      over silently.

## Local artifacts

Diagnostic logs are written in plain text under `~/.system-runtime/` (`%USERPROFILE%\.system-runtime`
on Windows) on Windows, macOS and Linux. They contain connection statistics and peer
addresses, not screen content or keystrokes.

The desktop apps keep two more files in that folder: `ui-settings.txt` (fps, bitrate,
resolution cap, port, the view-only switch, your host passcode, optional session-encryption
toggles and session key material when encryption is used, and the optional device name
shown to hosts — stored as the plain text you typed) and
`recent-devices.txt` (the last 10 addresses you connected to, when, the passcode you used
for each, and when applicable whether the connection was encrypted and the session key
used). The mobile apps keep the same two files inside their own sandbox — on iOS in the
app group container, so the broadcast extension reads the same host passcode the app shows
you. Stored passcodes and session keys are obfuscated with a fixed XOR key, which keeps
them off the screen and out of a casual `type` of the file — **it is not encryption**, and
anyone with the source and the file recovers them in seconds. Treat that folder as readable
by anything running as you.

Nothing uploads any of this; delete the folder at any time.

## Planned mitigations

Tracked, in the order they are intended to land:

1. **A connection prompt on the host** — the sharing machine asks before the first
   session is accepted, instead of accepting silently.
2. **Storing passcodes and session keys in the OS keychain** instead of an obfuscated
   text file.
3. **Silencing the discovery beacon** so it does not reply at all to an unsolicited
   probe, rather than replying with an empty list.

Shipped / specified since earlier revisions of this list: the mandatory passcode and
view-only switch on every host; optional session encryption with a host-generated session
key, copy/refresh, per-share or persistent lifetime, and optional key escrow; refusal to
downgrade an encrypting host to plaintext; connection and discovery rate limits as in
[`docs/SPECIFICATION.md`](docs/SPECIFICATION.md) §9. Implementation is being aligned to
that specification where a build still differs.

This list is a statement of intent, not a schedule. Deskhub is maintained by one person
in their spare time. Treat the current state as the state, not the plan.

## Reporting a vulnerability

Please report security issues **privately** — not as a public GitHub issue.

- **Email:** manhpv151090@gmail.com — put `[Deskhub security]` in the subject.
- **Or:** open a [private security advisory](https://github.com/manhpham90vn/Deskhub/security/advisories/new)
  on GitHub.

Please include what you were running (OS, Deskhub version from the title bar or
[`VERSION`](VERSION)), what you did, and what happened. A proof of concept helps a lot.

**What to expect:** an acknowledgement within 7 days and an assessment within 30. This
is a spare-time project run by one developer, so please be patient with the timeline —
you will get a straight answer either way. If a fix ships, you will be credited in the
release notes unless you would rather not be.

There is no bug bounty; nothing is paid out.

**Already documented above is not a vulnerability.** Missing host approval prompts,
cleartext discovery, optional-by-default encryption, and the limits of a 4-digit passcode
are known and listed — a report that Deskhub can be connected to with only a short code
on a trusted LAN tells us nothing new. What *is* worth reporting: memory corruption or
crashes reachable from a malformed packet, a way to escape the documented threat model,
anything that leaks data off the machine, bypass of session encryption when it is on, or
a flaw in a mitigation once one ships.

## Supported versions

Only the most recent release on the [Releases page](https://github.com/manhpham90vn/Deskhub/releases)
is supported. Fixes ship in a new release; there are no backports to older versions.

## Scope

This policy covers the Deskhub source in this repository and the binaries published on
the Releases page, TestFlight and Google Play. It does not cover Tailscale, your
operating system, your router, or any other software you run alongside it.
