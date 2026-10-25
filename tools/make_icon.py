#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Generate res/DPKViewer.ico for the DPKViewer MFC project.

The icon is drawn procedurally (no external image libraries):
  - dark blue rounded square background
  - amber package/box with a lid, a vertical seam and a highlight
  - a small cyan corner mark standing for "inspect / unpack"

Usage:
  python make_icon.py                # writes res/DPKViewer.ico next to this script's project
  python make_icon.py --preview      # additionally writes res/icon_preview.png (self-check only)
"""
import os
import struct
import sys
import zlib

SIZES = (16, 24, 32, 48, 64, 128, 256)

BG = (0x1C, 0x33, 0x52)          # dark blue
BG_EDGE = (0x11, 0x22, 0x3A)     # darker rim
BOX = (0xE3, 0xA5, 0x3F)         # amber body
BOX_LIGHT = (0xF3, 0xC8, 0x74)   # lid
BOX_DARK = (0x8C, 0x5A, 0x1B)    # outline / shadow
MARK = (0x66, 0xC8, 0xE8)        # cyan mark
WHITE = (0xFF, 0xFF, 0xFF)


def mix(a, b, t):
    t = 0.0 if t < 0.0 else (1.0 if t > 1.0 else t)
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def rounded_rect_inside(x, y, x0, y0, x1, y1, r):
    if x < x0 or x > x1 or y < y0 or y > y1:
        return False
    cx = x0 + r if x < x0 + r else (x1 - r if x > x1 - r else x)
    cy = y0 + r if y < y0 + r else (y1 - r if y > y1 - r else y)
    if (cx - x) ** 2 + (cy - y) ** 2 > r * r:
        return False
    return True


def sample(u, v):
    """u, v in [0,1]; return (r,g,b,a)."""
    # background plate with rounded corners
    pad = 0.045
    r = 0.22
    if not rounded_rect_inside(u, v, pad, pad, 1.0 - pad, 1.0 - pad, r):
        return (0, 0, 0, 0)
    col = BG
    # rim shading
    d = min(u - pad, v - pad, 1.0 - pad - u, 1.0 - pad - v)
    if d < 0.035:
        col = mix(BG_EDGE, BG, d / 0.035)

    # package body
    bx0, bx1 = 0.24, 0.76
    by0, by1 = 0.48, 0.80
    lx0, lx1 = 0.20, 0.80
    ly0, ly1 = 0.34, 0.50
    if lx0 <= u <= lx1 and ly0 <= v <= ly1:
        col = BOX_LIGHT
        if abs(u - 0.5) < 0.022:
            col = BOX_DARK
        if v > ly1 - 0.045 or (ly0 <= v <= ly0 + 0.03):
            col = mix(col, BOX_DARK, 0.45)
    elif bx0 <= u <= bx1 and by0 <= v <= by1:
        col = BOX
        if abs(u - 0.5) < 0.022:
            col = BOX_DARK
        if u < bx0 + 0.045 or u > bx1 - 0.045:
            col = mix(col, BOX_LIGHT, 0.35)
        if v > by1 - 0.05:
            col = mix(col, BOX_DARK, 0.55)
    # cyan corner mark (bottom-right), reads as "inspect / extract"
    mx, my = 0.80, 0.20
    if (u - mx) ** 2 + (v - my) ** 2 <= 0.145 ** 2:
        col = MARK
        if 0.055 < ((u - mx) ** 2 + (v - my) ** 2) ** 0.5 < 0.085:
            col = WHITE
    return (col[0], col[1], col[2], 255)


def render(size):
    """Supersampled RGBA render, returns list of rows (top-down) of (r,g,b,a)."""
    ss = 4
    width = size
    rows = []
    big = size * ss
    for py in range(size):
        row = []
        for px in range(size):
            acc = [0, 0, 0, 0]
            for sy in range(ss):
                for sx in range(ss):
                    u = (px * ss + sx + 0.5) / big
                    v = (py * ss + sy + 0.5) / big
                    r, g, b, a = sample(u, v)
                    acc[0] += r * a
                    acc[1] += g * a
                    acc[2] += b * a
                    acc[3] += a
            n = ss * ss
            a = acc[3] / n
            if a <= 0.0:
                row.append((0, 0, 0, 0))
            else:
                row.append((int(round(acc[0] / acc[3])), int(round(acc[1] / acc[3])), int(round(acc[2] / acc[3])),
                            int(round(a))))
        rows.append(row)
    return rows


def ico_bmp(rows):
    """BITMAPINFOHEADER + 32bpp BGRA (bottom-up) + AND mask."""
    h = len(rows)
    w = len(rows[0])
    header = struct.pack("<IiiHHIIiiII", 40, w, h * 2, 1, 32, 0, w * h * 4, 0, 0, 0, 0)
    pixels = bytearray()
    for y in range(h - 1, -1, -1):
        for x in range(w):
            r, g, b, a = rows[y][x]
            pixels += bytes((b, g, r, a))
    mask_row = ((w + 31) // 32) * 4
    mask = bytearray()
    for y in range(h - 1, -1, -1):
        line = bytearray(mask_row)
        for x in range(w):
            if rows[y][x][3] < 128:
                line[x // 8] |= 0x80 >> (x % 8)
        mask += line
    return header + bytes(pixels) + bytes(mask)


def png_bytes(rows):
    """RGBA PNG 字节（与 write_png 相同编码）。ICO 内大尺寸用 PNG 存储：
    视觉无损，但比 32bpp BMP 小一个数量级（256px 约 270KB -> 20KB 上下）。"""
    h = len(rows)
    w = len(rows[0])
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for x in range(w):
            r, g, b, a = rows[y][x]
            raw += bytes((r, g, b, a))
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    return png


def write_ico(path, sizes):
    images = []
    for s in sizes:
        rows = render(s)
        images.append((s, png_bytes(rows) if s >= 128 else ico_bmp(rows)))
    out = bytearray()
    out += struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    for s, data in images:
        out += struct.pack("<BBBBHHII", 0 if s >= 256 else s, 0 if s >= 256 else s, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    for _, data in images:
        out += data
    with open(path, "wb") as f:
        f.write(out)
    return len(out)


def write_png(path, rows):
    h = len(rows)
    w = len(rows[0])
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for x in range(w):
            r, g, b, a = rows[y][x]
            raw += bytes((r, g, b, a))
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)
    return len(png)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    res = os.path.join(here, "..", "res")
    res = os.path.abspath(res)
    ico_path = os.path.join(res, "DPKViewer.ico")
    n = write_ico(ico_path, SIZES)
    print("wrote %s (%d bytes, %d sizes)" % (ico_path, n, len(SIZES)))
    if "--preview" in sys.argv:
        png_path = os.path.join(res, "icon_preview.png")
        m = write_png(png_path, render(256))
        print("wrote %s (%d bytes)" % (png_path, m))


if __name__ == "__main__":
    main()