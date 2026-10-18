# 5.4 beta 1 — One-to-one display scaling

This prerelease integrates Raf (@netsplitz)'s shared 1× display pipeline from
[PR #29](https://github.com/amcchord/M5Tab-Macintosh/pull/29), with source-bound
checks and expanded regression tests. Existing Tab5 and Waveshare 10.1 images
continue to use 2× scaling, their existing resolutions and their existing SDK
pins. There is no new hardware target or user-selectable scaling setting.

## What changed

- Indexed 1-, 2-, 4- and 8-bit pixels can be rendered at 1× directly into a
  portrait-native panel framebuffer, in either supported rotation.
- The snapshot renderer and preboot UI use the board's scale. At 1× the
  fallback renderer now fills its allocated buffer without a 2× expansion.
- Both indexed writers reject a source pixel range that exceeds the declared
  row stride. This prevents a buffer overread for malformed source geometry;
  existing production callers already supply valid rows.
- Pointer advancement stays within source/output arrays, including the final
  row and the unused duplicate-row pointer at 1×.
- A host-only 640×360 configuration exercises 1× scaling. It is not firmware
  for Olimex or another physical panel.

## Downloads

All firmware assets in this prerelease are **software/build validated only**.
No image from this release has been flashed or tested on a physical display.
The debug profile is an additional build configuration, not a hardware-tested
release candidate. Stable users can remain on
[v5.3](https://github.com/amcchord/M5Tab-Macintosh/releases/tag/v5.3).

| Hardware / use | Filename | Flash offset |
| --- | --- | --- |
| Tab5, pre-v3 | `M5Tab-Macintosh-v5.4-beta.1.bin` | `0x0` |
| Tab5, v3.1+ | `M5Tab-Macintosh-Rev3-v5.4-beta.1.bin` | `0x0` |
| Waveshare 10.1, pre-v3 | `M5Tab-Macintosh-Waveshare-P4-10.1-v5.4-beta.1.bin` | `0x0` |
| Waveshare 10.1, v3.1+ | `M5Tab-Macintosh-Waveshare-P4-10.1-Rev3-v5.4-beta.1.bin` | `0x0` |
| Tab5, pre-v3, USB debug SDK | `M5Tab-Macintosh-USB-Debug-v5.4-beta.1.bin` | `0x0` |
| Same debug application, compatible existing installation | `M5Tab-Macintosh-USB-Debug-v5.4-beta.1-app.bin` | `0x10000` |
| Python CLI, MCP server and instructions | `M5Tab-Macintosh-Host-Tools-v5.4-beta.1.zip` | — |
| Brightness Control Strip module (unchanged) | `M5Tab-Macintosh-Mac-Extras-v5.4-beta.1.zip` | — |

Chip revision is separate from panel or PCB revision. Rev3 images require
ESP32-P4 **3.1 or newer**; revision 3.0 is not covered. Merged images contain
the bootloader, partition table and application and replace an existing
launcher. The debug application-only image preserves those other flash
regions on a compatible installation. Preserve known-good firmware and SD
backups. No package contains a Macintosh ROM, disk image or credentials.

The ROM/system translation is unchanged from v5.3 (SHA256
`7fb5dfeea3f9d38c3a8901f216d17f4520736bdc5035bda0fbecc6588595e3dc`).
Its generated source remains git-ignored. Builds without that local artifact
run the interpreter; see [EMULATOR_PERFORMANCE.md](EMULATOR_PERFORMANCE.md).
The [power-management guide](POWER_MANAGEMENT.md) covers installation of the
unchanged Brightness module.

## Software verification

- 68 host tests, including AddressSanitizer/UndefinedBehaviorSanitizer runs
  for Tab5 2×, Waveshare 2× and host-only 1× display pipelines.
- Packed decoding at all four indexed depths, both rotations, random and
  partial damage, palette-only redraws, cache-publication retries and render
  lifetime checks. The full snapshot/render/push fallback is forced at every
  depth and rotation; the real overlay compositor has tile-boundary checks
  for all three geometries.
- Preboot screens at all three geometries, logical control hit tests, exact
  canvas-to-panel pixel comparisons, panel diagnostics, odd single-pixel
  damage, the last panel pixel and failed-publication retry.
- All five existing firmware profiles build with their SDK pins unchanged.
  Every translated-code blob passes `check_blob.py`; merged images are
  compared with their components at the expected flash offsets, and ESP
  image checksums are verified. Exact hashes are in `BUILD-MANIFEST.json`
  and `SHA256SUMS`.

## Outstanding hardware validation and rollback

A Tab5 regression is still required before promoting this prerelease to
stable: startup settings and splash, Finder, all indexed color depths,
rotation, touch overlay, panel verification and sustained use. Host tests
validate memory contents and geometry; they cannot establish panel timing,
optical tearing, physical color fidelity, touch behavior or performance.
Waveshare and production-silicon images also require physical testing.

This release does not resolve
[issue #28](https://github.com/amcchord/M5Tab-Macintosh/issues/28)'s color-mapping
investigation, add the
[Olimex proposal in issue #27](https://github.com/amcchord/M5Tab-Macintosh/issues/27),
or establish support for other proposed hardware. Board-specific drivers,
SDK isolation and hardware results remain separate work.

To roll back, flash the matching board/silicon image from v5.3. Firmware
rollback does not revert settings or guest disk contents. No attached device
was modified during preparation of this release.
