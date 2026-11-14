#!/usr/bin/env python3
# scripts/pins/evidence.py -- pull-request evidence from pin frames (TASK73).
# Python 3 standard library only (zlib, struct, hashlib). Called by
# "scripts/pins.sh evidence" and "evidence-publish"; not part of the build.
#
# Reads the uncompressed TGA frames the pin harness writes and writes PNG,
# animated PNG and GIF with its own encoders: a side-by-side still of one
# frame (reference, candidate, absolute difference amplified), a short clip
# of the same three panels over every frame, and a manifest naming the
# command, the commit, the pin ids, the scores and the SHA-256 of each
# output. File names and the manifest hold no host name, user name or
# absolute path.
#
#   evidence.py build --ref-dir D --cand-dir D --compare F --out D --prefix P [--meta k=v]...
#   evidence.py synth --out D --prefix P [--noise BYTES | --size WxHxFRAMES] [--meta k=v]...
#   evidence.py stage --root D --store D --manifest M --stage D --repo O/N --markdown F [-- file...]
#   evidence.py selftest
#
# Captions use a built-in 5x7 bitmap font; no font file is read.
import hashlib
import os
import re
import shutil
import socket
import stat
import struct
import sys
import zlib

BUDGET = os.environ.get('PINS_EVIDENCE_BUDGET', '4000000')  # bytes per file
if not re.fullmatch(r'[1-9][0-9]{0,9}', BUDGET, re.ASCII):
    raise SystemExit('evidence: PINS_EVIDENCE_BUDGET must be a positive whole number of bytes')
BUDGET = int(BUDGET)
DIFF_GAIN = 8          # the difference panel shows |ref - cand| * 8, clamped
CLIP_DELAY_MS = 500    # each clip frame is shown for half a second
CLIP_MAX_FRAMES = 48   # a longer pin is subsampled evenly to this many frames
MANIFEST_MAGIC = 'pins-evidence 1'

# ---- images ----------------------------------------------------------------
# An image is (w, h, bytearray of w*h*3 RGB bytes, rows top to bottom).


def read_tga(path):
    with open(path, 'rb') as f:
        data = f.read()
    if len(data) < 18:
        raise ValueError('%s: not a TGA file' % os.path.basename(path))
    idlen, cmtype, itype = data[0], data[1], data[2]
    w, h = struct.unpack('<HH', data[12:16])
    bpp, desc = data[16], data[17]
    if itype != 2 or cmtype != 0 or bpp not in (24, 32) or w == 0 or h == 0:
        raise ValueError('%s: not an uncompressed 24/32-bit truecolor TGA' % os.path.basename(path))
    n = bpp // 8
    off = 18 + idlen
    px = data[off:off + w * h * n]
    if len(px) != w * h * n:
        raise ValueError('%s: short TGA' % os.path.basename(path))
    rgb = bytearray(w * h * 3)
    rgb[0::3] = px[2::n]
    rgb[1::3] = px[1::n]
    rgb[2::3] = px[0::n]
    if not desc & 0x20:  # bottom-left origin: flip to top-down
        stride = w * 3
        rgb = bytearray(b''.join(bytes(rgb[y * stride:(y + 1) * stride]) for y in range(h - 1, -1, -1)))
    return (w, h, rgb)


def write_tga(path, img, bottom_up=True, bpp=24):
    w, h, rgb = img
    n = bpp // 8
    px = bytearray(w * h * n)
    px[0::n] = rgb[2::3]
    px[1::n] = rgb[1::3]
    px[2::n] = rgb[0::3]
    if n == 4:
        px[3::4] = b'\xff' * (w * h)
    stride = w * n
    if bottom_up:
        px = b''.join(bytes(px[y * stride:(y + 1) * stride]) for y in range(h - 1, -1, -1))
    hdr = struct.pack('<BBBHHBHHHHBB', 0, 0, 2, 0, 0, 0, 0, 0, w, h, bpp, 0 if bottom_up else 0x20)
    with open(path, 'wb') as f:
        f.write(hdr + bytes(px))


def blank(w, h, rgb=(0, 0, 0)):
    return (w, h, bytearray(bytes(rgb) * (w * h)))


def fill(img, x0, y0, w, h, rgb):
    iw, ih, px = img
    x1, y1 = min(iw, x0 + w), min(ih, y0 + h)
    x0, y0 = max(0, x0), max(0, y0)
    if x1 <= x0 or y1 <= y0:
        return
    run = bytes(rgb) * (x1 - x0)
    for y in range(y0, y1):
        o = (y * iw + x0) * 3
        px[o:o + len(run)] = run


def paste(dst, src, x0, y0):
    dw, dh, dp = dst
    sw, sh, sp = src
    for y in range(sh):
        if 0 <= y0 + y < dh:
            o = ((y0 + y) * dw + x0) * 3
            dp[o:o + sw * 3] = sp[y * sw * 3:(y + 1) * sw * 3]


def downscale(img, f):
    """Nearest-neighbour reduction by the integer factor f."""
    w, h, px = img
    if f == 1:
        return img
    nw, nh = w // f, h // f
    out = bytearray(nw * nh * 3)
    stride = w * 3
    for y in range(nh):
        row = px[y * f * stride:(y * f) * stride + stride]
        o = y * nw * 3
        seg = bytearray(nw * 3)
        seg[0::3] = row[0::3 * f][:nw]
        seg[1::3] = row[1::3 * f][:nw]
        seg[2::3] = row[2::3 * f][:nw]
        out[o:o + nw * 3] = seg
    return (nw, nh, out)


_DIFF_TABLE = None


def absdiff(a, b, gain=DIFF_GAIN):
    """|a - b| * gain per channel, clamped to 255."""
    global _DIFF_TABLE
    w, h, pa = a
    if (w, h) != b[:2]:
        raise ValueError('frame sizes differ: %dx%d and %dx%d' % (w, h, b[0], b[1]))
    pb = b[2]
    if pa == pb:
        return blank(w, h)
    if _DIFF_TABLE is None:
        _DIFF_TABLE = bytes(min(255, abs((i >> 8) - (i & 255)) * gain) for i in range(65536))
    t = _DIFF_TABLE
    return (w, h, bytearray(t[(x << 8) | y] for x, y in zip(pa, pb)))


# ---- the 5x7 caption font --------------------------------------------------
# Seven rows per glyph, top to bottom, five bits each (bit 4 is the left column).
FONT = {
    '0': '0E1113151911' '0E', '1': '040C04040404' '0E', '2': '0E1101020408' '1F',
    '3': '1F0204020111' '0E', '4': '02060A121F02' '02', '5': '1F101E010111' '0E',
    '6': '0608101E1111' '0E', '7': '1F0102040808' '08', '8': '0E11110E1111' '0E',
    '9': '0E11110F0102' '0C', 'A': '0E1111111F11' '11', 'B': '1E11111E1111' '1E',
    'C': '0E1110101011' '0E', 'D': '1C1211111112' '1C', 'E': '1F10101E1010' '1F',
    'F': '1F10101E1010' '10', 'G': '0E1110171111' '0F', 'H': '1111111F1111' '11',
    'I': '0E0404040404' '0E', 'J': '070202020212' '0C', 'K': '111214181412' '11',
    'L': '101010101010' '1F', 'M': '111B15151111' '11', 'N': '111119151311' '11',
    'O': '0E1111111111' '0E', 'P': '1E11111E1010' '10', 'Q': '0E1111111512' '0D',
    'R': '1E11111E1412' '11', 'S': '0F10100E0101' '1E', 'T': '1F0404040404' '04',
    'U': '111111111111' '0E', 'V': '11111111110A' '04', 'W': '111111151515' '0A',
    'X': '11110A040A11' '11', 'Y': '1111110A0404' '04', 'Z': '1F0102040810' '1F',
    ' ': '000000000000' '00', '-': '0000001F0000' '00', '.': '000000000000' '0C',
    ':': '000C0C000C0C' '00', '=': '00001F001F00' '00', '/': '000102040810' '00',
    '(': '020408080804' '02', ')': '080402020204' '08', '_': '000000000000' '1F',
    '#': '0A0A1F0A1F0A' '0A', '+': '0004041F0404' '00', '?': '0E1101020400' '04',
    '|': '040404040404' '04',
}
FONT_W, FONT_H = 5, 7


