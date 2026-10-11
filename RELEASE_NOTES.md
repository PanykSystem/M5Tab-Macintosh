# 5.2 - 24 MB and Web Browsing

5.2 lets Mac OS 8 use up to 24 MB of RAM, enough to run a period web
browser, and fixes Wi-Fi auto-connect so the Mac has a network after a
normal power-on. Performance is unchanged from 5.1.

On the development Tab5 with 24 MB, Mac OS 8.1 reports 24 MB of built-in
memory with an 18 MB largest unused block, and Internet Explorer loads
`http://system7today.com` over Wi-Fi. Speedometer 4.02 rates it the same as
5.1 (Quadra 605 = 1.0):

| Score | 5.1, 16 MB | 5.2, 24 MB |
| --- | ---: | ---: |
| CPU | 0.804 | 0.806 |
| Graphics | 1.033 | 1.036 |
| Disk | 1.03 | 1.03 |
| Math | 10.64 | 10.56 |
| **Performance Rating** | **1.009** | **1.01** |

## What changed

- **20 and 24 MB of Mac RAM.** Startup Settings and serial control offer 8,
  16, 20 and 24 MB. A saved 4 or 12 MB setting from older firmware rounds up
  to 8 or 16 MB. The 16 MB limit was only the list of choices: the Tab5 still
  had 10.75 MB of PSRAM free with 16 MB of Mac RAM. If guest RAM would leave
  less than 5 MB of PSRAM for the emulator, startup uses the next smaller
  size and logs a warning instead of failing. The emulator also frees its 1 MB
  68k decode table after startup, so 24 MB leaves 3.4 MB of PSRAM free.
- **Wi-Fi at power-on.** Wi-Fi auto-connect started in the background, but
  the splash usually started the emulator first, and the connection was
  dropped. The emulator only looks for a network at startup, so the Mac had
  none unless settings had just been changed over serial. Startup now waits
  up to 10 seconds for the connection; the Tab5 connects in about 6.
- **Shut Down with a network.** Choosing Special > Shut Down while Mac OS had
  a network crashed the Tab5: the emulator deleted its network task after
  the task had already deleted itself. The bug dates from v3.0 but was rare,
  because Wi-Fi seldom reached the emulator before the fix above. The 5.2
  hardware gate found it.
- **Diagnostics.** The serial `MEM` command reports free PSRAM, its largest
  free block and the lowest free since boot. The boot log reports the largest
  free block after startup.
- **Cleanup.** The unused, compiled-out RV32 JIT code is removed. The
  firmware it produced was unchanged.

## Browsing from Mac OS 8

Join Wi-Fi in Startup Settings first. In Mac OS, TCP/IP must be on:

1. If **TCP/IP** is missing from Control Panels, open **Extensions Manager**,
   turn on TCP/IP and the Open Transport extensions (Open Transport Library,
   OpenTransportLib, Open Tpt Internet Library, OpenTptInternetLib, Open Tpt
   AppleTalk Library, OpenTptAppleTalkLib) and restart. A copy of your
   current set keeps the original intact.
2. In **TCP/IP**, set Connect via **Ethernet** and Configure **Using DHCP
   Server**.

Browsers of this era cannot negotiate modern HTTPS. Use plain-HTTP sites
such as `http://system7today.com`. With Open Transport loaded and Wi-Fi
connected, the Performance Rating was 0.991 to 0.999 over eight runs.

## Downloads

