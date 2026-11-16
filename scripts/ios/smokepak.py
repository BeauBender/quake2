#!/usr/bin/env python3
# scripts/ios/smokepak.py -- the synthetic game data of the iOS smoke run
# (TASK146). Python 3 standard library only (struct); run with python3 -I.
# Called by "scripts/ios-sim.sh smoke"; not part of the build.
#
# Writes <out>/baseq2/smoke.pak holding the two files ref_metal needs to
# reach the console, both made here from formulas; no id Software data is
# read, copied or derived:
#   pics/colormap.pcx  8-bit PCX whose 256-colour palette is a 3-3-2 RGB
#                      cube (GetPCXPalette in R_InitImages reads it);
#   pics/conchars.pcx  128x128 8-bit PCX of 16x16 glyph cells of 8x8, the
#                      cell of character code c showing the bits of c, one
#                      row per bit from bit 7 down (a bar for a set bit, a
#                      dot for a clear one) on index 255, the transparent
#                      index (R_FindPic("conchars") in RM_InitDraw).
#
#   smokepak.py --out DIR
import os
import struct
import sys

TRANSPARENT = 255
INK = 0xFE  # 3-3-2: (255, 255, 170)


def palette():
    """768 bytes: index i is r = bits 7-5, g = bits 4-2, b = bits 1-0."""
    out = bytearray()
    for i in range(256):
        out += bytes(((i >> 5) * 255 // 7, ((i >> 2) & 7) * 255 // 7, (i & 3) * 255 // 3))
    return bytes(out)


def rle_line(row):
    """One scan line in PCX run-length encoding."""
    out = bytearray()
    i = 0
    while i < len(row):
        v = row[i]
        n = 1
        while i + n < len(row) and row[i + n] == v and n < 63:
            n += 1
        if n > 1 or v >= 0xC0:
            out += bytes((0xC0 | n, v))
        else:
            out.append(v)
        i += n
    return bytes(out)


def pcx(width, height, pixels):
    """An 8-bit, one-plane PCX (version 5, RLE) with the palette at its end."""
    if width % 2 or len(pixels) != width * height:
        raise ValueError("pcx: the width must be even and the pixels width x height")
    head = struct.pack("<BBBBHHHHHH48sBBHH58s", 0x0A, 5, 1, 8, 0, 0, width - 1, height - 1,
                       72, 72, bytes(48), 0, 1, width, 1, bytes(58))
    assert len(head) == 128
    body = b"".join(rle_line(pixels[y * width:(y + 1) * width]) for y in range(height))
    return head + body + b"\x0c" + palette()


def colormap():
    """256x64 indices, each row the identity ramp: only its palette is read."""
    return pcx(256, 64, bytes(range(256)) * 64)


def conchars():
    w = h = 128
    pix = bytearray([TRANSPARENT]) * (w * h)
    for c in range(256):
        cx, cy = (c % 16) * 8, (c // 16) * 8
        for bit in range(8):
            y = cy + bit
            xs = range(cx + 1, cx + 7) if c & (0x80 >> bit) else (cx + 1,)
            for x in xs:
                pix[y * w + x] = INK
    return pcx(w, h, bytes(pix))


def pak(files):
    """A Quake II PACK: header, the files, then 64-byte directory entries."""
    data = bytearray(12)
    entries = bytearray()
    for name, blob in files:
        raw = name.encode("ascii")
        if len(raw) >= 56:
            raise ValueError("pak: name too long: " + name)
        entries += struct.pack("<56sii", raw, len(data), len(blob))
        data += blob
    data[0:12] = struct.pack("<4sii", b"PACK", len(data), len(entries))
    return bytes(data + entries)


def main(argv):
    if len(argv) != 3 or argv[1] != "--out":
        sys.stderr.write("usage: smokepak.py --out DIR\n")
        return 2
    base = os.path.join(argv[2], "baseq2")
    os.makedirs(base, exist_ok=True)
    path = os.path.join(base, "smoke.pak")
    blob = pak([("pics/colormap.pcx", colormap()), ("pics/conchars.pcx", conchars())])
    with open(path + ".tmp", "wb") as f:
        f.write(blob)
    os.replace(path + ".tmp", path)
    print("smokepak: wrote %s (%d bytes)" % (path, len(blob)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
