#!/usr/bin/env python3
"""Package the Rez output for installation.

    package.py build/Brightness.bin dist

writes
  dist/Brightness.bin            MacBinary II, for hfsutils (hcopy -m) or any
                                 Mac unarchiver
  dist/M5Tab Extras/Brightness   the same file in Basilisk II's shared-folder
                                 layout (.rsrc/ and .finf/ sidecars). Copy the
                                 whole "M5Tab Extras" folder into the SD card
                                 folder used as the Mac's shared folder; the
                                 sidecars are hidden, so copy the folder, not
                                 its visible contents

Both have Finder's hasBundle flag set so the module shows its own icon.
"""
import struct
import sys
from pathlib import Path

HAS_BUNDLE = 0x2000
NAME = b"Brightness"


def crc16_xmodem(data: bytes) -> int:
    crc = 0
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else crc << 1
            crc &= 0xFFFF
    return crc


def read_macbinary(path: Path):
    raw = path.read_bytes()
    file_type, creator = raw[65:69], raw[69:73]
    data_len, rsrc_len = struct.unpack(">II", raw[83:91])
    data_start = 128
    rsrc_start = data_start + (data_len + 127) // 128 * 128
    return file_type, creator, raw[data_start:data_start + data_len], raw[rsrc_start:rsrc_start + rsrc_len]


def pad(blob: bytes) -> bytes:
    return blob + bytes(-len(blob) % 128)


def write_macbinary(path: Path, file_type, creator, flags, data, rsrc):
    header = bytearray(128)
    header[1] = len(NAME)
    header[2:2 + len(NAME)] = NAME
    header[65:69] = file_type
    header[69:73] = creator
    header[73] = flags >> 8
    struct.pack_into(">II", header, 83, len(data), len(rsrc))
    header[101] = flags & 0xFF
    header[122] = 129
    header[123] = 129
    struct.pack_into(">H", header, 124, crc16_xmodem(bytes(header[:124])))
    path.write_bytes(bytes(header) + pad(data) + pad(rsrc))


def write_shared_folder(folder: Path, file_type, creator, flags, data, rsrc):
    (folder / ".rsrc").mkdir(parents=True, exist_ok=True)
    (folder / ".finf").mkdir(parents=True, exist_ok=True)
    name = NAME.decode()
    (folder / name).write_bytes(data)
    (folder / ".rsrc" / name).write_bytes(rsrc)
    finfo = file_type + creator + struct.pack(">H", flags) + bytes(6)   # FInfo
    (folder / ".finf" / name).write_bytes(finfo + bytes(16))             # + FXInfo


def main():
    source, out = Path(sys.argv[1]), Path(sys.argv[2])
    file_type, creator, data, rsrc = read_macbinary(source)
    if not rsrc:
        raise SystemExit(f"{source}: no resource fork")
    out.mkdir(parents=True, exist_ok=True)
    write_macbinary(out / "Brightness.bin", file_type, creator, HAS_BUNDLE, data, rsrc)
    write_shared_folder(out / "M5Tab Extras", file_type, creator, HAS_BUNDLE, data, rsrc)
    print(f"{out}/Brightness.bin: type {file_type.decode()} creator {creator.decode()}, "
          f"resource fork {len(rsrc)} bytes")


if __name__ == "__main__":
    main()
