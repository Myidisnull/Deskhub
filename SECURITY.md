**English** · [Tiếng Việt](SECURITY.vi.md)

# Deskhub Security Policy

_Last updated: August 13, 2026_

## ⚠️ Read this first

**Deskhub has no encryption of its own. Every host requires a 4-digit passcode, but it
travels in plaintext, so it stops a stranger who cannot see your traffic and nothing
more. Anyone who can capture one packet reads the code, and from there gets full mouse
and keyboard control of the sharing machine.**

Every host can also share **view-only** (input is dropped instead of injected). Both
controls live in the Settings section of the Windows, macOS and Linux apps, and every
client — Windows, macOS, Linux, Android, iOS — can enter a passcode.

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
| A stranger who cannot sniff your traffic | Every host requires a passcode, and every third wrong connection attempt locks the host for 30 seconds. That throttle covers connections only: the discovery beacon answers probes at full speed, and a probe carrying the right code gets the real display list where a wrong one gets an empty list — an oracle that confirms a guessed code, so all 10 000 combinations can be walked in seconds. The passcode keeps out the casual, not anyone running a scanner (see the beacon bullet below). |
| Viewers fighting each other for the mouse | Up to 5 viewers may watch one host, but only one drives input: the earliest to have joined wins, and a later viewer's input is dropped until the earlier one has been idle for a second. A 6th viewer is rejected as `Busy`. |
| A viewer you only want to show the screen to | View-only sharing, available on every host, drops input packets at the host before anything is injected — it is not enforced by asking the client to behave. Android and iOS hosts are view-only unconditionally. |
| A phone left sharing by accident | The operating system, not Deskhub, is the backstop: Android keeps a permanent notification up and re-asks for recording consent on every single share, and iOS keeps its broadcast indicator visible. Either can stop the share without opening the app. |
| Malformed packets | Every field is bounds-checked before it is read. The parsers are covered by unit tests, run under AddressSanitizer, UndefinedBehaviorSanitizer and ThreadSanitizer in CI, and fuzzed nightly with libFuzzer — six targets covering the wire format, H.264 parsing, packet reassembly, session state machines and UI text. Crashes found by fuzzing are kept in-repo as regression tests, and new coverage is folded back into the seed corpus. |

### What Deskhub does **not** protect against

This is the honest list. Nothing below is solved today — and the passcode does not solve
any of it:

- **No real authentication.** The passcode is 4 digits sent in the clear inside the
  `Hello` packet and compared for equality — it is a lock on a door, not a cryptographic
  identity check. Anyone who can capture one packet has it forever and can replay it.
  Making it mandatory removed the "no code at all" case, but there is still no pairing
  step, no approval prompt, and no address allowlist: whoever sends the right four digits
  first is in.
- **No encryption.** There is no TLS, no DTLS, no Noise, no application-layer crypto of
  any kind in this codebase. Video frames, keystrokes and mouse movement all travel as
  plaintext UDP. Anyone who can capture your traffic can watch your screen and read
  everything you type.
- **The device name travels in the clear too.** The *Your name* a viewer sends
  rides in every `Hello` alongside the passcode, as plaintext, and is shown on the host's
  screen and written into the host's logs. It defaults to the machine's own hostname or
  device name — which on a personal machine often contains its owner's real name. Treat
  it as public to the network: replace the default with a nickname that gives nothing
  away — something you are happy for anyone on the LAN, and anyone looking at the host,
  to read — and never put a password or anything sensitive in it. Clearing the field
  does not stop a name being sent; it only restores the default.
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
  gets a reply from any source address; so does a `PING`. The passcode only empties the
  reply — a probe without the right code is told "nothing shared" instead of your display
  names and resolutions. Either way the packet still comes back, so the machine is still
  discoverable by scanning, and the port is still usable as a small UDP reflector. The
  beacon is also not rate limited, and a non-empty reply confirms a correct code, so it
  doubles as an oracle for brute-forcing the passcode — the 30-second connection lockout
  does not apply here.
- **A viewer slot frees itself after 5 seconds of silence.** If your viewer drops off,
  its slot reopens and the next `Hello` to arrive takes it — whoever sent it, subject
  only to the passcode.
- **Sharing exposes the entire display.** Not one window: every notification, popup and
  window on that monitor. See [`PRIVACY.md` §3.4](PRIVACY.md).
- **A phone or tablet host exposes the whole phone.** Android and iOS can host too, and
  what they stream is the entire screen — banking apps, one-time codes, messages, every
  password you type while sharing. The same plaintext UDP carries it, so anyone who can
  sniff the network sees all of it. Mobile hosts are always view-only, which removes the
  remote-control risk but none of the exposure risk.

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
2. Read your passcode off the wire and connect, and immediately have mouse and keyboard.
   From there: open a browser, read your email, install software, exfiltrate files. The
   passcode blocks this only for as long as they have not seen one of your packets — the
   code is in the clear in every `Hello`, so anyone who can sniff the network simply
   reads it and connects.
3. Whether or not they can connect, passively record the session and reconstruct your
   screen and your keystrokes offline.
4. If clipboard sync is enabled, read every piece of text you copy while sharing —
   clipboard text rides the same unencrypted UDP channel as everything else. Passwords
   copied from a password manager are the classic casualty; leave the toggle off on
   networks you do not fully trust.

The "host wins" behaviour limits mischief while you are *sitting at* the machine. It
does nothing while you are away from it, which is when it matters.

## Hardening checklist

If you want to keep using Deskhub as it is today, these are worth doing:

- [ ] Run Tailscale on both machines and connect only over the `100.x.y.z` address.
- [ ] Confirm your router has **no** port-forward or UPnP mapping for UDP 47777.
- [ ] Change the generated passcode in Settings to something a viewer will not guess from
      your habits, and untick *Viewers can control this machine* whenever you only need
      someone to watch. Neither replaces the VPN — they raise the floor for a stranger
      who cannot read your traffic.
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
on Windows) on Windows, macOS and Linux. They contain connection statistics and peer
addresses, not screen content or keystrokes.

The desktop apps keep two more files in that folder: `ui-settings.txt` (fps, bitrate,
resolution cap, port, the view-only switch, your host passcode, and the optional device
name shown to hosts — stored as the plain text you typed) and
`recent-devices.txt` (the last 10 addresses you connected to, when, and the passcode you
used for each). The mobile apps keep the same two files inside their own sandbox — on iOS
in the app group container, so the broadcast extension reads the same host passcode the
app shows you. Stored
passcodes are obfuscated with a fixed XOR key, which keeps them off the screen and out of
a casual `type` of the file — **it is not encryption**, and anyone with the source and
the file recovers them in seconds. Treat that folder as readable by anything running as
you.

Nothing uploads any of this; delete the folder at any time.

## Planned mitigations

Tracked, in the order they are intended to land:

1. **A connection prompt on the host** — the sharing machine asks before the first
   session is accepted, instead of accepting silently.
2. **Real encryption and authentication** — an authenticated key exchange (Noise IK or
   DTLS) so that Deskhub no longer depends on the network being trustworthy, and so the
   passcode stops being readable off the wire. Until this ships, the rule at the top of
   this document is the entire security model.
3. **Storing the passcode in the OS keychain** instead of an obfuscated text file.
4. **Silencing the discovery beacon** so it does not reply at all to an unsolicited
   probe, rather than replying with an empty list.

Shipped since the last revision of this list: the passcode and the view-only switch on
every host, a passcode field in every client, a host passcode that is generated on first
launch instead of left blank, and a discovery beacon that hides the display list from
probes without the right code. None of them is a substitute for item 2.

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
