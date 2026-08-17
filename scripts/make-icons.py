#!/usr/bin/env python3
"""Regenerate every client icon from the single master artwork.

The master is assets/icon_1024.png. Everything else is derived from it:

  - macOS:   every icon_*.png in the appiconset (1024 is a byte-for-byte copy)
  - iOS:     AppIcon.png (byte-for-byte copy of the master)
  - Android: legacy mipmap-*/ic_launcher.png (rounded), adaptive
             mipmap-*/ic_launcher_foreground.png (full-bleed) and the
             values/ic_launcher_background.xml colour sampled from the artwork
  - Google Play listing: fastlane metadata icon.png (512, full-bleed)
  - Windows: Deskhub.ico
  - Linux:   deskhub-*.png

macOS, iOS, the Play Store and Android's adaptive-icon pipeline mask icons into their
own shapes, so those assets stay full-bleed squares. Windows, Linux and pre-API-26
Android launchers draw whatever they are given, so their icons carry the rounded shape
and its transparency baked in - otherwise the app shows up as a hard blue square next
to every other rounded icon.

Run by hand after changing the artwork:

    python3 scripts/make-icons.py

Pure standard library on purpose: bootstrap installs no image tooling.
"""

import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MASTER = ROOT / "assets/icon_1024.png"
WINDOWS_ICO = ROOT / "client/windows/win32/Deskhub.ico"
LINUX_ICON_DIR = ROOT / "client/linux/icons"

MACOS_APPICONSET = ROOT / "client/macos/app/Assets.xcassets/AppIcon.appiconset"
IOS_APPICON = ROOT / "client/ios/app/Assets.xcassets/AppIcon.appiconset/AppIcon.png"
ANDROID_RES = ROOT / "client/android/app/src/main/res"
PLAY_STORE_ICON = ROOT / "client/android/fastlane/metadata/android/en-US/images/icon.png"

WINDOWS_SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256]
LINUX_SIZES = [256, 512]
MACOS_SIZES = [16, 32, 64, 128, 256, 512]
ANDROID_LAUNCHER_SIZES = {"mdpi": 48, "hdpi": 72, "xhdpi": 96, "xxhdpi": 144, "xxxhdpi": 192}
ANDROID_FOREGROUND_SIZES = {"mdpi": 108, "hdpi": 162, "xhdpi": 216, "xxhdpi": 324, "xxxhdpi": 432}
PLAY_STORE_ICON_SIZE = 512

CORNER_RADIUS_FRACTION = 0.225
MASK_SUPERSAMPLE = 4

def read_png(path):
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit(f"{path}: not a PNG")

    pos = 8
    idat = b""
    width = height = depth = color_type = None
    while pos < len(data):
        (length,) = struct.unpack_from(">I", data, pos)
        chunk = data[pos + 4 : pos + 8]
        payload = data[pos + 8 : pos + 8 + length]
        if chunk == b"IHDR":
            width, height, depth, color_type = struct.unpack_from(">IIBB", payload, 0)
        elif chunk == b"IDAT":
            idat += payload
        pos += 12 + length

    if depth != 8 or color_type not in (2, 6):
        raise SystemExit(f"{path}: need an 8-bit RGB or RGBA PNG, got depth={depth} type={color_type}")

    channels = 3 if color_type == 2 else 4
    raw = zlib.decompress(idat)
    stride = width * channels
    rows = []
    previous = bytearray(stride)
    at = 0
    for _ in range(height):
        filter_type = raw[at]
        at += 1
        line = bytearray(raw[at : at + stride])
        at += stride
        for x in range(stride):
            left = line[x - channels] if x >= channels else 0
            up = previous[x]
            up_left = previous[x - channels] if x >= channels else 0
            if filter_type == 1:
                line[x] = (line[x] + left) & 0xFF
            elif filter_type == 2:
                line[x] = (line[x] + up) & 0xFF
            elif filter_type == 3:
                line[x] = (line[x] + (left + up) // 2) & 0xFF
            elif filter_type == 4:
                estimate = left + up - up_left
                d_left = abs(estimate - left)
                d_up = abs(estimate - up)
                d_up_left = abs(estimate - up_left)
                if d_left <= d_up and d_left <= d_up_left:
                    nearest = left
                elif d_up <= d_up_left:
                    nearest = up
                else:
                    nearest = up_left
                line[x] = (line[x] + nearest) & 0xFF
        rows.append(bytes(line))
        previous = line
    return width, height, channels, rows

def write_png(width, height, rgba_rows):
    def chunk(tag, payload):
        return (
            struct.pack(">I", len(payload))
            + tag
            + payload
            + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)
        )

    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    body = b"".join(b"\x00" + row for row in rgba_rows)
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", header)
        + chunk(b"IDAT", zlib.compress(body, 9))
        + chunk(b"IEND", b"")
    )