def text_width(text, scale):
    return len(text) * (FONT_W + 1) * scale


def draw_text(img, x, y, text, scale=1, rgb=(255, 255, 255)):
    for ch in text.upper():
        rows = FONT.get(ch, FONT['?'])
        for r in range(FONT_H):
            bits = int(rows[2 * r:2 * r + 2], 16)
            for c in range(FONT_W):
                if bits & (0x10 >> c):
                    fill(img, x + c * scale, y + r * scale, scale, scale, rgb)
        x += (FONT_W + 1) * scale


def compose(panels, captions):
    """Panels side by side, each under a caption bar, with a 4-pixel gutter."""
    gap = 4
    pw, ph = panels[0][0], panels[0][1]
    scale = 2 if pw >= 300 else 1
    bar = FONT_H * scale + 6
    out = blank(len(panels) * pw + (len(panels) - 1) * gap, ph + bar, (24, 24, 24))
    for i, (p, cap) in enumerate(zip(panels, captions)):
        x = i * (pw + gap)
        while cap and text_width(cap, scale) > pw - 4:
            cap = cap[:-1]
        draw_text(out, x + 3, 3, cap, scale)
        paste(out, p, x, bar)
    return out


# ---- PNG and animated PNG --------------------------------------------------


def _chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)


def _bytesub(a, b, H, L, n):
    """(a - b) mod 256 for every byte, on whole rows at once (SWAR)."""
    x = int.from_bytes(a, 'big')
    y = int.from_bytes(b, 'big')
    return (((x | H) - (y & L)) ^ ((x ^ ~y) & H)).to_bytes(n, 'big')


def png_idat(img):
    """The zlib stream of the image, filtered with whichever of None, Sub and
    Up compresses smallest over the whole image."""
    w, h, px = img
    n = w * 3
    rows = [bytes(px[y * n:(y + 1) * n]) for y in range(h)]
    H = int.from_bytes(b'\x80' * n, 'big')
    L = int.from_bytes(b'\x7f' * n, 'big')
    zero = bytes(n)
    best = None
    for ftype in (0, 1, 2):
        if ftype == 0:
            body = b''.join(b'\x00' + r for r in rows)
        elif ftype == 1:
            body = b''.join(b'\x01' + _bytesub(r, b'\x00\x00\x00' + r[:-3], H, L, n) for r in rows)
        else:
            body = b''.join(b'\x02' + _bytesub(r, p, H, L, n) for r, p in zip(rows, [zero] + rows[:-1]))
        z = zlib.compress(body, 9)
        if best is None or len(z) < len(best):
            best = z
    return best


def _ihdr(w, h):
    return _chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))


PNG_SIG = b'\x89PNG\r\n\x1a\n'


def encode_png(img):
    return PNG_SIG + _ihdr(img[0], img[1]) + _chunk(b'IDAT', png_idat(img)) + _chunk(b'IEND', b'')


def encode_apng(frames, delay_ms=CLIP_DELAY_MS):
    """Animated PNG: every frame full size, replacing the last (blend SOURCE),
    looping forever. The first frame is also the still image."""
    w, h = frames[0][0], frames[0][1]
    out = [PNG_SIG, _ihdr(w, h), _chunk(b'acTL', struct.pack('>II', len(frames), 0))]
    seq = 0
    for i, fr in enumerate(frames):
        if fr[:2] != (w, h):
            raise ValueError('clip frames differ in size')
        out.append(_chunk(b'fcTL', struct.pack('>IIIIIHHBB', seq, w, h, 0, 0, delay_ms, 1000, 0, 0)))
        seq += 1
        z = png_idat(fr)
        if i == 0:
            out.append(_chunk(b'IDAT', z))
        else:
            out.append(_chunk(b'fdAT', struct.pack('>I', seq) + z))
            seq += 1
    out.append(_chunk(b'IEND', b''))
    return b''.join(out)


def png_chunks(data):
    """[(kind, payload)] of a PNG; raises when the signature, a length or a CRC is wrong."""
    if data[:8] != PNG_SIG:
        raise ValueError('not a PNG signature')
    pos, out = 8, []
    while pos < len(data):
        if pos + 8 > len(data):
            raise ValueError('truncated chunk header')
        n, = struct.unpack('>I', data[pos:pos + 4])
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + n]
        if len(body) != n or pos + 12 + n > len(data):
            raise ValueError('chunk %r length runs past the end' % kind)
        crc, = struct.unpack('>I', data[pos + 8 + n:pos + 12 + n])
        if crc != zlib.crc32(kind + body) & 0xffffffff:
            raise ValueError('chunk %r CRC mismatch' % kind)
        out.append((kind, body))
        pos += 12 + n
        if kind == b'IEND':
            break
    if pos != len(data) or not out or out[-1][0] != b'IEND':
        raise ValueError('no IEND at the end')
    return out


def _unfilter(raw, w, h):
    n = w * 3
    out = bytearray()
    prev = bytearray(n)
    pos = 0
    for _ in range(h):
        ft = raw[pos]
        line = bytearray(raw[pos + 1:pos + 1 + n])
        pos += 1 + n
        for i in range(n):
            a = line[i - 3] if i >= 3 else 0
            b = prev[i]
            c = prev[i - 3] if i >= 3 else 0
            if ft == 1:
                line[i] = (line[i] + a) & 255
            elif ft == 2:
                line[i] = (line[i] + b) & 255
            elif ft == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif ft == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
            elif ft != 0:
                raise ValueError('bad filter type %d' % ft)
        out += line
        prev = line
    return out


def decode_png(data):
    """[(w, h, rgb)] for every frame of an 8-bit RGB PNG or APNG (frames all full size)."""
    chunks = png_chunks(data)
    if chunks[0][0] != b'IHDR':
        raise ValueError('IHDR is not first')
    w, h, depth, ctype = struct.unpack('>IIBB', chunks[0][1][:10])
    if depth != 8 or ctype != 2:
        raise ValueError('only 8-bit RGB is read')
    frames, cur, seq, declared = [], None, 0, None
    for kind, body in chunks[1:]:
        if kind == b'acTL':
            declared = struct.unpack('>II', body)[0]
        elif kind == b'fcTL':
            s, fw, fh = struct.unpack('>III', body[:12])
            if s != seq:
                raise ValueError('fcTL sequence %d, expected %d' % (s, seq))
            seq += 1
            if (fw, fh) != (w, h):
                raise ValueError('partial frames are not read')
            if cur is not None:
                frames.append(cur)
            cur = b''
        elif kind == b'IDAT':
            cur = (cur or b'') + body
        elif kind == b'fdAT':
            s, = struct.unpack('>I', body[:4])
            if s != seq:
                raise ValueError('fdAT sequence %d, expected %d' % (s, seq))
            seq += 1
            cur += body[4:]
    if cur is not None:
        frames.append(cur)
    if declared is not None and declared != len(frames):
        raise ValueError('acTL declares %d frames, found %d' % (declared, len(frames)))
    return [(w, h, _unfilter(zlib.decompress(z), w, h)) for z in frames]


# ---- GIF ---------------------------------------------------------------------
# A fixed 3-3-2 palette (8 levels of red and green, 4 of blue): no search, no
# per-image palette, so every frame of every clip shares one colour table.

def _level(v, n):
    return (v * (n - 1) + 127) // 255


