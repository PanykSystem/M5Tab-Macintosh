# 5.3 - Battery, Sleep and Brightness

On the Tab5, Mac OS 8 now has a PowerBook-style Power Manager fed by the
Tab5's battery monitor. The Control Strip's **Battery Monitor** shows the
charge level, charging and time remaining; **Sleep** turns the display off
until you touch it; **Power Settings** dims and turns off the display when
idle; and a new **Brightness** Control Strip module sets the backlight from
the Mac. The emulated machine is still a Quadra: the emulator answers the
Power Manager calls itself, with no PMU emulation.
[POWER_MANAGEMENT.md](POWER_MANAGEMENT.md) has the details.

## What changed

- **Battery Monitor.** Apple's Control Strip module, already in Mac OS 8.1,
  appears on the Tab5 and shows the level, a charging indicator, time
  remaining on battery and "battery removed". The Finder's menu bar clock
  shows a battery gauge too. The Tab5 has no fuel gauge: the level is
  estimated from the battery monitor's voltage and current, smoothed so the
  gauge does not jump with load.
- **Sleep.** Special > Sleep (new in the Finder's menu) and the Sleep Now
  module turn the backlight off and pause the Mac until a touch, key or
  mouse movement. The waking touch is not passed to the Mac. Wi-Fi stays up.
- **Idle dimming.** Power Settings' Better Conservation and Better
  Performance presets dim the display after the dimming timeout and turn it
  off after the sleep timeout (1 and 2 minutes, or 5 and 15).
- **Brightness module.** A new Control Strip module (download
  `M5Tab-Macintosh-Mac-Extras-v5.3.zip`) sets the backlight to one of eight
  levels or turns the display off, and remembers the level.
- **Power services for Mac software.** Gestalt `'M5pw'` gives programs the
  backlight, a battery record, display-off and a serial log, on every board.
- **Settings and serial control.** `power_manager=no` in
  `/basilisk_settings.txt`, or `boot-set power_manager false`, turns the
  Power Manager off. `POWER` serial commands report battery and display
  state and can simulate battery states for testing. `mac_control.py power`
  wraps them.
- **Host tools.** `mac_control.py boot-get` now works against firmware that
  predates a setting.

The Waveshare board has no battery, so Mac OS sees no Power Manager there;
the Brightness module still works.

## Installing the Brightness module

Unzip `M5Tab-Macintosh-Mac-Extras-v5.3.zip`. Then either:

- serve `Brightness.bin` from a computer on your network (for example
  `python3 -m http.server` in that folder) and open
  `http://<computer>:8000/Brightness.bin` in Internet Explorer 3.01 on the
  Mac, which decodes it and saves **Brightness**; or
- with the disk image on a computer and hfsutils installed,
  `hcopy -m Brightness.bin ":System Folder:Control Strip Modules:Brightness"`;
  or
- copy the `M5Tab Extras` folder (with its hidden `.rsrc` and `.finf`
  folders) into your SD card's shared folder.

Drag **Brightness** onto the System Folder; the Finder puts it in Control
Strip Modules. Restart. The Control Strip control panel must be on.

## Downloads

| Hardware / use | Filename | Flash offset |
| --- | --- | --- |
| Tab5, pre-v3, standard SDK | `M5Tab-Macintosh-v5.3.bin` | `0x0` |
| Tab5, v3.1+ | `M5Tab-Macintosh-Rev3-v5.3.bin` | `0x0` |
| Waveshare 10.1, pre-v3 | `M5Tab-Macintosh-Waveshare-P4-10.1-v5.3.bin` | `0x0` |
| Waveshare 10.1, v3.1+ | `M5Tab-Macintosh-Waveshare-P4-10.1-Rev3-v5.3.bin` | `0x0` |
| Tab5, pre-v3, tested USB debug SDK | `M5Tab-Macintosh-USB-Debug-v5.3.bin` | `0x0` |
| Same debug application, existing compatible Tab5 installation | `M5Tab-Macintosh-USB-Debug-v5.3-app.bin` | `0x10000` |
| Brightness Control Strip module for the Mac | `M5Tab-Macintosh-Mac-Extras-v5.3.zip` | — |
| Python CLI, MCP server, benchmark and startup-screen tour | `M5Tab-Macintosh-Host-Tools-v5.3.zip` | — |

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

- 64 host tests, including the battery estimate (charger inference, time
  estimates, simulation, backlight states), the committed Brightness module
  package and `boot-get` against older firmware.
- All five firmware profiles build. Each one's translated code passes
  `check_blob.py`. Every merged image is checked against its bootloader,
  partition table and application at the expected flash offsets.
- The translation is unchanged from 5.1. On the Tab5 all 537 translated ROM
  regions still install over the 5.3-patched ROM (each is checked against
  the ROM bytes first). The on-device self-test was stopped early, by choice,
  after 8,130 trials with no failures.
- Hardware testing on a pre-v3 ESP32-P4 revision 1.3 Tab5 with Mac OS 8.1,
  24 MB and Wi-Fi, before the release build:
  - The Power Manager traps stay installed through startup, and Gestalt
    reports the Power Manager and the power services.
  - Battery Monitor showed the live battery (charging, then full on USB)
    and simulated states: on battery with time remaining, charging, low and
    pack removed. The menu bar clock showed the same.
  - Sleep Now and Special > Sleep turned the display off; a mouse movement or
    `POWER WAKE` woke it, and Open Transport kept working.
  - Better Conservation dimmed the display after 60 s idle and turned it off
    after 120 s.
  - The Brightness module's menu, icon refresh, Turn Off Display and saved
    level across restarts.
  - Speedometer 4.02 with the Control Strip open over the test: PR 0.977 to
    0.982 (CPU 0.80, Graphics 0.94 to 0.95, Math 10.3) over four runs; with the
    strip collapsed, PR 1.091 (Graphics 1.41). 5.2 rated 1.000 to 1.002 in
    the same session with its narrower strip.
- The 5.3 USB-Debug application was flashed to that Tab5 and read back with a
  matching hash. The release gate did not finish: during the reset that
  followed, the Tab5 left USB and could not be checked remotely. The release
  was published without the remaining gate steps.
- `BUILD-MANIFEST.json` records exact source commits, component hashes, the
  translation's hash and the hardware testing. `SHA256SUMS` covers
  downloadable assets.

## Limits and rollback

- Sleep keeps the ESP32-P4, Wi-Fi and the display controller powered; only
  the backlight is off and the Mac is paused. Mac OS sleep procedures are
  not called (one of them, the Display Manager's, crashed the firmware), so
  software that wants to know about sleep is not told.
- There are no low-battery warnings and no automatic shutdown on an empty
  battery. Power Settings' timeouts last until the Mac restarts.
- Not tested on battery power alone (the USB cable carried the serial
  console in testing), with the battery pack removed, or with the
  shared-folder install route. Waveshare and Rev3 images build cleanly but
  were not run on hardware.
- With the Power Manager on, the Special menu gains Sleep, so Restart and
  Shut Down move down a row. Scripts that click fixed menu positions need
  the new positions (see POWER_MANAGEMENT.md).
- The Control Strip floats over the bottom of the screen. With Battery
  Monitor, Sleep Now, Power Settings and Brightness in it, it is wider, and
  Speedometer's Graphics score drops when its test area is covered; collapse
  the strip to compare scores.

The [v5.2 release](https://github.com/amcchord/M5Tab-Macintosh/releases/tag/v5.2)
remains available for rollback with a matching board/silicon image. 5.2
ignores the `power_manager` setting; the Brightness module declines to load
on it. Preserve known-good firmware and SD backups. Firmware rollback does
not revert settings or guest disk contents.
