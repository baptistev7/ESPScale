#!/usr/bin/env python3
"""Coupe le fond uni en bas d'une capture PNG (Firefox headless capture toute
la fenêtre, pas la page) : garde 20 px sous le dernier contenu."""
import struct
import sys
import zlib


def read_png(path):
    b = open(path, "rb").read()
    pos, idat = 8, b""
    while pos < len(b):
        n, = struct.unpack(">I", b[pos:pos + 4])
        typ, chunk = b[pos + 4:pos + 8], b[pos + 8:pos + 8 + n]
        if typ == b"IHDR":
            w, h, _, ct = struct.unpack(">IIBB", chunk[:10])
        elif typ == b"IDAT":
            idat += chunk
        pos += 12 + n
    bpp = {6: 4, 2: 3, 0: 1}[ct]
    raw, stride = zlib.decompress(idat), w * bpp
    rows, prev = [], bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            up = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + up) & 255
            elif f == 3:
                line[x] = (line[x] + ((a + up) >> 1)) & 255
            elif f == 4:
                p = a + up - c
                pa, pb, pc = abs(p - a), abs(p - up), abs(p - c)
                pr = a if pa <= pb and pa <= pc else (up if pb <= pc else c)
                line[x] = (line[x] + pr) & 255
        rows.append(bytes(line))
        prev = line
    return w, ct, rows


def write_png(path, w, ct, rows):
    raw = b"".join(b"\x00" + r for r in rows)

    def chunk(t, c):
        return struct.pack(">I", len(c)) + t + c + struct.pack(">I", zlib.crc32(t + c) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", w, len(rows), 8, ct, 0, 0, 0)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
                           chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


for src, dst in zip(sys.argv[1::2], sys.argv[2::2]):
    w, ct, rows = read_png(src)
    y = len(rows) - 1
    while y > 0 and rows[y] == rows[-1]:
        y -= 1
    write_png(dst, w, ct, rows[:min(len(rows), y + 20)])
    print(f"  {dst}  ({w}x{min(len(rows), y + 20)})")
