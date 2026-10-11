# Brightness Control Strip module

A Mac OS Control Strip module (file type `sdev`, creator `M5Br`) that sets
the M5Tab-Macintosh backlight from the Mac. It shows a sun in the Control
Strip; clicking it pops up eight levels and **Turn Off Display**. The level
is saved with the Control Strip preferences and restored at startup.

It uses the emulator's power services, `Gestalt('M5pw')`, described in
[POWER_MANAGEMENT.md](../../POWER_MANAGEMENT.md). Where they are missing,
the module declines to load.

Install the prebuilt module from `dist/` as described in
[POWER_MANAGEMENT.md](../../POWER_MANAGEMENT.md#installing-the-brightness-module).

## Files

| File | Purpose |
|---|---|
| `brightness.c` | The module (Control Strip `sdev` messages, menu, preferences) |
| `brightness.r` | Resources: the code, Finder bundle, `vers` |
| `make_icons.py` | Draws the sun icons and writes `icons.r` |
| `package.py` | Writes `dist/` from the Rez output |
| `build.sh` | Builds everything |
| `dist/Brightness.bin` | Prebuilt module, MacBinary II |
| `dist/M5Tab Extras/` | Prebuilt module in Basilisk II's shared-folder layout |

## Building

Needs [Retro68](https://github.com/autc04/Retro68) (68k toolchain, Rez and
Universal Interfaces). Point `RETRO68_TOOLCHAIN` at its toolchain
directory, then:

```sh
RETRO68_TOOLCHAIN=/path/to/Retro68-build/toolchain ./build.sh
```

The code is a flat code resource (`-Wl,--mac-flat`). Retro68 upper-cases
`pascal` function names, so the entry point is `MODULEMAIN`.
`RETRO68_RELOCATE()` fixes up absolute addresses on the first call and
locks the code. The module has no globals; its state is a system-heap
handle returned from `sdevInitModule`, which the Control Strip passes back
in `params`.

The module reports its preference load and save results through the
power services' Log call (selector 6), which prints them on the serial
console.

Rebuild and commit `dist/` after changing anything here. The host tests
(`test/test_power.py`) check the committed packaging.
