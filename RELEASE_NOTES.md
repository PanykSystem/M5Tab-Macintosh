# 5.1 - Translated Code and Startup Settings

5.1 runs the Mac's most-used ROM and system code as translated native code,
redesigns the startup screens in the Mac OS 8 style, and makes Special > Shut
Down work.

Speedometer 4.02 Performance Rating on the development Tab5 (Quadra 605 = 1.0):

| Score | 5.0.1 | 5.1 | Change |
| --- | ---: | ---: | ---: |
| CPU | 0.804 | 0.804 | unchanged |
| Graphics | 0.496 | 1.033 | +108% |
| Disk | 1.03 | 1.03 | unchanged |
| Math | 9.47 | 10.64 | +12% |
| **Performance Rating** | **0.765** | **1.009** | **+32%** |

5.1 values are medians of three full ratings on the release candidate
(PR 1.008 to 1.009). Speedometer's CPU test is a sort loop inside Speedometer
itself, which 5.1 does not translate, so its score is unchanged.

## What changed

- **Translated ROM and system code.** The hottest 68k code in the Quadra 650
  ROM and in Mac OS system software is translated to C++ when the firmware is
  built, from the interpreter's own instruction handlers. It runs from PSRAM
  and is entered through patched opcodes; nothing is generated at run time.
  Every ROM region is CRC-checked against the running ROM before it is
  installed. System code in RAM is installed only where its bytes match, and
  is checked again after every guest cache flush. Anything else, including
  application code, other ROMs and other System versions, runs in the
  interpreter at 5.0.1 speed. During development, Speedometer's Color
  QuickDraw test with the same translation scored 1.007 in black and white
  (5.0.1: 0.549) and 0.869 at 8 bits (5.0.1: 0.509).
- **Mac OS 8 style Startup Settings.** The startup screens now use the
  Platinum appearance and a Charcoal 12 font. They are drawn at the Mac's own
  resolution and doubled exactly like the Mac screen. Lists scroll, the Wi-Fi
  list shows each network name once, the keyboard has punctuation, Cancel
  discards typed text, and leaving Tab5 USB Disk mode returns to settings.
  5.0.1 images had shipped a 5x7 debug font and a placeholder Happy Mac: the
  asset build silently fell back when Pillow was missing. It now uses only the
  Python standard library and fails loudly.
- **Shut Down works.** Choosing Special > Shut Down used to crash the
  emulator (`Load access fault` in the CPU loop), and the next start opened
  recovery settings. The emulator kept running past Mac OS's power-off call
  into unmapped memory, a bug present since at least v3.4.2. The CPU now stops
  at that point. Mac OS's own shutdown completes, the screen says "It is now
  safe to switch off your Macintosh.", and the next start is clean.
- **Diagnostics.** Serial control protocol 5 adds screenshots, pixel checks
  and touch injection on the startup screens, a full-resolution panel readback
  in any phase (`SCREENSHOT PANEL`), and translated-code statistics
  (`AOT STATS`). `tools/preboot_screenshots.py` captures every startup screen.

Native QuickDraw acceleration remains disabled, as in 5.0.

The [performance design guide](EMULATOR_PERFORMANCE.md) explains the
translation, its safety rules and how to regenerate it
([procedure](tools/aot/README.md)). The
[performance report](PERFORMANCE_REPORT.md) has build-by-build measurements.
The [serial control reference](AUTOMATION.md) covers protocol 5.

## Downloads

| Hardware / use | Filename | Flash offset |
| --- | --- | --- |
| Tab5, pre-v3, standard SDK | `M5Tab-Macintosh-v5.1.bin` | `0x0` |
| Tab5, v3.1+ | `M5Tab-Macintosh-Rev3-v5.1.bin` | `0x0` |
| Waveshare 10.1, pre-v3 | `M5Tab-Macintosh-Waveshare-P4-10.1-v5.1.bin` | `0x0` |
| Waveshare 10.1, v3.1+ | `M5Tab-Macintosh-Waveshare-P4-10.1-Rev3-v5.1.bin` | `0x0` |
| Tab5, pre-v3, tested USB debug SDK | `M5Tab-Macintosh-USB-Debug-v5.1.bin` | `0x0` |
| Same debug application, existing compatible Tab5 installation | `M5Tab-Macintosh-USB-Debug-v5.1-app.bin` | `0x10000` |
| Python CLI, MCP server, benchmark and startup-screen tour | `M5Tab-Macintosh-Host-Tools-v5.1.zip` | — |

Chip revision is separate from panel or PCB revision. Rev3 images require
ESP32-P4 revision **3.1 or newer**; revision 3.0 is not covered. Merged images
include bootloader, partitions and application, and replace an existing
launcher. The debug application-only image preserves bootloader, partition
table and NVS on a compatible installation. No package includes a Macintosh
ROM, disk images or credentials.

The firmware contains code translated from Apple's Quadra 650 ROM and Mac OS
system software. The generated translation source is not in the repository.
Without it, a build from source runs everything in the interpreter, as 5.0.1
does.

The optional pre-v3 Tab5 debug profile uses Arduino 3.3.8 / IDF 5.5.4 and
matches the development device used for hardware testing. Use the matching
board/silicon image; the debug image is not for Waveshare or production Rev3
hardware.

## Verification

- 58 host tests. New since 5.0.1: startup-screen layouts and native renders on
  both boards and rotations, the standard-library asset pipeline, and the
  translator and its relocation checks.
- All five firmware profiles build. Each one's translated code passes
  `check_blob.py`, which confirms it can run from PSRAM. Every merged image is
  checked against its bootloader, partition table and application at the
  expected flash offsets.
- The translation matches the interpreter: the host differential test ran
  51,102 trials covering 186 million instructions with no mismatch. The
  on-device self-test on the Tab5 ran 26,866 trials covering 91.8 million
  instructions, also with no mismatch.
- Hardware checks on a pre-v3 ESP32-P4 revision 1.3 Tab5 with Mac OS 8.1:
  - Three full Performance Ratings.
  - A tour of the startup screens with 0 of 921,600 panel pixels wrong at
    every step.
  - Shut Down ending at the power-off screen, followed by a clean start.
  - Restart returning to the Finder.
  - The released debug application was flashed and read back with a matching
    hash. It printed the 5.1 banner and booted to the Finder with saved
    settings intact. It then completed a Performance Rating with a clean
    full-screen pixel check.
- `BUILD-MANIFEST.json` records exact source commits, component hashes, the
  translation's hash and the hardware-tested application. `SHA256SUMS` covers
  downloadable assets.

## Limits and rollback

Waveshare and production-silicon (Rev3) images share this code and build
cleanly but were not run on hardware for this release. That includes the new
startup screens at Waveshare's 640x400. Startup-screen input was tested with
injected touches, not fingers. The translation was profiled on Mac OS 8.1;
other System versions run their own code in the interpreter. On the Tab5,
free internal SRAM after startup is 168 KB (5.0.1: 177 KB).

The [v5.0.1 release](https://github.com/amcchord/M5Tab-Macintosh/releases/tag/v5.0.1)
remains available for rollback with a matching board/silicon image. Preserve
known-good firmware and SD backups. Firmware rollback does not revert settings
or guest disk contents.
