#!/usr/bin/env python3
"""Regenerate the Windows and Linux app icons from the macOS master artwork.

macOS and iOS mask app icons into the system squircle themselves, so their assets stay
full-bleed squares. Windows and Linux draw whatever they are given, so their icons have
to carry the rounded shape and its transparency baked in - otherwise the app shows up as
a hard blue square next to every other rounded icon.

Run by hand after changing the artwork:

    python3 scripts/make-icons.py

Pure standard library on purpose: bootstrap installs no image tooling.
"""

import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MASTER = ROOT / "client/macos/app/Assets.xcassets/AppIcon.appiconset/icon_1024.png"
WINDOWS_ICO = ROOT / "client/windows/win32/Deskhub.ico"
LINUX_ICON_DIR = ROOT / "client/linux/icons"

WINDOWS_SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256]
LINUX_SIZES = [256, 512]

# Corner radius as a fraction of the icon edge. 0.225 is the ratio Apple uses for the
# macOS/iOS icon shape, so the result sits next to native icons without looking off.
CORNER_RADIUS_FRACTION = 0.225
# Subsamples per axis used to antialias the mask edge.
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


def resample(source, size):
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
            alpha = corner_coverage(size, x, y)
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


def main():
    if not MASTER.exists():
        raise SystemExit(f"{MASTER}: missing master artwork")

    source = read_png(MASTER)
    print(f"master {MASTER.relative_to(ROOT)} {source[0]}x{source[1]}")

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

    return 0


if __name__ == "__main__":
    sys.exit(main())