| Hardware / use | Filename | Flash offset |
| --- | --- | --- |
| Tab5, pre-v3, standard SDK | `M5Tab-Macintosh-v5.2.bin` | `0x0` |
| Tab5, v3.1+ | `M5Tab-Macintosh-Rev3-v5.2.bin` | `0x0` |
| Waveshare 10.1, pre-v3 | `M5Tab-Macintosh-Waveshare-P4-10.1-v5.2.bin` | `0x0` |
| Waveshare 10.1, v3.1+ | `M5Tab-Macintosh-Waveshare-P4-10.1-Rev3-v5.2.bin` | `0x0` |
| Tab5, pre-v3, tested USB debug SDK | `M5Tab-Macintosh-USB-Debug-v5.2.bin` | `0x0` |
| Same debug application, existing compatible Tab5 installation | `M5Tab-Macintosh-USB-Debug-v5.2-app.bin` | `0x10000` |
| Python CLI, MCP server, benchmark and startup-screen tour | `M5Tab-Macintosh-Host-Tools-v5.2.zip` | — |

Chip revision is separate from panel or PCB revision. Rev3 images require
ESP32-P4 revision **3.1 or newer**; revision 3.0 is not covered. Merged images
include bootloader, partitions and application, and replace an existing
launcher. The debug application-only image preserves bootloader, partition
table and NVS on a compatible installation. No package includes a Macintosh
ROM, disk images or credentials.

The firmware contains code translated from Apple's Quadra 650 ROM and Mac OS
system software, unchanged from 5.1. The generated translation source is not
in the repository. Without it, a build from source runs everything in the
interpreter.

The optional pre-v3 Tab5 debug profile uses Arduino 3.3.8 / IDF 5.5.4 and
matches the development device used for hardware testing. Use the matching
board/silicon image; the debug image is not for Waveshare or production Rev3
hardware.

## Verification

- 58 host tests, including the new RAM choices, saved-setting migration and
  the Settings screen's Memory row rendered on both boards.
- All five firmware profiles build. Each one's translated code passes
  `check_blob.py`. Every merged image is checked against its bootloader,
  partition table and application at the expected flash offsets.
- The translation still matches the interpreter: the host differential test
  ran 51,102 trials covering 186 million instructions with no mismatch, and
  the on-device self-test on the Tab5 also passed.
- Hardware checks on a pre-v3 ESP32-P4 revision 1.3 Tab5 with Mac OS 8.1 and
  24 MB:
  - About This Computer reports 24 MB built-in memory.
  - Performance Ratings at 24 MB, with and without networking loaded, and
    150 back-to-back color screenshots over serial with Wi-Fi connected.
  - Full-screen pixel checks with 0 of 921,600 pixels wrong.
  - Wi-Fi connected after a plain power-on, and Internet Explorer 3.01a
    loaded `http://system7today.com`.
  - The lowest free PSRAM across startup, Speedometer, Wi-Fi and browsing was
    2.86 MB.
  - Special > Shut Down with Wi-Fi connected, ending at the power-off screen,
    followed by a clean start.
  - The released debug application was flashed and read back with a matching
    hash, printed the 5.2 banner and booted to the Finder with saved settings
    intact.
- `BUILD-MANIFEST.json` records exact source commits, component hashes, the
  translation's hash and the hardware-tested application. `SHA256SUMS` covers
  downloadable assets.

## Limits and rollback

Waveshare and production-silicon (Rev3) images share this code and build
cleanly but were not run on hardware for this release. Waveshare's panel
framebuffer is 0.2 MB larger than the Tab5's; 24 MB should still fit, and
startup steps down if it does not. The new Memory row was tested with
injected touches, not fingers. At 24 MB about 3.4 MB of PSRAM stays free for
screenshots, networking and other runtime buffers.

During release testing the Tab5 reset once with Wi-Fi connected, while a
color screenshot was being read over serial just after a Performance
Rating. It came back in Startup Settings with the disk image unchanged. The
cause was not found, and it did not recur in eight further ratings and 150
screenshot transfers. Please report a reset with the serial log if you see
one.

The [v5.1 release](https://github.com/amcchord/M5Tab-Macintosh/releases/tag/v5.1)
remains available for rollback with a matching board/silicon image. A 5.1
firmware reads a saved 20 or 24 MB setting as 8 MB. Preserve known-good
firmware and SD backups. Firmware rollback does not revert settings or guest
disk contents.
