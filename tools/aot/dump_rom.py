#!/usr/bin/env python3
"""Save the ROM as the running emulator has it, patches included.

    dump_rom.py --port PORT --out patched.rom [--base 40800000] [--size 0x100000]

Basilisk II patches the ROM at start-up, and translated code must match the
patched bytes. This reads them from the running firmware with PEEK, 256
bytes a request. The file is a copy of the ROM: keep it out of the
repository.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--base", type=lambda v: int(v, 16), default=0x40800000)
    parser.add_argument("--size", type=lambda v: int(v, 0), default=0x100000)
    args = parser.parse_args()

    from mac_control import MacControl

    data = bytearray()
    with MacControl(args.port, reset_on_failure=False) as control:
        control.connect(30.0)
        while len(data) < args.size:
            length = min(256, args.size - len(data))
            for _ in range(5):
                fields = control.request(f"PEEK {args.base + len(data):X} {length}", "OK PEEK",
                                         timeout=10.0).split()
                position = fields.index("PEEK") + 2
                if len(fields) > position and len(fields[position]) == 2 * length:
                    data += bytes.fromhex(fields[position])
                    break
            else:
                raise SystemExit(f"no usable reply at offset {len(data):#x}")
    args.out.write_bytes(bytes(data))
    print(f"{len(data)} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
