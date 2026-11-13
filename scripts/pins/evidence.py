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
#   evidence.py synth --out D --prefix P [--noise BYTES] [--meta k=v]...
#   evidence.py stage --root D --manifest M --stage D [file]...
#   evidence.py markdown --manifest M --base-url URL
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

BUDGET = int(os.environ.get('PINS_EVIDENCE_BUDGET', '4000000'))  # bytes per file
DIFF_GAIN = 8          # the difference panel shows |ref - cand| * 8, clamped
CLIP_DELAY_MS = 500    # each clip frame is shown for half a second
CLIP_MAX_FRAMES = 48   # a longer pin is subsampled evenly to this many frames
MANIFEST_MAGIC = 'pins-evidence 1'
NAME_RE = re.compile(r'^[A-Za-z0-9_][A-Za-z0-9_.-]*$')

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


def lzw_decode(data, npix):
    """The inverse of lzw_encode, for the self-test and for reading a GIF back."""
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


def read_manifest(path):
    with open(path) as f:
        lines = f.read().split('\n')
    if lines[0] != MANIFEST_MAGIC:
        raise ValueError('not an evidence manifest')
    meta, scores, files = {}, [], []
    for line in lines[1:]:
        if not line:
            continue
        k, _, v = line.partition(' ')
        if k == 'file':
            parts = v.split(' ')
            if len(parts) != 6:
                raise ValueError('bad file line: %s' % line)
            files.append({'name': parts[0], 'size': int(parts[1]), 'sha256': parts[2],
                          'kind': parts[3], 'dims': parts[4], 'scale': parts[5]})
        elif k == 'score':
            scores.append(v.split(' '))
        else:
            meta[k] = v
    return meta, scores, files


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


def identity_check(data, what):
    for s in identity_strings():
        if s.encode() in data:
            raise SystemExit('evidence: refused: %s holds a host name, user name or home path' % what)
    for s in (b'/Users/', b'/home/', b'C:\\Users'):
        if s in data:
            raise SystemExit('evidence: refused: %s holds an absolute home path' % what)


# ---- build -------------------------------------------------------------------


def parse_compare(path):
    """[(frame, score, bad, verdict)] and the summary line of pins.sh compare."""
    rows, summary = [], ''
    with open(path) as f:
        for line in f:
            p = line.split()
            if len(p) == 4 and p[0].endswith('.tga') and NAME_RE.match(p[0]):
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
    ref_cap, cand_cap = a.get('ref-caption', 'REFERENCE'), a.get('cand-caption', 'CANDIDATE')
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
    w, h, total = 320, 240, 24
    frames = [pattern(w, h, i, total) for i in range(total)]
    meta += [('clip-frames', str(total)), ('clip-delay-ms', '125'), ('budget', str(BUDGET))]
    write_outputs(a['out'], a['prefix'], meta, [
        ('still.png', 'png', encode_png(pattern(w, h)), w, h, 1),
        ('clip.png', 'apng', encode_apng(frames, 125), w, h, 1),
        ('clip.gif', 'gif', encode_gif(frames, 125), w, h, 1)])


# ---- stage: the provenance and type checks of evidence-publish -------------
# Every file to upload is listed in a manifest the evidence step wrote, lies
# directly in the manifest's folder inside the evidence root, is a PNG or a
# GIF by name and by content (or the manifest itself), and matches the size
# and SHA-256 the manifest records. Data files of the game and of the pin
# store are refused by name and by content before anything else is looked at.
# Accepted files are copied to the stage folder and hashed again there; only
# the copies are uploaded.

def refuse(msg):
    raise SystemExit('evidence-publish: refused: ' + msg)


def data_file_reason(name, head):
    """Why name (with its first bytes head) is a game or pin-store data file, or None."""
    low = name.lower()
    if low.endswith('.pak') or head[:4] == b'PACK':
        return 'a game data archive (.pak)'
    if low.endswith(('.pk3', '.zip')) or head[:4] == b'PK\x03\x04':
        return 'a game data archive (.pk3/.zip)'
    if low.endswith('.dm2'):
        return 'a demo (.dm2)'
    if low.endswith('.cin'):
        return 'a cinematic (.cin)'
    if low.endswith('.q2r') or 'trace' in low or head[:8] == b'Q2REFREC':
        return 'a renderer trace (.q2r)'
    if low == 'manifest.txt' or re.match(rb'^[A-Za-z0-9_.-]+\.tga [a-z0-9_-]+ [0-9]+x[0-9]+\n', head):
        return 'a pin-store manifest'
    if low.endswith('.tga') or low in ('complete', 'incomplete', 'engine.log', 'timedemo.txt'):
        return 'a file of the pin store'
    return None


