#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
build_assets.py - Convert pre-boot artwork and fonts into RGB565 / C headers.

Reads:
    assets/BgTile.png           - splash background tile (small PNG, drawn at 2x)
    assets/happymacIconNew.png  - centered splash icon (RGBA PNG with transparency)
    assets/fonts/charcoal12.bdf - Charcoal 12 bitmap font for the preboot UI

Writes:
    src/generated/asset_bg_tile.h
    src/generated/asset_happy_mac.h
    src/generated/charcoal_font_data.h

Only the Python standard library is used, so every interpreter that runs the
PlatformIO build produces the same headers. A missing PNG falls back to a
built-in classic default with a warning; a PNG or BDF that is present but
cannot be decoded, or a missing font, stops the build instead of shipping
placeholder art.

Also runnable standalone for CI / manual invocation:
    python3 scripts/build_assets.py [--force]

Designed to also run as a PlatformIO `pre:` script.
"""

import os
import sys

# ---------------------------------------------------------------------------
# Paths
#
# When invoked by PlatformIO via SConscript, `__file__` isn't defined in the
# exec'd globals, so we probe a few sources to find the project root. When
# run standalone (python3 scripts/build_assets.py), `__file__` works fine.
# ---------------------------------------------------------------------------

# Pick up the PIO env as early as possible so we can read PROJECT_DIR from it.
_PIO_ENV = None
try:
    Import("env")          # type: ignore[name-defined] # noqa: F821
    _PIO_ENV = env          # type: ignore[name-defined] # noqa: F821
except Exception:
    _PIO_ENV = None


def _resolve_project_root():
    if _PIO_ENV is not None:
        try:
            p = _PIO_ENV["PROJECT_DIR"]
            if p:
                return p
        except Exception:
            pass
    try:
        return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    except NameError:
        return os.getcwd()


_PROJECT_ROOT = _resolve_project_root()
_SCRIPTS_DIR = os.path.join(_PROJECT_ROOT, "scripts")

ASSETS_DIR   = os.path.join(_PROJECT_ROOT, "assets")
FONTS_DIR    = os.path.join(ASSETS_DIR, "fonts")
GEN_DIR      = os.path.join(_PROJECT_ROOT, "src", "generated")

def _first_existing(directory, basenames):
    """Return the first path in `directory` whose basename matches any entry
    of `basenames` case-insensitively. Returns the canonical (first) path
    even if the file does not exist, so callers can still print it as the
    expected location in warnings.
    """
    if not basenames:
        return ""
    try:
        present = os.listdir(directory)
    except OSError:
        present = []
    lower_map = {p.lower(): p for p in present}
    for want in basenames:
        hit = lower_map.get(want.lower())
        if hit:
            return os.path.join(directory, hit)
    return os.path.join(directory, basenames[0])


SRC_BG_TILE   = _first_existing(ASSETS_DIR, ["BgTile.png", "bgtile.png"])
SRC_HAPPY_MAC = _first_existing(ASSETS_DIR, ["happymacIconNew.png", "happymacicon.png",
                                              "happymacIcon.png",
                                              "HappyMacIcon.png", "happyMacIcon.png"])
SRC_FONT_BDF  = _first_existing(FONTS_DIR,  ["charcoal12.bdf"])

OUT_BG_TILE   = os.path.join(GEN_DIR, "asset_bg_tile.h")
OUT_HAPPY_MAC = os.path.join(GEN_DIR, "asset_happy_mac.h")
OUT_FONT      = os.path.join(GEN_DIR, "charcoal_font_data.h")


class AssetError(RuntimeError):
    pass


# ---------------------------------------------------------------------------
# Colour helpers
# ---------------------------------------------------------------------------

def rgb888_to_rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3)


# ---------------------------------------------------------------------------
# Emit helpers
# ---------------------------------------------------------------------------

def _ensure_gen_dir():
    os.makedirs(GEN_DIR, exist_ok=True)


def _write_if_changed(path, contents):
    old = None
    try:
        with open(path, "r", encoding="utf-8") as f:
            old = f.read()
    except OSError:
        pass
    if old == contents:
        return False
    with open(path, "w", encoding="utf-8") as f:
        f.write(contents)
    return True


def _report(out_path, written, detail):
    print("[build_assets] %s %s (%s)" % ("wrote" if written else "unchanged",
                                        os.path.relpath(out_path, _PROJECT_ROOT), detail))


def _format_u16_array(values, per_line=16):
    out_lines = []
    for i in range(0, len(values), per_line):
        chunk = values[i:i + per_line]
        out_lines.append("    " + ", ".join("0x%04X" % v for v in chunk) + ",")
    return "\n".join(out_lines)


def _format_u8_array(values, per_line=16):
    out_lines = []
    for i in range(0, len(values), per_line):
        chunk = values[i:i + per_line]
        out_lines.append("    " + ", ".join("0x%02X" % v for v in chunk) + ",")
    return "\n".join(out_lines)


def _preamble(source_label):
    return (
        "// -----------------------------------------------------------------\n"
        "// AUTO-GENERATED by scripts/build_assets.py - DO NOT EDIT BY HAND.\n"
        "// Source: %s\n"
        "// -----------------------------------------------------------------\n"
        "#pragma once\n"
        "\n"
        "#include <stdint.h>\n"
        "\n"
    ) % source_label


# ---------------------------------------------------------------------------
# Default (fallback) artwork
# ---------------------------------------------------------------------------

# Classic Mac "50% gray" desktop pattern: alternating pixels. Looks gray at
# native 1x and gives the canonical stippled texture when tiled.
def _default_bg_tile():
    w = 16
    h = 16
    pixels = []
    for y in range(h):
        for x in range(w):
            on = (x + y) & 1
            if on:
                pixels.append(rgb888_to_rgb565(0x88, 0x88, 0x88))
            else:
                pixels.append(rgb888_to_rgb565(0xC0, 0xC0, 0xC0))
    return w, h, pixels


# Classic 32x32 Happy Mac (1-bit). Mirrors the bitmap previously embedded in
# boot_gui.cpp so the out-of-the-box boot still shows a recognizable icon.
_DEFAULT_HAPPY_MAC_1BIT = [
    0x00, 0x0F, 0xF0, 0x00,
    0x00, 0x3F, 0xFC, 0x00,
    0x00, 0x7F, 0xFE, 0x00,
    0x00, 0xFF, 0xFF, 0x00,
    0x01, 0xFF, 0xFF, 0x80,
    0x03, 0xFF, 0xFF, 0xC0,
    0x07, 0xE0, 0x07, 0xE0,
    0x07, 0xC0, 0x03, 0xE0,
    0x0F, 0x9E, 0x79, 0xF0,
    0x0F, 0x9E, 0x79, 0xF0,
    0x0F, 0x80, 0x01, 0xF0,
    0x0F, 0x80, 0x01, 0xF0,
    0x0F, 0x8C, 0x31, 0xF0,
    0x0F, 0x87, 0xE1, 0xF0,
    0x07, 0xC0, 0x03, 0xE0,
    0x07, 0xE0, 0x07, 0xE0,
    0x03, 0xFF, 0xFF, 0xC0,
    0x01, 0xFF, 0xFF, 0x80,
    0x00, 0xFF, 0xFF, 0x00,
    0x00, 0x7F, 0xFE, 0x00,
    0x00, 0x3F, 0xFC, 0x00,
    0x00, 0x0F, 0xF0, 0x00,
    0x00, 0x07, 0xE0, 0x00,
    0x00, 0x1F, 0xF8, 0x00,
    0x00, 0x3F, 0xFC, 0x00,
    0x00, 0x7F, 0xFE, 0x00,
    0x00, 0x7F, 0xFE, 0x00,
    0x00, 0x7F, 0xFE, 0x00,
    0x00, 0x7F, 0xFE, 0x00,
    0x00, 0x3F, 0xFC, 0x00,
    0x00, 0x1F, 0xF8, 0x00,
    0x00, 0x07, 0xE0, 0x00,
]


def _default_happy_mac():
    w, h = 32, 32
    pixels = [0] * (w * h)
    mask_bits = [0] * (w * h)
    white = rgb888_to_rgb565(0xFF, 0xFF, 0xFF)
    for y in range(h):
        for bx in range(4):
            byte = _DEFAULT_HAPPY_MAC_1BIT[y * 4 + bx]
            for bit in range(8):
                x = bx * 8 + bit
                if byte & (1 << (7 - bit)):
                    pixels[y * w + x] = white
                    mask_bits[y * w + x] = 1
    return w, h, pixels, mask_bits


# ---------------------------------------------------------------------------
# PNG decoding (standard library only)
# ---------------------------------------------------------------------------

def _decode_png_rgba(path):
    """Decode a non-interlaced 8-bit PNG (gray, RGB, palette, gray+alpha or
    RGBA) into (w, h, [(r, g, b, a), ...]). Raises AssetError otherwise."""
    import struct
    import zlib

    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise AssetError("%s is not a PNG file" % path)
    pos = 8
    header = None
    palette = []
    alpha = b""
    idat = bytearray()
    while pos + 8 <= len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", body)
        elif kind == b"PLTE":
            palette = [tuple(body[i:i + 3]) for i in range(0, len(body), 3)]
        elif kind == b"tRNS":
            alpha = body
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break
    if header is None:
        raise AssetError("%s has no IHDR chunk" % path)
    w, h, depth, color, _compression, _filter, interlace = header
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(color)
    if depth != 8 or channels is None or interlace != 0:
        raise AssetError("%s: unsupported PNG (depth %d, color type %d, interlace %d); "
                         "save it as non-interlaced 8-bit RGB or RGBA" % (path, depth, color, interlace))
    raw = zlib.decompress(bytes(idat))
    stride = w * channels
    if len(raw) != h * (stride + 1):
        raise AssetError("%s: truncated image data" % path)
    rows = []
    prev = bytearray(stride)
    for y in range(h):
        kind = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - channels] if i >= channels else 0
            b = prev[i]
            c = prev[i - channels] if i >= channels else 0
            if kind == 1:
                line[i] = (line[i] + a) & 0xFF
            elif kind == 2:
                line[i] = (line[i] + b) & 0xFF
            elif kind == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 0xFF
            elif kind == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + pred) & 0xFF
            elif kind != 0:
                raise AssetError("%s: invalid PNG filter %d" % (path, kind))
        rows.append(line)
        prev = line
    pixels = []
    for line in rows:
        for x in range(w):
            v = line[x * channels:(x + 1) * channels]
            if color == 0:
                pixels.append((v[0], v[0], v[0], 255))
            elif color == 2:
                pixels.append((v[0], v[1], v[2], 255))
            elif color == 3:
                if v[0] >= len(palette):
                    raise AssetError("%s: palette index out of range" % path)
                r, g, b = palette[v[0]]
                pixels.append((r, g, b, alpha[v[0]] if v[0] < len(alpha) else 255))
            elif color == 4:
                pixels.append((v[0], v[0], v[0], v[1]))
            else:
                pixels.append((v[0], v[1], v[2], v[3]))
    return w, h, pixels


def _png_to_rgb565(path):
    """Return (w, h, rgb565_pixels, mask_bits_or_None)."""
    w, h, rgba = _decode_png_rgba(path)
    pixels = [0] * (w * h)
    mask = [0] * (w * h)
    any_transparent = False
    for i, (r, g, b, a) in enumerate(rgba):
        pixels[i] = rgb888_to_rgb565(r, g, b)
        if a >= 128:
            mask[i] = 1
        else:
            any_transparent = True
    if any_transparent:
        return w, h, pixels, mask
    return w, h, pixels, None

# ---------------------------------------------------------------------------
# BDF parsing
# ---------------------------------------------------------------------------

def _parse_bdf(path):
    """Minimal BDF parser sufficient for Chicago-style proportional fonts.

    Returns a dict:
        {
          'ascent': int, 'descent': int, 'line_height': int,
          'glyphs': { codepoint: {
              'w': int, 'h': int, 'bbx_x': int, 'bbx_y': int,
              'advance': int, 'rows_rowmajor': [bytes, ...]
          }}
        }
    BITMAP rows are emitted as MSB-first row-major bytes (ceil(w/8) bytes/row).
    """
    ascent = 0
    descent = 0
    line_height = 0
    glyphs = {}

    with open(path, "r", encoding="utf-8", errors="replace") as f:
        lines = f.readlines()

    i = 0
    n = len(lines)
    while i < n:
        line = lines[i].strip()
        if line.startswith("FONT_ASCENT"):
            ascent = int(line.split()[1])
        elif line.startswith("FONT_DESCENT"):
            descent = int(line.split()[1])
        elif line.startswith("PIXEL_SIZE"):
            line_height = int(line.split()[1])
        elif line == "STARTCHAR" or line.startswith("STARTCHAR "):
            # Parse one glyph
            cp = None
            bbx_w = bbx_h = bbx_x = bbx_y = 0
            advance = 0
            bitmap_src = []
            i += 1
            while i < n:
                l2 = lines[i].strip()
                if l2.startswith("ENCODING"):
                    cp = int(l2.split()[1])
                elif l2.startswith("DWIDTH"):
                    advance = int(l2.split()[1])
                elif l2.startswith("BBX"):
                    parts = l2.split()
                    bbx_w = int(parts[1])
                    bbx_h = int(parts[2])
                    bbx_x = int(parts[3])
                    bbx_y = int(parts[4])
                elif l2 == "BITMAP":
                    i += 1
                    while i < n and lines[i].strip() != "ENDCHAR":
                        bitmap_src.append(lines[i].strip())
                        i += 1
                    break  # i is on ENDCHAR; the outer loop steps past it
                elif l2 == "ENDCHAR":
                    break
                i += 1
            if cp is not None and 0x20 <= cp <= 0x7E:
                bytes_per_row = (bbx_w + 7) // 8
                rows = []
                for r_hex in bitmap_src:
                    if not r_hex:
                        rows.append(b"\x00" * max(1, bytes_per_row))
                        continue
                    row_int = int(r_hex, 16)
                    # BDF pads rows to whole bytes already; extract as bytes
                    nbytes = (len(r_hex) + 1) // 2
                    row_bytes = row_int.to_bytes(nbytes, "big")
                    # Trim / pad to bytes_per_row
                    if len(row_bytes) > bytes_per_row:
                        row_bytes = row_bytes[:bytes_per_row]
                    elif len(row_bytes) < bytes_per_row:
                        row_bytes = row_bytes + b"\x00" * (bytes_per_row - len(row_bytes))
                    rows.append(row_bytes)
                glyphs[cp] = {
                    "w": bbx_w,
                    "h": bbx_h,
                    "bbx_x": bbx_x,
                    "bbx_y": bbx_y,
                    "advance": advance,
                    "rows": rows,
                }
        i += 1

    if line_height == 0:
        line_height = ascent + descent
    return {
        "ascent": ascent,
        "descent": descent,
        "line_height": line_height,
        "glyphs": glyphs,
    }


# ---------------------------------------------------------------------------
# Emit: bg tile
# ---------------------------------------------------------------------------

def emit_bg_tile(src_path, out_path):
    if os.path.exists(src_path):
        w, h, pixels, _mask = _png_to_rgb565(src_path)
        label = os.path.relpath(src_path, _PROJECT_ROOT)
    else:
        print("[build_assets] WARNING: %s not found; using the built-in gray tile" % src_path)
        w, h, pixels = _default_bg_tile()
        label = "<built-in 16x16 classic 50%% gray tile>"

    contents = _preamble(label)
    contents += "constexpr int BG_TILE_W = %d;\n" % w
    contents += "constexpr int BG_TILE_H = %d;\n\n" % h
    contents += "constexpr uint16_t BG_TILE_PIXELS[%d * %d] = {\n" % (w, h)
    contents += _format_u16_array(pixels) + "\n"
    contents += "};\n"

    _report(out_path, _write_if_changed(out_path, contents), "%dx%d" % (w, h))


# ---------------------------------------------------------------------------
# Emit: happy mac
# ---------------------------------------------------------------------------

def emit_happy_mac(src_path, out_path):
    mask = None
    if os.path.exists(src_path):
        w, h, pixels, mask = _png_to_rgb565(src_path)
        label = os.path.relpath(src_path, _PROJECT_ROOT)
    else:
        print("[build_assets] WARNING: %s not found; using the built-in Happy Mac" % src_path)
        w, h, pixels, mask_bits = _default_happy_mac()
        mask = mask_bits
        label = "<built-in 32x32 classic Happy Mac>"

    contents = _preamble(label)
    contents += "constexpr int HAPPY_MAC_W = %d;\n" % w
    contents += "constexpr int HAPPY_MAC_H = %d;\n" % h
    if mask is not None:
        mask_define = "1"
    else:
        mask_define = "0"
    # Use a #define (not constexpr bool) so the preprocessor can conditionally
    # reference the mask array below.
    contents += "#define HAPPY_MAC_HAS_MASK %s\n\n" % mask_define
    contents += "constexpr uint16_t HAPPY_MAC_PIXELS[%d * %d] = {\n" % (w, h)
    contents += _format_u16_array(pixels) + "\n"
    contents += "};\n\n"

    if mask is not None:
        # Pack 1-bit mask MSB-first, row-major, pad rows to whole bytes.
        packed = []
        bits_per_row = w
        bytes_per_row = (bits_per_row + 7) // 8
        for y in range(h):
            for bx in range(bytes_per_row):
                byte = 0
                for b in range(8):
                    x = bx * 8 + b
                    if x < w:
                        idx = y * w + x
                        if mask[idx]:
                            byte |= 1 << (7 - b)
                packed.append(byte)
        contents += "// 1 bit per pixel, MSB-first, row-major, rows padded to whole bytes.\n"
        contents += "constexpr int HAPPY_MAC_MASK_ROW_BYTES = %d;\n" % bytes_per_row
        contents += "constexpr uint8_t HAPPY_MAC_MASK[%d] = {\n" % len(packed)
        contents += _format_u8_array(packed) + "\n"
        contents += "};\n"

    _report(out_path, _write_if_changed(out_path, contents),
            "%dx%d%s" % (w, h, ", with mask" if mask is not None else ""))


# ---------------------------------------------------------------------------
# Emit: Charcoal font
# ---------------------------------------------------------------------------

def emit_charcoal_font(src_path, out_path):
    if not os.path.exists(src_path):
        raise AssetError("missing %s; the preboot UI font is part of the source tree"
                         % os.path.relpath(src_path, _PROJECT_ROOT))
    font = _parse_bdf(src_path)
    first_cp = 0x20
    last_cp = 0x7E
    missing = [cp for cp in range(first_cp, last_cp + 1) if cp not in font["glyphs"]]
    if missing:
        raise AssetError("%s lacks glyphs for %s" % (
            src_path, ", ".join("U+%04X" % cp for cp in missing)))

    # Pack all glyph bitmaps into a contiguous blob; build table pointing into it.
    blob = bytearray()
    table = []
    for cp in range(first_cp, last_cp + 1):
        g = font["glyphs"][cp]
        offset = len(blob)
        for row in g["rows"]:
            blob.extend(row)
        table.append((offset, g["w"], g["h"], g["bbx_x"], g["bbx_y"], g["advance"]))

    contents = _preamble(os.path.relpath(src_path, _PROJECT_ROOT))
    contents += "struct CharcoalGlyph {\n"
    contents += "    uint16_t offset;     // byte offset into CHARCOAL_GLYPH_BLOB\n"
    contents += "    uint8_t  w;          // glyph bitmap width in pixels\n"
    contents += "    uint8_t  h;          // glyph bitmap height in pixels\n"
    contents += "    int8_t   bbx_x;      // offset from pen position (positive = right)\n"
    contents += "    int8_t   bbx_y;      // offset of bitmap bottom from baseline (positive = up)\n"
    contents += "    uint8_t  advance;    // pen advance after drawing, in pixels\n"
    contents += "};\n\n"

    contents += "constexpr int CHARCOAL_FIRST_CP    = 0x%02X;\n" % first_cp
    contents += "constexpr int CHARCOAL_LAST_CP     = 0x%02X;\n" % last_cp
    contents += "constexpr int CHARCOAL_ASCENT      = %d;\n" % font["ascent"]
    contents += "constexpr int CHARCOAL_DESCENT     = %d;\n" % font["descent"]
    contents += "constexpr int CHARCOAL_LINE_HEIGHT = %d;\n\n" % font["line_height"]

    contents += "// Packed glyph bitmaps: each glyph stores h consecutive rows of\n"
    contents += "// ceil(w/8) bytes, MSB-first. Look up CHARCOAL_GLYPH_TABLE[cp -\n"
    contents += "// CHARCOAL_FIRST_CP] to find the byte offset, width, and height.\n"
    contents += "constexpr uint8_t CHARCOAL_GLYPH_BLOB[%d] = {\n" % max(1, len(blob))
    contents += _format_u8_array(list(blob)) + "\n"
    contents += "};\n\n"

    contents += "constexpr CharcoalGlyph CHARCOAL_GLYPH_TABLE[%d] = {\n" % len(table)
    for (off, w, h, bbx_x, bbx_y, adv) in table:
        contents += "    { %d, %d, %d, %d, %d, %d },\n" % (off, w, h, bbx_x, bbx_y, adv)
    contents += "};\n"

    _report(out_path, _write_if_changed(out_path, contents), "%d glyphs, ascent=%d descent=%d" % (
        len(table), font["ascent"], font["descent"]))


# ---------------------------------------------------------------------------
# Driver
# ---------------------------------------------------------------------------

def build_all():
    """Regenerate every header. Output is deterministic and written only when
    it changes, so unchanged assets never trigger a recompile, and a header
    generated earlier from different inputs can never be mistaken for current.
    """
    _ensure_gen_dir()
    tasks = [
        (SRC_BG_TILE,   OUT_BG_TILE,   emit_bg_tile),
        (SRC_HAPPY_MAC, OUT_HAPPY_MAC, emit_happy_mac),
        (SRC_FONT_BDF,  OUT_FONT,      emit_charcoal_font),
    ]
    try:
        for src, dst, fn in tasks:
            fn(src, dst)
    except (AssetError, OSError, ValueError) as exc:
        print("[build_assets] ERROR: %s" % exc)
        if _PIO_ENV is not None:
            _PIO_ENV.Exit(1)
        raise SystemExit(1)


# Run now: as a PlatformIO pre: script (before compilation) or standalone.
# `--force` is accepted for compatibility; every run regenerates.
if _PIO_ENV is not None or __name__ == "__main__":
    build_all()
