# Deskhub Security Policy

_Last updated: August 5, 2026_

## ⚠️ Read this first

**Deskhub has no encryption of its own. A Windows host can require a 4-digit passcode,
but it is off by default and it travels in plaintext, so it stops a stranger who cannot
see your traffic and nothing more. Without one, anyone who can send UDP packets to port
47777 on a sharing machine gets full mouse and keyboard control of it.**

A Windows host can also share **view-only** (input is dropped instead of injected). Both
switches live in the Settings tab of the Windows app; the macOS and Linux hosts have
neither yet, and no client other than Windows can enter a passcode.

That is a deliberate design choice, not an oversight — Deskhub is built to run inside a
network you already trust, and to borrow its encryption and its identity checks from the
layer underneath it (your LAN, or a WireGuard tunnel such as Tailscale). It is **not**
built to survive exposure to the open Internet.

So there is exactly one rule:

> **Never port-forward UDP 47777. Never expose a sharing machine to the Internet
> directly. For remote access, use a VPN — [Tailscale](https://tailscale.com) is what
> this project is tested against — and connect to the `100.x.y.z` address.**

If you follow that rule, Deskhub is safe to use. If you break it, you are handing your
machine to the Internet.

## Threat model

### What Deskhub protects against

| | |
|---|---|
| Data reaching the developer | Nothing does. There are no servers, no accounts, no telemetry, no third-party SDKs. See [`PRIVACY.md`](PRIVACY.md). |
| A remote viewer fighting you for the machine | "Host wins": the moment you touch the real mouse or keyboard, remote input is paused (Windows, macOS and Linux hosts alike). |
| Keys left stuck down | Any key the remote side is holding is released automatically when the session ends or the viewer switches away. |
| A stranger who cannot sniff your traffic | Only if you set a passcode on a Windows host: a wrong code is rejected, and every third wrong code locks the host for 30 seconds. That caps guessing at 3 tries per half-minute, so walking all 10 000 combinations would take about a day of uninterrupted attempts. A discovery probe without the right code gets an empty list back instead of your display names. |
| Viewers fighting each other for the mouse | Up to 5 viewers may watch one host, but only one drives input: the earliest to have joined wins, and a later viewer's input is dropped until the earlier one has been idle for a second. A 6th viewer is rejected as `Busy`. |
| A viewer you only want to show the screen to | View-only sharing (Windows host) drops input packets at the host, before anything is injected — it is not enforced by asking the client to behave. |
| Malformed packets | Every field is bounds-checked before it is read. The parsers are covered by unit tests and run under AddressSanitizer, UndefinedBehaviorSanitizer and ThreadSanitizer in CI. |

### What Deskhub does **not** protect against

This is the honest list. Nothing below is solved today — and the passcode, where it
exists, does not solve any of it:

- **No real authentication.** The optional passcode is 4 digits sent in the clear inside
  the `Hello` packet and compared for equality — it is a lock on a door, not a
  cryptographic identity check. Anyone who can capture one packet has it forever and can
  replay it. It is blank by default, exists only in the Windows app, and there is still
  no pairing step, no approval prompt, and no address allowlist. On a host with no
  passcode set, the first `Hello` to arrive is accepted.
- **No encryption.** There is no TLS, no DTLS, no Noise, no application-layer crypto of
  any kind in this codebase. Video frames, keystrokes and mouse movement all travel as
  plaintext UDP. Anyone who can capture your traffic can watch your screen and read
  everything you type.
- **No integrity protection.** Packets are not signed or authenticated, so an attacker
  who can inject traffic can forge input events.
- **No protection against session hijacking on a shared network.** A live session is
  identified only by a 32-bit session id. It is generated from the OS CSPRNG
  (`BCryptGenRandom` / `arc4random_buf` / `getrandom`), so guessing it blindly is not
  practical — but on a network where an attacker can *sniff* your packets, that id is
  sitting in the clear in every one of them. With it, an attacker can inject input and
  redirect the video stream to their own address.
- **No rate limiting or DoS resistance.** Flooding the port will disrupt a session.
- **The discovery beacon answers anyone.** A `LIST_SOURCES` probe needs no session and
  gets a reply from any source address; so does a `PING`. Setting a passcode only empties
  the reply — a probe without the right code is told "nothing shared" instead of your
  display names and resolutions. Either way the packet still comes back, so the machine
  is still discoverable by scanning, and the port is still usable as a small UDP
  reflector.
- **A viewer slot frees itself after 5 seconds of silence.** If your viewer drops off,
  its slot reopens and the next `Hello` to arrive takes it — whoever sent it, subject
  only to the passcode if you set one.
- **Sharing exposes the entire display.** Not one window: every notification, popup and
  window on that monitor. See [`PRIVACY.md` §3.3](PRIVACY.md).

## Where it is safe to run

✅ **Safe**

- A home or personal LAN where you control every device on it.
- A Tailscale tailnet (or another WireGuard/VPN tunnel) that only your own devices have
  joined. The VPN provides the encryption and the identity check that Deskhub does not.
- A machine that is only ever a *client* (phone, tablet, laptop that never shares its
  screen). Clients accept no inbound sessions.

❌ **Unsafe — do not do this**

- Port-forwarding UDP 47777 through your router, or putting a sharing machine in a DMZ.
- Sharing your screen on café, hotel, airport, campus, coworking or conference Wi-Fi.
- Sharing on an office or shared-house LAN where you do not trust every other device.
- Any network with guest devices, IoT devices you did not configure, or roommates'
  machines you do not administer.
- Exposing the port through a cloud VM's public interface or a public tunnel service.

The socket binds to all interfaces (`INADDR_ANY`), so it is reachable on every network
the machine is attached to — including one you forgot it was joined to. On Windows the
app runs elevated from the moment it starts (it asks once, so that it can inject input
into elevated windows) and opens the firewall rule for you when you share; that
convenience is also what makes the rule above matter.

## What an attacker on the same network can do

If someone is on the same LAN as a machine that is sharing its screen, and Deskhub is
running, they can:

1. Discover it by scanning for UDP 47777, and read back the list of displays being
   shared, with their resolutions. A passcode blanks that list but the machine still
   answers the probe, so it still gives itself away.
2. Connect to it and immediately have mouse and keyboard. From there: open a browser,
   read your email, install software, exfiltrate files. A passcode blocks this only for
   as long as they have not seen one of your packets — the code is in the clear in every
   `Hello`, so anyone who can sniff the network simply reads it and connects.
3. Whether or not they can connect, passively record the session and reconstruct your
   screen and your keystrokes offline.

The "host wins" behaviour limits mischief while you are *sitting at* the machine. It
does nothing while you are away from it, which is when it matters.

## Hardening checklist

If you want to keep using Deskhub as it is today, these are worth doing:

- [ ] Run Tailscale on both machines and connect only over the `100.x.y.z` address.
- [ ] Confirm your router has **no** port-forward or UPnP mapping for UDP 47777.
- [ ] On a Windows host, set a passcode in Settings, and untick *Viewers can control this
      machine* whenever you only need someone to watch. Neither replaces the VPN — they
      raise the floor for a stranger who cannot read your traffic.
- [ ] Quit Deskhub when you are not actively using it. It does not run as a background
      service — closing it closes the hole.
- [ ] On Linux, if you use `ufw`, scope the rule instead of opening it wide:
      `sudo ufw allow from 100.64.0.0/10 to any port 47777 proto udp` rather than
      `sudo ufw allow 47777/udp`.
- [ ] Do not leave a share running on a laptop that you carry onto other networks.
- [ ] Lock your machine when you walk away, so an unattended session cannot be taken
      over silently.

## Local artifacts

Diagnostic logs are written in plain text under `~/.deskhub/` (`%USERPROFILE%\.deskhub`
on Windows) on Windows, macOS and Ubuntu. They contain connection statistics and peer
addresses, not screen content or keystrokes.

The Windows app keeps two more files in that folder: `ui-settings.txt` (fps, bitrate,
resolution cap, port, the view-only switch, and your host passcode) and
`recent-devices.txt` (the last 10 addresses you connected to, when, and the passcode you
used for each). Stored passcodes are obfuscated with a fixed XOR key, which keeps them
off the screen and out of a casual `type` of the file — **it is not encryption**, and
anyone with the source and the file recovers them in seconds. Treat that folder as
readable by anything running as you.

Nothing uploads any of this; delete the folder at any time.

## Planned mitigations

Tracked, in the order they are intended to land:

1. **The passcode on every platform** — it exists only in the Windows app today, so a
   macOS or Linux host cannot ask for one and a phone cannot answer one.
2. **A connection prompt on the host** — the sharing machine asks before the first
   session is accepted, instead of accepting silently.
3. **Real encryption and authentication** — an authenticated key exchange (Noise IK or
   DTLS) so that Deskhub no longer depends on the network being trustworthy, and so the
   passcode stops being readable off the wire. Until this ships, the rule at the top of
   this document is the entire security model.
4. **Storing the passcode in the OS keychain** instead of an obfuscated text file.
5. **Silencing the discovery beacon** so it does not reply at all to an unsolicited
   probe, rather than replying with an empty list.

Shipped since the last revision of this list: the 4-digit host passcode with a lockout,
view-only sharing, and a discovery beacon that hides the display list from probes without
the right code — all Windows-only, and none of them a substitute for item 3.

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

**Already documented above is not a vulnerability.** The missing authentication and
encryption are known, listed, and being worked on — a report that Deskhub can be
connected to without a password tells us nothing new. What *is* worth reporting: memory
corruption or crashes reachable from a malformed packet, a way to escape the documented
threat model, anything that leaks data off the machine, or a flaw in a mitigation once
one ships.

## Supported versions

Only the most recent release on the [Releases page](https://github.com/manhpham90vn/Deskhub/releases)
is supported. Fixes ship in a new release; there are no backports to older versions.

## Scope

This policy covers the Deskhub source in this repository and the binaries published on
the Releases page, TestFlight and Google Play. It does not cover Tailscale, your
operating system, your router, or any other software you run alongside it.