def physical(p):
    return os.path.realpath(p)


def cmd_stage(a):
    root = physical(a['root'])
    mpath = a['manifest']
    if os.path.islink(mpath) or not os.path.isfile(mpath):
        refuse('the manifest %s is not a regular file' % os.path.basename(mpath))
    mdir = physical(os.path.dirname(os.path.abspath(mpath)))
    if not (mdir + os.sep).startswith(root + os.sep) or mdir == root:
        refuse('the manifest is outside the evidence folder')
    store = a.get('store')
    if store and (mdir + os.sep).startswith(physical(store) + os.sep):
        refuse('the manifest is inside the pin store')
    mname = os.path.basename(mpath)
    with open(mpath, 'rb') as f:
        mhead = f.read(64)
    why = data_file_reason(mname, mhead)
    if why:
        refuse('%s is %s' % (mname, why))
    if not mname.endswith('-evidence.txt'):
        refuse('%s is not an evidence manifest (*-evidence.txt)' % mname)
    try:
        meta, scores, files = read_manifest(mpath)
    except (ValueError, UnicodeDecodeError) as e:
        refuse('%s: %s' % (mname, e))
    listed = {f['name']: f for f in files}
    if a['files']:
        chosen = []
        for p in a['files']:
            n = os.path.basename(p)
            if n not in listed or physical(p) != os.path.join(mdir, n):
                refuse('%s is not in the manifest' % n)
            chosen.append(listed[n])
    else:
        chosen = files
    os.makedirs(a['stage'], exist_ok=True)
    with open(mpath, 'rb') as f:
        mdata = f.read()
    identity_check(mdata, mname)
    for ent in chosen:
        n = ent['name']
        path = os.path.join(mdir, n)
        head = b''
        if os.path.isfile(path):
            with open(path, 'rb') as f:
                head = f.read(64)
        why = data_file_reason(n, head)
        if why:
            refuse('%s is %s' % (n, why))
        if not NAME_RE.match(n) or os.sep in n:
            refuse('%s is outside the evidence folder (not a bare file name)' % n)
        st = os.lstat(path) if os.path.lexists(path) else None
        if st is None:
            refuse('%s, listed in the manifest, does not exist' % n)
        if stat.S_ISLNK(st.st_mode) or os.path.dirname(physical(path)) != mdir:
            refuse('%s is outside the evidence folder (a symbolic link)' % n)
        if not stat.S_ISREG(st.st_mode):
            refuse('%s is not a regular file' % n)
        if n.endswith('.png') and head[:8] == PNG_SIG and ent['kind'] in ('png', 'apng'):
            pass
        elif n.endswith('.gif') and head[:6] == b'GIF89a' and ent['kind'] == 'gif':
            pass
        else:
            refuse('%s is not a PNG or GIF image written by the evidence step' % n)
        dst = os.path.join(a['stage'], n)
        shutil.copyfile(path, dst)
        if os.path.getsize(dst) != ent['size'] or sha256(dst) != ent['sha256']:
            refuse('%s: its digest differs from the manifest' % n)
        with open(dst, 'rb') as f:
            identity_check(f.read(), n)
        print(dst)
    dst = os.path.join(a['stage'], mname)
    with open(dst, 'wb') as f:
        f.write(mdata)
    print(dst)