_QR = bytes(_level(v, 8) << 5 for v in range(256))
_QG = bytes(_level(v, 8) << 2 for v in range(256))
_QB = bytes(_level(v, 4) for v in range(256))
GIF_PALETTE = b''.join(bytes(((i >> 5) * 255 // 7, ((i >> 2) & 7) * 255 // 7, (i & 3) * 255 // 3)) for i in range(256))


def quantize(img):
    w, h, px = img
    n = w * h
    r = int.from_bytes(bytes(px[0::3]).translate(_QR), 'big')
    g = int.from_bytes(bytes(px[1::3]).translate(_QG), 'big')
    b = int.from_bytes(bytes(px[2::3]).translate(_QB), 'big')
    return (r | g | b).to_bytes(n, 'big')


def lzw_encode(idx):
    """GIF LZW with a minimum code size of 8; a clear code whenever the table fills."""
    out = bytearray()
    acc = nbits = 0
    size = 9

    def emit(code):
        nonlocal acc, nbits
        acc |= code << nbits
        nbits += size
        while nbits >= 8:
            out.append(acc & 255)
            acc >>= 8
            nbits -= 8

    emit(256)
    table, nxt = {}, 258
    w = idx[0]
    for k in idx[1:]:
        key = (w << 8) | k
        c = table.get(key)
        if c is not None:
            w = c
            continue
        emit(w)
        if nxt < 4096:
            table[key] = nxt
            nxt += 1
            if nxt > (1 << size) and size < 12:
                size += 1
        else:
            emit(256)
            table, nxt, size = {}, 258, 9
        w = k
    emit(w)
    emit(257)
    if nbits:
        out.append(acc & 255)
    return bytes(out)


def lzw_decode(data, npix, strict=False):
    """The inverse of lzw_encode, for the self-test and for reading a GIF back.
    strict: nothing may follow the end code but the zero bits of its last byte."""
    pos = acc = nbits = 0
    size = 9
    table = [bytes([i]) for i in range(256)] + [b'', b'']
    out = bytearray()
    prev = None
    while True:
        while nbits < size:
            if pos >= len(data):
                raise ValueError('LZW stream ends without an end code')
            acc |= data[pos] << nbits
            pos += 1
            nbits += 8
        code = acc & ((1 << size) - 1)
        acc >>= size
        nbits -= size
        if code == 256:
            table = table[:258]
            size = 9
            prev = None
            continue
        if code == 257:
            if strict and (pos != len(data) or acc):
                raise ValueError('data after the LZW end code')
            break
        if prev is None:
            entry = table[code]
        elif code < len(table):
            entry = table[code]
            if len(table) < 4096:
                table.append(prev + entry[:1])
        elif code == len(table):
            entry = prev + prev[:1]
            table.append(entry)
        else:
            raise ValueError('LZW code %d out of range' % code)
        out += entry
        prev = entry
        if len(table) >= (1 << size) and size < 12:
            size += 1
    if len(out) != npix:
        raise ValueError('LZW gave %d pixels, expected %d' % (len(out), npix))
    return bytes(out)


def encode_gif(frames, delay_ms=CLIP_DELAY_MS):
    w, h = frames[0][0], frames[0][1]
    out = [b'GIF89a', struct.pack('<HHBBB', w, h, 0xF7, 0, 0), GIF_PALETTE,
           b'\x21\xff\x0bNETSCAPE2.0\x03\x01\x00\x00\x00']
    for fr in frames:
        if fr[:2] != (w, h):
            raise ValueError('clip frames differ in size')
        out.append(b'\x21\xf9\x04\x04' + struct.pack('<H', max(2, delay_ms // 10)) + b'\x00\x00')
        out.append(b'\x2c' + struct.pack('<HHHHB', 0, 0, w, h, 0) + b'\x08')
        z = lzw_encode(quantize(fr))
        out.append(b''.join(bytes([len(z[i:i + 255])]) + z[i:i + 255] for i in range(0, len(z), 255)) + b'\x00')
    out.append(b'\x3b')
    return b''.join(out)


def decode_gif(data):
    """[palette indices] of every frame of a GIF this encoder wrote."""
    if data[:6] != b'GIF89a':
        raise ValueError('not GIF89a')
    w, h, packed = struct.unpack('<HHB', data[6:11])
    pos = 13 + (3 << ((packed & 7) + 1) if packed & 0x80 else 0)
    frames = []
    while True:
        t = data[pos]
        if t == 0x3b:
            break
        if t == 0x21:
            pos += 2
            while data[pos]:
                pos += data[pos] + 1
            pos += 1
        elif t == 0x2c:
            fw, fh = struct.unpack('<HH', data[pos + 5:pos + 9])
            pos += 10 + 1
            z = bytearray()
            while data[pos]:
                z += data[pos + 1:pos + 1 + data[pos]]
                pos += data[pos] + 1
            pos += 1
            frames.append(lzw_decode(bytes(z), fw * fh))
        else:
            raise ValueError('unknown GIF block 0x%02x' % t)
    return w, h, frames


# ---- budget ------------------------------------------------------------------


def fit(make, start, limit=BUDGET, largest=8):
    """(bytes, factor) for the smallest integer reduction from start up that fits."""
    for f in range(start, largest + 1):
        data = make(f)
        if len(data) <= limit:
            return data, f
    raise SystemExit('evidence: no reduction up to 1/%d fits the budget of %d bytes' % (largest, limit))


# ---- manifest ----------------------------------------------------------------


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 16), b''):
            h.update(block)
    return h.hexdigest()


def write_outputs(out, prefix, meta, files):
    """files: [(suffix, kind, data, w, h, factor)]. Writes them and the manifest."""
    os.makedirs(out, exist_ok=True)
    lines = [MANIFEST_MAGIC]
    for k, v in meta:
        lines.append('%s %s' % (k, v))
    for suffix, kind, data, w, h, f in files:
        name = '%s-%s' % (prefix, suffix)
        with open(os.path.join(out, name), 'wb') as fp:
            fp.write(data)
        lines.append('file %s %d %s %s %dx%d 1/%d' % (name, len(data), hashlib.sha256(data).hexdigest(), kind, w, h, f))
    text = '\n'.join(lines) + '\n'
    identity_check(text.encode(), 'the manifest')
    mpath = os.path.join(out, prefix + '-evidence.txt')
    with open(mpath, 'w') as fp:
        fp.write(text)
    for suffix, kind, data, w, h, f in files:
        print('wrote %s-%s (%s, %dx%d, %d bytes, 1/%d)' % (prefix, suffix, kind, w, h, len(data), f))
    print('manifest %s-evidence.txt' % prefix)


def parse_meta(pairs):
    meta = []
    for p in pairs:
        k, _, v = p.partition('=')
        if not re.match(r'^[a-z][a-z0-9-]*$', k) or '\n' in v:
            raise SystemExit('evidence: bad --meta %r' % p)
        meta.append((k, v))
    return meta


# ---- identity ----------------------------------------------------------------


def identity_strings():
    out = set()
    home = os.path.expanduser('~')
    if home and home not in ('/', '~'):
        out.add(home)
    for k in ('USER', 'LOGNAME'):
        v = os.environ.get(k, '')
        if len(v) >= 3:
            out.add(v)
    try:
        import pwd
        v = pwd.getpwuid(os.getuid()).pw_name
        if len(v) >= 3:
            out.add(v)
    except (ImportError, KeyError):
        pass
    host = socket.gethostname()
    for v in (host, host.split('.')[0]):
        if len(v) >= 3 and v != 'localhost':
            out.add(v)
    return out


# the roots of home directories on macOS, Linux and Windows, spelled so that
# a scan of the tree for local paths does not take this list for one
HOME_ROOTS = tuple(b'%s%s%s' % (a, b, c) for a, b, c in ((b'/', b'Users', b'/'), (b'/', b'home', b'/'), (b'C:', b'\\Users', b'')))


def identity_check(data, what):
    for s in identity_strings():
        if s.encode() in data:
            raise SystemExit('evidence: refused: %s holds a host name, user name or home path' % what)
    for s in HOME_ROOTS:
        if s in data:
            raise SystemExit('evidence: refused: %s holds an absolute home path' % what)


# ---- build -------------------------------------------------------------------


def parse_compare(path):
    """[(frame, score, bad, verdict)] and the summary line of pins.sh compare."""
    rows, summary = [], ''
    with open(path) as f:
        for line in f:
            p = line.split()
            if len(p) == 4 and SCORE_RE.fullmatch(' '.join(p)):
                rows.append(p)
            elif line.startswith('frames='):
                summary = ' '.join(t for t in p if re.match(r'^[a-z_]+=[0-9.]+$', t))
    return rows, summary


def cmd_build(a):
    rows, summary = parse_compare(a['compare'])
    scored = [r for r in rows if r[1] != '-']
    if not scored:
        raise SystemExit('evidence: the comparison lists no frame present in both pins')
    worst = max(scored, key=lambda r: (float(r[1]), float(r[2]) if r[2] != '-' else 0.0))
    given = dict(parse_meta(a['meta']))
    ref_cap, cand_cap = given.get('ref-caption', 'REFERENCE'), given.get('cand-caption', 'CANDIDATE')
    diff_cap = 'DIFFERENCE X%d' % DIFF_GAIN

    def load(d, name):
        return read_tga(os.path.join(d, name))

    r0, c0 = load(a['ref-dir'], worst[0]), load(a['cand-dir'], worst[0])
    d0 = absdiff(r0, c0)
    still_cap = [ref_cap, cand_cap, diff_cap + ' ' + worst[0][:-4]]

    def still(f):
        img = compose([downscale(x, f) for x in (r0, c0, d0)], still_cap)
        still.dims = img[:2]
        return encode_png(img)

    still_png, sf = fit(still, 1)
    sdims = still.dims
    names = [r[0] for r in scored]
    if len(names) > CLIP_MAX_FRAMES:
        step = len(names) / CLIP_MAX_FRAMES
        names = [names[int(i * step)] for i in range(CLIP_MAX_FRAMES)]
    pairs = [(load(a['ref-dir'], n), load(a['cand-dir'], n)) for n in names]
    cache = {}

    def clip_frames(f):
        if f not in cache:
            fr = []
            for n, (r, c) in zip(names, pairs):
                rs, cs = downscale(r, f), downscale(c, f)
                fr.append(compose([rs, cs, absdiff(rs, cs)], [ref_cap, cand_cap, diff_cap + ' ' + n[:-4]]))
            cache.clear()
            cache[f] = fr
        return cache[f]

    def apng(f):
        return encode_apng(clip_frames(f))

    def gif(f):
        return encode_gif(clip_frames(f))

    clip_png, cf = fit(apng, 2)
    cdims = clip_frames(cf)[0][:2]
    clip_gif, gf = fit(gif, 2)
    gdims = clip_frames(gf)[0][:2]
    meta = parse_meta(a['meta'])
    meta += [('frames', str(len(rows))), ('clip-frames', str(len(names))),
             ('clip-delay-ms', str(CLIP_DELAY_MS)), ('diff-gain', str(DIFF_GAIN)),
             ('budget', str(BUDGET)), ('worst-frame', worst[0]), ('summary', summary)]
    meta += [('score', ' '.join(r)) for r in rows]
    write_outputs(a['out'], a['prefix'], meta, [
        ('still.png', 'png', still_png, sdims[0], sdims[1], sf),
        ('clip.png', 'apng', clip_png, cdims[0], cdims[1], cf),
        ('clip.gif', 'gif', clip_gif, gdims[0], gdims[1], gf)])


# ---- synthetic test pattern ------------------------------------------------

BARS = [(235, 235, 235), (235, 235, 16), (16, 235, 235), (16, 235, 16),
        (235, 16, 235), (235, 16, 16), (16, 16, 235)]


def pattern(w, h, frame=None, total=1):
    img = blank(w, h, (16, 16, 16))
    bw = w // len(BARS)
    for i, c in enumerate(BARS):
        fill(img, i * bw, 0, bw if i < len(BARS) - 1 else w - i * bw, h * 2 // 3, c)
    for x in range(w):  # grey ramp along the bottom third
        v = x * 255 // (w - 1)
        fill(img, x, h * 2 // 3, 1, h - h * 2 // 3, (v, v, v))
    draw_text(img, 6, 6, 'SYNTHETIC TEST PATTERN', 2, (0, 0, 0))
    if frame is not None:
        s = 32
        x = (w - s) * frame // max(1, total - 1)
        fill(img, x, h // 3 - s // 2, s, s, (0, 0, 0))
        fill(img, x + 4, h // 3 - s // 2 + 4, s - 8, s - 8, (255, 128, 0))
        draw_text(img, 6, h - 22, 'FRAME %02d OF %02d' % (frame + 1, total), 2, (255, 0, 0))
    return img


def noise(w, h, seed):
    n = w * h * 3
    out = bytearray()
    i = 0
    while len(out) < n:
        out += hashlib.sha256(b'pins-evidence-noise %d %d' % (seed, i)).digest()
        i += 1
    return (w, h, out[:n])


def cmd_synth(a):
    meta = parse_meta(a['meta']) + [('content', 'synthetic: colour bars, a moving square, frame numbers; no game content')]
    if a.get('noise'):
        target = int(a['noise'])
        w, h = 512, 512
        count = max(1, round(target / (w * h * 3)))
        frames = [noise(w, h, i) for i in range(count)]
        for i, fr in enumerate(frames):
            fill(fr, 0, 0, w, 24, (0, 0, 0))
            draw_text(fr, 6, 5, 'NOISE %d OF %d' % (i + 1, count), 2)
        data = encode_apng(frames)
        meta += [('budget', 'exempt: a size-limit probe')]
        write_outputs(a['out'], a['prefix'], meta, [('noise.png', 'apng', data, w, h, 1)])
        return
    w, h, total = (int(v) for v in a.get('size', '320x240x24').split('x'))  # the self-test draws a small one
    frames = [pattern(w, h, i, total) for i in range(total)]
    meta += [('clip-frames', str(total)), ('clip-delay-ms', '125'), ('budget', str(BUDGET))]
    write_outputs(a['out'], a['prefix'], meta, [
        ('still.png', 'png', encode_png(pattern(w, h)), w, h, 1),
        ('clip.png', 'apng', encode_apng(frames, 125), w, h, 1),
        ('clip.gif', 'gif', encode_gif(frames, 125), w, h, 1)])


# ---- stage: what evidence-publish may upload -------------------------------
# Publish never uploads bytes it found. It reads the manifest and each image
# once, through a descriptor opened without following links, runs every check
# on the bytes it read, and writes those same bytes to the stage folder; the
# manifest it stages is regenerated from the parsed and validated fields, and
# the Markdown is built from the same fields. The file names are derived from
# the validated card, commit, renderer and scenario, never taken from the
# manifest. Every value must match the exact pattern the evidence step can
# write; anything else refuses the whole set. Checks per image: size within
# the budget, the SHA-256 the manifest records, and a full structural parse
# (PNG: signature, every chunk CRC, exactly the chunks the encoder writes,
# nothing after IEND, every frame decompresses to the stated size; GIF: the
# header, palette and loop block the encoder writes, every frame walked to
# the trailer with nothing after it). Refusals never print the value a check
# exists to hide.

SYNTH_CONTENT = 'synthetic: colour bars, a moving square, frame numbers; no game content'
NOISE_BUDGET = 'exempt: a size-limit probe'
_NAME = r'[A-Za-z0-9_][A-Za-z0-9_.-]{0,99}'
_LOWER = r'[a-z0-9_][a-z0-9_-]{0,63}'
_DEC = r'[0-9]+(?:\.[0-9]+)?'
_COUNT = r'[1-9][0-9]{0,5}'
NAME_ONLY_RE = re.compile(_NAME, re.ASCII)
MANIFEST_NAME_RE = re.compile(_NAME + r'-evidence\.txt', re.ASCII)
REPO_RE = re.compile(r'[A-Za-z0-9_.-]{1,100}/[A-Za-z0-9_.-]{1,100}', re.ASCII)
FRAME_RE = re.compile(_NAME + r'\.tga', re.ASCII)
SCORE_RE = re.compile(r'(%s\.tga) (%s|-) (%s|-) (ok|OVER|MISSING|EXTRA)' % (_NAME, _DEC, _DEC), re.ASCII)
SUMMARY_TOKEN = r'[a-z_]+=[0-9.]+'
FILE_RE = re.compile(r'file (%s) ([0-9]{1,10}) ([0-9a-f]{64}) (png|apng|gif) ([1-9][0-9]{0,4})x([1-9][0-9]{0,4}) 1/([1-8])' % _NAME, re.ASCII)
PIN_RE = r'(%s)/(%s)/(%s)' % (_LOWER, _LOWER, _NAME)
VALUE_RE = {k: re.compile(v, re.ASCII) for k, v in {
    'card': r'[A-Za-z0-9]{1,32}', 'commit': r'[0-9a-f]{12,40}|nogit',
    'command': r'pins\.sh evidence(?: [A-Za-z0-9_.-]{1,100}){1,20}', 'content': re.escape(SYNTH_CONTENT),
    'renderer': _LOWER, 'scenario': _LOWER, 'reference': PIN_RE, 'candidate': PIN_RE,
    'compare-exit': r'[0-9]{1,3}', 'ref-caption': 'REFERENCE ' + _LOWER, 'cand-caption': 'CANDIDATE ' + _LOWER,
    'frames': _COUNT, 'clip-frames': _COUNT, 'clip-delay-ms': _COUNT, 'diff-gain': _COUNT,
    'budget': r'[1-9][0-9]{0,9}|' + re.escape(NOISE_BUDGET), 'worst-frame': _NAME + r'\.tga',
    'summary': r'(?:%s(?: %s){0,15})?' % (SUMMARY_TOKEN, SUMMARY_TOKEN)}.items()}
# the fields of each kind of set, in the order the evidence step writes them
BUILD_KEYS = ['card', 'commit', 'command', 'renderer', 'scenario', 'reference', 'candidate', 'compare-exit',
              'ref-caption', 'cand-caption', 'frames', 'clip-frames', 'clip-delay-ms', 'diff-gain', 'budget',
              'worst-frame', 'summary']
PATTERN_KEYS = ['card', 'commit', 'command', 'content', 'clip-frames', 'clip-delay-ms', 'budget']
NOISE_KEYS = ['card', 'commit', 'command', 'content', 'budget']
# the options "pins.sh evidence" accepts, each with the pattern of its value
BUILD_OPTS = {'--card': r'[A-Za-z0-9]{1,32}', '--ref': _LOWER, '--ref-id': _NAME, '--cand-id': _NAME,
              '--run': r'[1-9][0-9]{0,2}', '--threshold': r'[0-9]+(?:\.[0-9]*)?|\.[0-9]+',
              '--bad-threshold': r'[0-9]+(?:\.[0-9]*)?|\.[0-9]+'}
SYNTH_OPTS = {'--card': r'[A-Za-z0-9]{1,32}', '--noise': r'[1-9][0-9]{0,9}'}


def refuse(msg):
    raise SystemExit('evidence-publish: refused: ' + msg)


def _command_options(words, opts):
    """{option: last value} of "--opt value" pairs; refuses anything else."""
    if len(words) % 2:
        refuse('the manifest command is not one evidence writes')
    got = {}
    for k, v in zip(words[0::2], words[1::2]):
        if k not in opts or not re.fullmatch(opts[k], v, re.ASCII):
            refuse('the manifest command is not one evidence writes')
        got[k] = v
    return got


def render_manifest(fields, scores, files):
    lines = [MANIFEST_MAGIC] + ['%s %s' % kv for kv in fields.items()]
    lines += ['score %s' % ' '.join(s) for s in scores]
    lines += ['file %s %d %s %s %dx%d 1/%d' % (f['name'], f['size'], f['sha256'], f['kind'], f['w'], f['h'], f['scale'])
              for f in files]
    return '\n'.join(lines) + '\n'


def parse_manifest(data):
    """The validated set a manifest describes: {fields, scores, files, prefix,
    expect}, where files are the manifest's file lines, prefix is the name
    stem derived from the validated fields and expect is [(name, kind,
    frames, delay_ms)] of the files that set has. Refuses any line, value or
    combination the evidence step cannot write; never prints a value."""
    try:
        text = data.decode('ascii')
    except UnicodeDecodeError:
        refuse('the manifest is not plain ASCII text')
    if not text.endswith('\n') or '\r' in text:
        refuse('the manifest does not have the line endings evidence writes')
    lines = text[:-1].split('\n')
    if lines[0] != MANIFEST_MAGIC:
        refuse('the manifest does not start with the line "%s"' % MANIFEST_MAGIC)
    fields, scores, files = {}, [], []
    for no, line in enumerate(lines[1:], 2):
        k, _, v = line.partition(' ')
        if k == 'file' and FILE_RE.fullmatch(line):
            m = FILE_RE.fullmatch(line)
            files.append({'name': m.group(1), 'size': int(m.group(2)), 'sha256': m.group(3), 'kind': m.group(4),
                          'w': int(m.group(5)), 'h': int(m.group(6)), 'scale': int(m.group(7))})
        elif k == 'score' and SCORE_RE.fullmatch(v):
            scores.append(list(SCORE_RE.fullmatch(v).groups()))
        elif k in VALUE_RE and k not in fields and VALUE_RE[k].fullmatch(v):
            fields[k] = v
        else:
            refuse('line %d of the manifest is not a line evidence writes' % no)
    words = fields.get('command', '').split(' ')[2:]
    keys = list(fields)
    if keys == BUILD_KEYS:
        renderer, scenario, card = fields['renderer'], fields['scenario'], fields['card']
        rs, rr, rid = re.fullmatch(PIN_RE, fields['reference']).groups()
        cs, cr, cid = re.fullmatch(PIN_RE, fields['candidate']).groups()
        opt = _command_options(words[2:], BUILD_OPTS)
        scored = [s[0] for s in scores if s[1] != '-']
        if (words[:2] != [renderer, scenario] or opt.get('--card', 'adhoc') != card or opt.get('--ref', 'gl1') != rr
                or opt.get('--ref-id', rid) != rid or opt.get('--cand-id', cid) != cid
                or (rs, cs, cr) != (scenario, scenario, renderer)
                or fields['ref-caption'] != 'REFERENCE ' + rr or fields['cand-caption'] != 'CANDIDATE ' + renderer
                or int(fields['frames']) != len(scores) or fields['worst-frame'] not in scored
                or int(fields['clip-frames']) > min(CLIP_MAX_FRAMES, len(scored))
                or fields['diff-gain'] != str(DIFF_GAIN) or fields['clip-delay-ms'] != str(CLIP_DELAY_MS)
                or fields['budget'] == NOISE_BUDGET):
            refuse('the manifest fields do not agree with each other as evidence writes them')
        prefix = '%s-%s-%s-%s' % (card, cid.split('-')[0], renderer, scenario)
        n = int(fields['clip-frames'])
        expect = [('still.png', 'png', 1, 0), ('clip.png', 'apng', n, CLIP_DELAY_MS), ('clip.gif', 'gif', n, CLIP_DELAY_MS)]
    elif keys in (PATTERN_KEYS, NOISE_KEYS) and words[:1] == ['--synthetic'] and not scores:
        opt = _command_options(words[1:], SYNTH_OPTS)
        noise = opt.get('--noise')
        if opt.get('--card', 'adhoc') != fields['card'] or (keys == NOISE_KEYS) != (noise is not None) \
                or (fields['budget'] == NOISE_BUDGET) != (noise is not None) \
                or (noise is None and fields['clip-delay-ms'] != '125'):
            refuse('the manifest fields do not agree with each other as evidence writes them')
        prefix = '%s-%s-synthetic-%s' % (fields['card'], fields['commit'], 'noise' + noise if noise else 'pattern')
        if noise:
            expect = [('noise.png', 'apng', max(1, round(int(noise) / (512 * 512 * 3))), CLIP_DELAY_MS)]
        else:
            n = int(fields['clip-frames'])
            expect = [('still.png', 'png', 1, 0), ('clip.png', 'apng', n, 125), ('clip.gif', 'gif', n, 125)]
    else:
        refuse('the manifest does not hold exactly the fields evidence writes')
    expect = [(prefix + '-' + s, kind, n, d) for s, kind, n, d in expect]
    if [(f['name'], f['kind']) for f in files] != [(e[0], e[1]) for e in expect]:
        refuse('the files the manifest lists are not the ones evidence writes for this set')
    if render_manifest(fields, scores, files) != text:
        refuse('the manifest is not in the form evidence writes')
    return {'fields': fields, 'scores': scores, 'files': files, 'prefix': prefix, 'expect': expect}


def _inflate(z, w, h, what):
    """Checks that the zlib stream z holds exactly h rows of w RGB pixels,
    each with a filter type the encoder uses (None, Sub, Up)."""
    n = h * (1 + 3 * w)
    d = zlib.decompressobj()
    raw = d.decompress(z, n + 1)
    if len(raw) != n or not d.eof or d.unused_data or d.unconsumed_tail:
        refuse('%s: a frame does not decompress to the stated size' % what)
    if not set(raw[0::1 + 3 * w]) <= {0, 1, 2}:
        refuse('%s: a row uses a filter the encoder does not write' % what)


def check_png(data, what, kind, w, h, frames, delay_ms):
    try:
        chunks = png_chunks(data)
    except (ValueError, struct.error):
        refuse('%s is not a whole PNG (signature, chunk lengths and CRCs, nothing after IEND)' % what)
    kinds = [k for k, _ in chunks]
    if kind == 'png':
        want = [b'IHDR', b'IDAT', b'IEND']
    else:
        want = [b'IHDR', b'acTL', b'fcTL', b'IDAT'] + [b'fcTL', b'fdAT'] * (frames - 1) + [b'IEND']
    if kinds != want:
        refuse('%s: its PNG chunks are not exactly the ones the encoder writes' % what)
    if chunks[0][1] != struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0):
        refuse('%s: its PNG header is not the stated size of 8-bit RGB' % what)
    if kind == 'apng' and chunks[1][1] != struct.pack('>II', frames, 0):
        refuse('%s: its animation control is not the one the encoder writes' % what)
    seq = 0
    for k, body in chunks[2:-1] if kind == 'apng' else chunks[1:-1]:
        if k == b'fcTL':
            if body != struct.pack('>IIIIIHHBB', seq, w, h, 0, 0, delay_ms, 1000, 0, 0):
                refuse('%s: a frame control is not the one the encoder writes' % what)
            seq += 1
        elif k == b'fdAT':
            if body[:4] != struct.pack('>I', seq):
                refuse('%s: a frame is out of sequence' % what)
            seq += 1
            _inflate(body[4:], w, h, what)
        else:
            _inflate(body, w, h, what)


def check_gif(data, what, w, h, frames, delay_ms):
    head = b'GIF89a' + struct.pack('<HHBBB', w, h, 0xF7, 0, 0) + GIF_PALETTE + b'\x21\xff\x0bNETSCAPE2.0\x03\x01\x00\x00\x00'
    if data[:len(head)] != head:
        refuse('%s: its GIF header, palette or loop block is not the one the encoder writes' % what)
    frame = b'\x21\xf9\x04\x04' + struct.pack('<H', max(2, delay_ms // 10)) + b'\x00\x00' + \
        b'\x2c' + struct.pack('<HHHHB', 0, 0, w, h, 0) + b'\x08'
    pos = len(head)
    for _ in range(frames):
        if data[pos:pos + len(frame)] != frame:
            refuse('%s: a GIF frame header is not the one the encoder writes' % what)
        pos += len(frame)
        z = bytearray()
        while pos < len(data) and data[pos]:
            z += data[pos + 1:pos + 1 + data[pos]]
            pos += 1 + data[pos]
        pos += 1
        try:
            lzw_decode(bytes(z), w * h, strict=True)
        except ValueError:
            refuse('%s: a GIF frame does not decode to the stated size' % what)
    if data[pos:] != b'\x3b':
        refuse('%s: the GIF does not end at its trailer after the stated frames' % what)


def _read(dfd, name, limit, what):
    """The bytes of the regular file name in the directory dfd, opened without
    following a link, at most limit bytes."""
    try:
        fd = os.open(name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=dfd)
    except OSError:
        refuse('%s cannot be opened as a file in the set folder (missing, unreadable or a symbolic link)' % what)
    try:
        if not stat.S_ISREG(os.fstat(fd).st_mode):
            refuse('%s is not a regular file' % what)
        with os.fdopen(fd, 'rb', closefd=False) as f:
            data = f.read(limit + 1)
    finally:
        os.close(fd)
    if len(data) > limit:
        refuse('%s is over the budget of %d bytes' % (what, limit))
    return data


def _write(dfd, name, data):
    fd = os.open(name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o644, dir_fd=dfd)
    with os.fdopen(fd, 'wb') as f:
        f.write(data)


def cmd_stage(a):
    mname = os.path.basename(a['manifest'])
    if not MANIFEST_NAME_RE.fullmatch(mname):
        refuse('the manifest name is not a bare evidence manifest name ([A-Za-z0-9_][A-Za-z0-9_.-]*-evidence.txt)')
    if not REPO_RE.fullmatch(a['repo']):
        refuse('the repository is not owner/name')
    root = os.path.realpath(a['root'])
    mdir = os.path.realpath(os.path.dirname(os.path.abspath(a['manifest'])))
    if os.path.dirname(mdir) != root:
        refuse('the manifest is outside the evidence folder (it must lie in a set folder directly inside it)')
    store = os.path.realpath(a['store'])
    if (mdir + os.sep).startswith(store + os.sep):
        refuse('the manifest is inside the pin store')
    dfd = os.open(mdir, os.O_RDONLY | os.O_DIRECTORY)
    try:
        s = parse_manifest(_read(dfd, mname, 1 << 20, 'the manifest'))
        text = render_manifest(s['fields'], s['scores'], s['files']).encode()
        # the file lines carry every asset name, so every name printed from here on is clean
        identity_check(text, 'the manifest')
        if mname != s['prefix'] + '-evidence.txt' or os.path.basename(mdir) != s['prefix']:
            refuse('the manifest and its folder are not named after the set it describes (%s)' % s['prefix'])
        listed = {f['name']: (f, e) for f, e in zip(s['files'], s['expect'])}
        chosen = []
        for p in a['files']:
            n = os.path.basename(p)
            if p.startswith('-') or not NAME_ONLY_RE.fullmatch(n) or n not in listed \
                    or os.path.realpath(p) != os.path.join(mdir, n):
                refuse('a file named after the manifest is not one of the files of this set in its folder')
            chosen.append(listed[n])
        chosen = chosen or list(listed.values())
        budget = BUDGET if s['fields']['budget'] == NOISE_BUDGET else min(BUDGET, int(s['fields']['budget']))
        staged = []
        for f, (n, kind, frames, delay) in chosen:
            data = _read(dfd, n, budget, n)
            if len(data) != f['size'] or hashlib.sha256(data).hexdigest() != f['sha256']:
                refuse('%s: its digest differs from the manifest' % n)
            if kind == 'gif':
                check_gif(data, n, f['w'], f['h'], frames, delay)
            else:
                check_png(data, n, kind, f['w'], f['h'], frames, delay)
            identity_check(data, n)
            staged.append((n, data))
    finally:
        os.close(dfd)
    md = markdown(s, [n for n, _ in staged], mname, 'https://github.com/%s/releases/download/pr-evidence/' % a['repo'])
    sfd = os.open(a['stage'], os.O_RDONLY | os.O_DIRECTORY)
    try:
        if os.listdir(sfd):
            refuse('the stage folder is not empty')
        for n, data in staged + [(mname, text)]:
            _write(sfd, n, data)
    finally:
        os.close(sfd)
    with open(a['markdown'], 'x') as f:
        f.write(md)


def markdown(s, names, mname, base):
    """The pull-request section, from the validated fields only."""
    meta = s['fields']
    url = {e[1]: base + e[0] for e in s['expect'] if e[0] in names}
    scen = meta.get('scenario', 'synthetic')
    if 'renderer' in meta:
        out = ['### Evidence: `%s` on `%s`' % (scen, meta['renderer']), '',
               'Reference `%s`, candidate `%s`%s.' % (meta['reference'], meta['candidate'],
                                                     '; `%s`' % meta['summary'] if meta['summary'] else ''), '']
    else:
        out = ['### Evidence: synthetic test pattern (no game content)', '']
    if 'png' in url and 'renderer' not in meta:
        out += ['![synthetic test pattern](%s)' % url['png'], '']
    elif 'png' in url:
        out += ['![%s: reference, candidate, difference x%s, frame %s (the worst score)](%s)'
                % (scen, meta['diff-gain'], meta['worst-frame'], url['png']), '']
    if 'apng' in url:
        count = meta.get('clip-frames', str(s['expect'][0][2]))
        out += ['![%s: clip, %s frames at %s ms each](%s)' % (scen, count, meta.get('clip-delay-ms', CLIP_DELAY_MS), url['apng']), '']
    if 'gif' in url:
        out += ['GIF of the same clip (256 fixed colours): [%s](%s)' % (url['gif'][len(base):], url['gif']), '']
    if s['scores']:
        out += ['<details><summary>Scores per frame</summary>', '', '| Frame | Score | Bad | Verdict |', '|---|---|---|---|']
        out += ['| `%s` | %s | %s | %s |' % tuple(r) for r in s['scores']]
        out += ['', '</details>', '']
    out.append('Manifest with the command, the commit, the pin ids and the SHA-256 of every file: [%s](%s)' % (mname, base + mname))
    return '\n'.join(out) + '\n'


# ---- self-test ---------------------------------------------------------------


def _fail(msg):
    raise SystemExit('evidence selftest: FAIL: ' + msg)


def cmd_selftest(a):
    import tempfile
    t = tempfile.mkdtemp(prefix='evidence.')
    try:
        _selftest(t)
    finally:
        shutil.rmtree(t)
    print('evidence selftest: ok')


def _selftest(t):
    # TGA in both orientations and depths reads back the same RGB
    img = noise(37, 23, 1)
    for up in (True, False):
        for bpp in (24, 32):
            p = os.path.join(t, 'a.tga')
            write_tga(p, img, up, bpp)
            if read_tga(p) != img:
                _fail('TGA round trip (bottom_up=%s, %d bit)' % (up, bpp))
    # only uncompressed 24/32-bit truecolor with no colour map is read
    good = bytes([0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 24, 0x20]) + b'\x00\x00\xff'
    for i, v in ((2, 10), (1, 1), (16, 16), (12, 0), (14, 0)):
        with open(p, 'wb') as f:
            f.write(good[:i] + bytes([v]) + good[i + 1:])
        try:
            read_tga(p)
            _fail('a TGA with byte %d = %d must be refused' % (i, v))
        except ValueError:
            pass
    # PNG: every filter path round-trips through the independent reader; CRCs and lengths hold
    flat = blank(64, 16, (9, 200, 77))
    pat = pattern(160, 120, 3, 10)
    for im in (img, flat, pat, noise(1, 1, 2)):
        data = encode_png(im)
        if decode_png(data) != [im]:
            _fail('PNG round trip %dx%d' % im[:2])
    bad = bytearray(encode_png(img))
    bad[40] ^= 1
    try:
        png_chunks(bytes(bad))
        _fail('a corrupted PNG chunk must fail its CRC')
    except ValueError:
        pass
    # every filter type the encoder may choose decodes the same
    for ftype in (0, 1, 2):
        w, h, px = pat
        n = w * 3
        rows = [bytes(px[y * n:(y + 1) * n]) for y in range(h)]
        H, L, zero = int.from_bytes(b'\x80' * n, 'big'), int.from_bytes(b'\x7f' * n, 'big'), bytes(n)
        if ftype == 0:
            body = b''.join(b'\x00' + r for r in rows)
        elif ftype == 1:
            body = b''.join(b'\x01' + _bytesub(r, b'\x00\x00\x00' + r[:-3], H, L, n) for r in rows)
        else:
            body = b''.join(b'\x02' + _bytesub(r, p, H, L, n) for r, p in zip(rows, [zero] + rows[:-1]))
        if _unfilter(body, w, h) != px:
            _fail('PNG filter %d' % ftype)
    # APNG: frame count, sequence numbers, every frame back
    frames = [pattern(96, 64, i, 5) for i in range(5)]
    data = encode_apng(frames, 100)
    kinds = [k for k, _ in png_chunks(data)]
    if kinds.count(b'fcTL') != 5 or kinds.count(b'fdAT') != 4 or kinds.index(b'acTL') > kinds.index(b'IDAT'):
        _fail('APNG chunk layout %r' % kinds)
    if decode_png(data) != frames:
        _fail('APNG round trip')
    # GIF: LZW decodes back, including a stream long and noisy enough to fill the table
    for idx in (bytes(1), bytes(range(256)) * 3, quantize(noise(300, 200, 3)), bytes(5000)):
        if lzw_decode(lzw_encode(idx), len(idx), strict=True) != idx:
            _fail('LZW round trip of %d pixels' % len(idx))
    # strict: a set padding bit after the end code, or a byte after it, is refused
    padded = 0
    for idx in (bytes(1), bytes(range(256)) * 3, bytes(5000)):
        z = lzw_encode(idx)
        for z2 in [z[:-1] + bytes([z[-1] | (1 << b)]) for b in range(8)] + [z + b'\x00']:
            try:
                same = z2 != z and lzw_decode(z2, len(idx)) == idx
            except ValueError:
                same = False
            if same:
                padded += 1
                try:
                    lzw_decode(z2, len(idx), strict=True)
                    _fail('strict LZW must refuse anything after the end code')
                except ValueError:
                    pass
    if padded < 2:
        _fail('the strict LZW check met no padding to test')
    gw, gh, gframes = decode_gif(encode_gif(frames))
    if (gw, gh) != (96, 64) or gframes != [quantize(f) for f in frames]:
        _fail('GIF round trip')
    if len(GIF_PALETTE) != 768 or quantize(blank(1, 1, (255, 255, 255))) != b'\xff' or quantize(blank(1, 1)) != b'\x00':
        _fail('GIF palette ends')
    # image helpers
    if downscale(pat, 3)[:2] != (53, 40) or downscale(pat, 2)[2][:3] != pat[2][:3]:
        _fail('downscale')
    d = absdiff(blank(2, 1, (10, 10, 10)), blank(2, 1, (12, 10, 100)))
    if d[2] != bytearray([16, 0, 255] * 2):
        _fail('absdiff gain and clamp: %r' % d[2])
    for ch, rows in FONT.items():
        if len(rows) != 14 or any(int(rows[i:i + 2], 16) > 0x1F for i in range(0, 14, 2)):
            _fail('font glyph %r' % ch)
    # budget: a reduction is chosen when the full size does not fit
    big = noise(200, 200, 4)
    data, f = fit(lambda k: encode_png(downscale(big, k)), 1, limit=40000)
    if f != 2 or len(data) > 40000:
        _fail('fit chose 1/%d for %d bytes' % (f, len(data)))
    # build end to end over two pins whose frames are literal engine bytes, as
    # VID_WriteTGA (src/client/vid/vid.c) writes them: an 18-byte header with
    # type 2, 24 bits and descriptor 0x20 (rows top-down), then B, G, R per
    # pixel. Frame 0 differs; known pixels are read back from each panel.
    W, H = 40, 30

    def engine_tga(path, px):
        hdr = bytes([0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, W & 255, W >> 8, H & 255, H >> 8, 24, 0x20])
        with open(path, 'wb') as f:
            f.write(hdr + b''.join(bytes((px(x, y)[2], px(x, y)[1], px(x, y)[0])) for y in range(H) for x in range(W)))

    def ref_px(x, y):  # red at the top left, blue in the 2x2 bottom-left corner
        return (255, 0, 0) if (x, y) == (0, 0) else (0, 0, 255) if x < 2 and y >= H - 2 else (10, 20, 30)

    def cand_px(x, y):
        return (0, 255, 0) if (x, y) == (0, 0) else (0, 0, 255) if x < 2 and y >= H - 2 else (12, 20, 29)

    for d in ('ref', 'cand', 'out'):
        os.makedirs(os.path.join(t, d))
    engine_tga(os.path.join(t, 'ref', 'world-000000.tga'), ref_px)
    engine_tga(os.path.join(t, 'cand', 'world-000000.tga'), cand_px)
    for d in ('ref', 'cand'):
        engine_tga(os.path.join(t, d, 'world-000001.tga'), ref_px)
    cmp_path = os.path.join(t, 'compare.txt')
    with open(cmp_path, 'w') as f:
        f.write('frame score bad verdict\nworld-000000.tga 0.500000 0.100000 OVER\nworld-000001.tga 0.000000 0.000000 ok\n'
                'ref=%s\ncandidate=%s\nframes=2 failed=1 worst=0.5 worst_bad=0.1 threshold=0.02 bad_threshold=0.005\n' % (t, t))
    # build keeps only score rows publish accepts
    odd = os.path.join(t, 'odd.txt')
    with open(odd, 'w') as f:
        f.write('world-000000.tga 0.5 0.1 OVER\nworld-000001.tga 1e-5 - ok\nworld-00000`.tga 1 1 ok\nworld-000002.tga 1 1 maybe\n')
    if [r[0] for r in parse_compare(odd)[0]] != ['world-000000.tga']:
        _fail('parse_compare keeps a row publish refuses: %r' % (parse_compare(odd)[0],))
    old = sys.stdout
    sys.stdout = open(os.devnull, 'w')
    try:
        cmd_build({'ref-dir': os.path.join(t, 'ref'), 'cand-dir': os.path.join(t, 'cand'), 'compare': cmp_path,
                   'out': os.path.join(t, 'out'), 'prefix': 'X1-abc-soft-world',
                   'meta': ['card=X1', 'commit=nogit', 'command=pins.sh evidence soft world --cand-id abc-1 --card X1',
                            'renderer=soft', 'scenario=world', 'reference=world/gl1/r1', 'candidate=world/soft/abc-1',
                            'compare-exit=1', 'ref-caption=REFERENCE gl1', 'cand-caption=CANDIDATE soft']})
    finally:
        sys.stdout.close()
        sys.stdout = old
    mpath = os.path.join(t, 'out', 'X1-abc-soft-world-evidence.txt')
    with open(mpath, 'rb') as f:
        mdata = f.read()
    if t.encode() in mdata:
        _fail('the manifest holds the scratch path')
    st = parse_manifest(mdata)  # what build writes, publish accepts
    if st['prefix'] != 'X1-abc-soft-world' or st['fields']['worst-frame'] != 'world-000000.tga' or len(st['scores']) != 2:
        _fail('build manifest: %r' % st)
    blobs = {}
    for f, (n, kind, frames, delay) in zip(st['files'], st['expect']):
        with open(os.path.join(t, 'out', n), 'rb') as fp:
            blobs[kind] = fp.read()
        if hashlib.sha256(blobs[kind]).hexdigest() != f['sha256'] or len(blobs[kind]) != f['size']:
            _fail('manifest digest of %s' % n)
        if kind == 'gif':
            check_gif(blobs[kind], n, f['w'], f['h'], frames, delay)
        else:
            check_png(blobs[kind], n, kind, f['w'], f['h'], frames, delay)

    def at(img, x, y):
        o = (y * img[0] + x) * 3
        return tuple(img[2][o:o + 3])

    still = decode_png(blobs['png'])[0]
    bar = FONT_H + 6
    if still[:2] != (3 * W + 8, H + bar):
        _fail('still size %dx%d' % still[:2])
    want = {(0, 0): (255, 0, 0), (0, H - 1): (0, 0, 255), (5, 9): (10, 20, 30),                    # reference
            (W + 4, 0): (0, 255, 0), (W + 4, H - 1): (0, 0, 255), (W + 9, 9): (12, 20, 29),        # candidate
            (2 * W + 8, 0): (255, 255, 0), (2 * W + 8, H - 1): (0, 0, 0), (2 * W + 13, 9): (16, 0, 8)}  # |r - c| * 8
    for (x, y), rgb in want.items():
        if at(still, x, y + bar) != rgb:
            _fail('still panel pixel (%d, %d) is %r, expected %r' % (x, y, at(still, x, y + bar), rgb))
    clip = decode_png(blobs['apng'])
    _, _, gifs = decode_gif(blobs['gif'])
    if len(clip) != 2 or len(gifs) != 2:
        _fail('clip frames %d, GIF frames %d' % (len(clip), len(gifs)))
    hw = W // 2  # the clip is at half size
    for (x, y), rgb in {(0, 0): (255, 0, 0), (0, H // 2 - 1): (0, 0, 255), (hw + 4, 0): (0, 255, 0),
                        (2 * hw + 8, 0): (255, 255, 0), (2 * hw + 13, 5): (16, 0, 8)}.items():
        if at(clip[0], x, y + bar) != rgb:
            _fail('clip panel pixel (%d, %d) is %r, expected %r' % (x, y, at(clip[0], x, y + bar), rgb))
        if gifs[0][(y + bar) * clip[0][0] + x] != quantize(blank(1, 1, rgb))[0]:
            _fail('GIF panel pixel (%d, %d)' % (x, y))


# the options of each command; only --meta may be given more than once, and
# only stage takes file names, after "--"
OPTIONS = {'build': {'ref-dir', 'cand-dir', 'compare', 'out', 'prefix', 'meta'},
           'synth': {'out', 'prefix', 'noise', 'size', 'meta'},
           'stage': {'root', 'store', 'manifest', 'stage', 'repo', 'markdown'},
           'selftest': set()}
REQUIRED = {'build': {'ref-dir', 'cand-dir', 'compare', 'out', 'prefix'}, 'synth': {'out', 'prefix'},
            'stage': OPTIONS['stage'], 'selftest': set()}


def parse_args(argv):
    if not argv or argv[0] not in OPTIONS:
        raise SystemExit('usage: evidence.py build|synth|stage|selftest [--option value]... [-- file...]')
    cmd, rest = argv[0], argv[1:]
    a = {'meta': [], 'files': []}
    i = 0
    while i < len(rest):
        if rest[i] == '--' and cmd == 'stage':
            a['files'] = rest[i + 1:]
            break
        k = rest[i][2:]
        if not rest[i].startswith('--') or k not in OPTIONS[cmd]:
            raise SystemExit('evidence: %s does not take that argument' % cmd)
        if i + 1 >= len(rest):
            raise SystemExit('evidence: --%s needs a value' % k)
        if k == 'meta':
            a['meta'].append(rest[i + 1])
        elif k in a:
            raise SystemExit('evidence: --%s is given twice' % k)
        else:
            a[k] = rest[i + 1]
        i += 2
    if not REQUIRED[cmd] <= set(a):
        raise SystemExit('evidence: %s needs --%s' % (cmd, ' --'.join(sorted(REQUIRED[cmd] - set(a)))))
    return cmd, a


def main(argv):
    cmd, a = parse_args(argv)
    fn = {'build': cmd_build, 'synth': cmd_synth, 'stage': cmd_stage, 'selftest': cmd_selftest}[cmd]
    if cmd != 'stage':
        fn(a)
        return
    try:
        fn(a)
    except SystemExit:
        raise
    except BaseException as e:  # fail closed, and print no message that could hold a path
        raise SystemExit('evidence-publish: refused: unexpected %s; nothing was staged' % type(e).__name__)


if __name__ == '__main__':
    main(sys.argv[1:])
