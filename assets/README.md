# Pre-boot assets

These files feed `scripts/build_assets.py`, which runs during PlatformIO
pre-build and emits C headers under `src/generated/`. The firmware then
compiles those headers in, so anything here ends up baked into the binary
(no SD-card dependency at splash time).

## Files

| Path | Purpose | Notes |
| --- | --- | --- |
| `BgTile.png` | Splash background | Small tile (e.g. 6x6, 16x16). One tile pixel per logical pixel, so it shows at 2x like the Mac screen. |
| `happymacIconNew.png` | Centered splash icon | PNG with alpha; transparent pixels are skipped. Shown at 2x. |
| `fonts/charcoal12.bdf` | Charcoal 12, the Mac OS 8 system font | Used by every preboot screen and the touch keyboard overlay. Required. |

The PNGs must be non-interlaced 8-bit gray, RGB, palette or RGBA images. If
one is missing, the script uses a classic built-in default (50% gray tile,
32x32 Happy Mac) and prints a warning. A PNG or font that is present but
cannot be decoded, or a missing font, stops the build: release images must
never fall back to placeholder art silently.

## The Charcoal font

`fonts/charcoal12.bdf` is a pixel recreation of Charcoal 12. Its bitmaps and
advance widths were traced from text rendered on screen by Mac OS 8.1
(SimpleText, Charcoal, 12 point) running in this emulator. Rendering the BDF
reproduces the guest's own text pixel for pixel. Line height is 16 pixels,
capitals are 9 pixels tall and the baseline sits 12 pixels below the top of
the line.

## Regenerating manually

```sh
python3 scripts/build_assets.py
```

Only the Python standard library is needed, so PlatformIO's own Python
produces the same headers as any other interpreter. Every run regenerates all
headers and rewrites only those whose contents changed, so a header made from
different inputs can never be mistaken for current.
