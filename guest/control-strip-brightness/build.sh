#!/bin/sh
# Build the Brightness Control Strip module with Retro68
# (https://github.com/autc04/Retro68). Set RETRO68_TOOLCHAIN to the
# toolchain directory (the one holding bin/m68k-apple-macos-gcc).
# Output: build/Brightness.bin (MacBinary) and the files under dist/.
set -eu
cd "$(dirname "$0")"
T="${RETRO68_TOOLCHAIN:-$HOME/Development/ClassicMac/vendor/Retro68-build/toolchain}"
if [ ! -x "$T/bin/m68k-apple-macos-gcc" ]; then
    echo "Retro68 toolchain not found at $T; set RETRO68_TOOLCHAIN" >&2
    exit 1
fi
mkdir -p build
python3 make_icons.py
# A flat code resource. Retro68 upper-cases pascal function names, hence MODULEMAIN.
"$T/bin/m68k-apple-macos-gcc" -Os -Wall -Wno-pointer-sign -ffunction-sections \
    -Wl,--mac-flat -Wl,-eMODULEMAIN -Wl,-gc-sections \
    -o build/brightness.code brightness.c
"$T/bin/Rez" -I "$T/RIncludes" brightness.r -t sdev -c M5Br -o build/Brightness.bin
python3 package.py build/Brightness.bin dist