def corner_coverage(size, x, y):
    radius = CORNER_RADIUS_FRACTION * size
    step = 1.0 / MASK_SUPERSAMPLE
    inside = 0
    for sy in range(MASK_SUPERSAMPLE):
        py = y + (sy + 0.5) * step
        cy = min(max(py, radius), size - radius)
        for sx in range(MASK_SUPERSAMPLE):
            px = x + (sx + 0.5) * step
            cx = min(max(px, radius), size - radius)
            dx = px - cx
            dy = py - cy
            if dx * dx + dy * dy <= radius * radius:
                inside += 1
    return inside / float(MASK_SUPERSAMPLE * MASK_SUPERSAMPLE)

def resample(source, size, rounded=True):
    src_w, src_h, channels, rows = source
    rows_out = []
    for y in range(size):
        y0 = y * src_h // size
        y1 = max(y0 + 1, (y + 1) * src_h // size)
        line = bytearray()
        for x in range(size):
            x0 = x * src_w // size
            x1 = max(x0 + 1, (x + 1) * src_w // size)
            r = g = b = count = 0
            for sy in range(y0, y1):
                row = rows[sy]
                for sx in range(x0, x1):
                    base = sx * channels
                    r += row[base]
                    g += row[base + 1]
                    b += row[base + 2]
                    count += 1
            alpha = corner_coverage(size, x, y) if rounded else 1.0
            line += bytes(
                (r // count, g // count, b // count, int(round(alpha * 255)))
            )
        rows_out.append(bytes(line))
    return rows_out

def write_ico(path, frames):
    header = struct.pack("<HHH", 0, 1, len(frames))
    offset = len(header) + 16 * len(frames)
    entries = b""
    payloads = b""
    for size, png in frames:
        entries += struct.pack(
            "<BBBBHHII", size & 0xFF, size & 0xFF, 0, 0, 1, 32, len(png), offset
        )
        payloads += png
        offset += len(png)
    path.write_bytes(header + entries + payloads)

def write_android_background_color(source):
    _, _, _, rows = source
    r, g, b = rows[0][0], rows[0][1], rows[0][2]
    out = ANDROID_RES / "values" / "ic_launcher_background.xml"
    out.write_text(
        '<?xml version="1.0" encoding="utf-8"?>\n'
        "<resources>\n"
        f'    <color name="ic_launcher_background">#FF{r:02X}{g:02X}{b:02X}</color>\n'
        "</resources>\n"
    )
    print(f"wrote {out.relative_to(ROOT)}")

def main():
    if not MASTER.exists():
        raise SystemExit(f"{MASTER}: missing master artwork")

    source = read_png(MASTER)
    print(f"master {MASTER.relative_to(ROOT)} {source[0]}x{source[1]}")

    for size in MACOS_SIZES:
        out = MACOS_APPICONSET / f"icon_{size}.png"
        out.write_bytes(write_png(size, size, resample(source, size, rounded=False)))
        print(f"wrote {out.relative_to(ROOT)}")

    for out in [MACOS_APPICONSET / "icon_1024.png", IOS_APPICON]:
        out.write_bytes(MASTER.read_bytes())
        print(f"wrote {out.relative_to(ROOT)}")

    frames = []
    for size in WINDOWS_SIZES:
        png = write_png(size, size, resample(source, size))
        frames.append((size, png))
        print(f"  windows {size}x{size}")
    write_ico(WINDOWS_ICO, frames)
    print(f"wrote {WINDOWS_ICO.relative_to(ROOT)}")

    LINUX_ICON_DIR.mkdir(parents=True, exist_ok=True)
    for size in LINUX_SIZES:
        out = LINUX_ICON_DIR / f"deskhub-{size}.png"
        out.write_bytes(write_png(size, size, resample(source, size)))
        print(f"wrote {out.relative_to(ROOT)}")

    for density, size in ANDROID_LAUNCHER_SIZES.items():
        out = ANDROID_RES / f"mipmap-{density}" / "ic_launcher.png"
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_bytes(write_png(size, size, resample(source, size)))
        print(f"wrote {out.relative_to(ROOT)}")

    for density, size in ANDROID_FOREGROUND_SIZES.items():
        out = ANDROID_RES / f"mipmap-{density}" / "ic_launcher_foreground.png"
        out.write_bytes(write_png(size, size, resample(source, size, rounded=False)))
        print(f"wrote {out.relative_to(ROOT)}")

    write_android_background_color(source)

    PLAY_STORE_ICON.parent.mkdir(parents=True, exist_ok=True)
    PLAY_STORE_ICON.write_bytes(
        write_png(
            PLAY_STORE_ICON_SIZE,
            PLAY_STORE_ICON_SIZE,
            resample(source, PLAY_STORE_ICON_SIZE, rounded=False),
        )
    )
    print(f"wrote {PLAY_STORE_ICON.relative_to(ROOT)}")

    return 0

if __name__ == "__main__":
    sys.exit(main())