def cmd_markdown(a):
    meta, scores, files = read_manifest(a['manifest'])
    base = a['base-url'].rstrip('/') + '/'
    url = {f['kind']: base + f['name'] for f in files}
    mname = os.path.basename(a['manifest'])
    scen, rend = meta.get('scenario', 'synthetic'), meta.get('renderer', '-')
    out = ['### Evidence: `%s` on `%s`' % (scen, rend), '']
    if 'reference' in meta:
        out.append('Reference `%s`, candidate `%s`; %s.' % (meta['reference'], meta['candidate'], meta.get('summary', '')))
        out.append('')
    if 'png' in url:
        cap = 'reference, candidate, difference x%s' % meta.get('diff-gain', DIFF_GAIN)
        if 'worst-frame' in meta:
            cap += ', frame %s (the worst score)' % meta['worst-frame']
        out += ['![%s: %s](%s)' % (scen, cap, url['png']), '']
    if 'apng' in url:
        out += ['![%s: clip, %s frames at %s ms each](%s)' % (scen, meta.get('clip-frames', '?'), meta.get('clip-delay-ms', '?'), url['apng']), '']
    if 'gif' in url:
        out += ['GIF of the same clip (256 fixed colours): [%s](%s)' % (os.path.basename(url['gif']), url['gif']), '']
    if scores:
        out += ['<details><summary>Scores per frame</summary>', '', '| Frame | Score | Bad | Verdict |', '|---|---|---|---|']
        out += ['| %s |' % ' | '.join(s) for s in scores]
        out += ['', '</details>', '']
    out.append('Manifest with the command, the commit, the pin ids and the SHA-256 of every file: [%s](%s)' % (mname, base + mname))
    print('\n'.join(out))


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
        if lzw_decode(lzw_encode(idx), len(idx)) != idx:
            _fail('LZW round trip of %d pixels' % len(idx))
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
    # build end to end over two synthetic pins; the manifest names no local path
    for d in ('ref', 'cand', 'out'):
        os.makedirs(os.path.join(t, d))
    for i in range(3):
        write_tga(os.path.join(t, 'ref', 'world-%06d.tga' % i), pattern(120, 90, i, 3))
        write_tga(os.path.join(t, 'cand', 'world-%06d.tga' % i), pattern(120, 90, i if i != 1 else 2, 3))
    cmp_path = os.path.join(t, 'compare.txt')
    with open(cmp_path, 'w') as f:
        f.write('frame score bad verdict\nworld-000000.tga 0.000000 0.000000 ok\n'
                'world-000001.tga 0.050000 0.100000 OVER\nworld-000002.tga 0.000000 0.000000 ok\n'
                'ref=%s\ncandidate=%s\nframes=3 failed=1 worst=0.05 worst_bad=0.1 threshold=0.02 bad_threshold=0.005\n' % (t, t))
    old = sys.stdout
    sys.stdout = open(os.devnull, 'w')
    try:
        cmd_build({'ref-dir': os.path.join(t, 'ref'), 'cand-dir': os.path.join(t, 'cand'), 'compare': cmp_path,
                   'out': os.path.join(t, 'out'), 'prefix': 'X1-abc-soft-world', 'meta': ['card=X1']})
    finally:
        sys.stdout.close()
        sys.stdout = old
    mpath = os.path.join(t, 'out', 'X1-abc-soft-world-evidence.txt')
    meta, scores, files = read_manifest(mpath)
    if meta.get('worst-frame') != 'world-000001.tga' or len(scores) != 3 or [f['kind'] for f in files] != ['png', 'apng', 'gif']:
        _fail('build manifest: %r %r' % (meta, files))
    with open(mpath, 'rb') as f:
        if t.encode() in f.read():
            _fail('the manifest holds the scratch path')
    for ent in files:
        p = os.path.join(t, 'out', ent['name'])
        if sha256(p) != ent['sha256'] or os.path.getsize(p) != ent['size']:
            _fail('manifest digest of %s' % ent['name'])
    still = decode_png(open(os.path.join(t, 'out', files[0]['name']), 'rb').read())[0]
    if still[:2] != (3 * 120 + 8, 90 + 13):
        _fail('still size %dx%d' % still[:2])
    clip = decode_png(open(os.path.join(t, 'out', files[1]['name']), 'rb').read())
    if len(clip) != 3:
        _fail('clip frames %d' % len(clip))


def main(argv):
    if not argv:
        raise SystemExit(__doc__ if __doc__ else 'usage: evidence.py build|synth|stage|markdown|selftest')
    cmd, rest = argv[0], argv[1:]
    a = {'meta': [], 'files': []}
    i = 0
    while i < len(rest):
        if rest[i].startswith('--'):
            if i + 1 >= len(rest):
                raise SystemExit('evidence: %s needs a value' % rest[i])
            k = rest[i][2:]
            if k == 'meta':
                a['meta'].append(rest[i + 1])
            else:
                a[k] = rest[i + 1]
            i += 2
        else:
            a['files'].append(rest[i])
            i += 1
    fn = {'build': cmd_build, 'synth': cmd_synth, 'stage': cmd_stage,
          'markdown': cmd_markdown, 'selftest': cmd_selftest}.get(cmd)
    if fn is None:
        raise SystemExit('evidence: unknown command %s' % cmd)
    fn(a)


if __name__ == '__main__':
    main(sys.argv[1:])
