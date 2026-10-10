#!/usr/bin/env python3
"""Check that the translated code in a firmware ELF can run from a copy.

    check_blob.py firmware.elf [--object aot_rom_generated.cpp.o]

The firmware copies the code between aot_blob_begin and aot_blob_end to
PSRAM and runs it there (AOT_PSRAM). That works only if the code

  - is all of the region functions and nothing else,
  - reaches nothing outside itself relative to the PC (a call or branch out,
    or an address formed with auipc), and
  - holds no absolute address of its own code, which would lead back into
    the copy in flash.

The first two are read from the disassembly. For the third the disassembly
can only show values that look like such an address, and a guest constant
can (0x4008FA00 is also the upper half of a double). The object file's
relocation records say which values are addresses; give it with --object
and they decide. Without it, every such value counts as a problem.

Exits non-zero and lists the offending instructions otherwise. An ELF
without translated code passes.
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

TOOLS = Path.home() / ".platformio/tools/toolchain-riscv32-esp/bin"
BRANCHES = {"j", "jal", "beq", "bne", "blt", "bge", "bltu", "bgeu", "beqz", "bnez", "bgez", "bltz", "blez",
            "bgtz", "bgt", "ble", "bgtu", "bleu"}
REGION = re.compile(r"aot_(m\d+_)?r\d+\b")


def tool(name: str) -> str:
    for root in (TOOLS, Path.home() / ".platformio/packages/toolchain-riscv32-esp/bin"):
        path = root / f"riscv32-esp-elf-{name}"
        if path.exists():
            return str(path)
    raise SystemExit(f"riscv32-esp-elf-{name} not found")


ABSOLUTE = {"R_RISCV_HI20", "R_RISCV_LO12_I", "R_RISCV_LO12_S", "R_RISCV_32", "R_RISCV_ADD32",
            "R_RISCV_SUB32"}
SECTION = ".text.aot_regions"


def own_addresses(obj: str) -> list[str]:
    """Absolute references to the translated code, from the relocation
    records. Entry tables may hold the address of a region function."""
    # readelf, because objdump leaves the compiler's local labels out.
    index = None
    loaded = set()          # sections that become part of the firmware image
    for line in subprocess.run([tool("readelf"), "-S", "-W", obj], capture_output=True, text=True,
                               check=True).stdout.splitlines():
        m = re.match(r"\s*\[\s*(\d+)\]\s+(\S+)\s+\S+\s+[0-9a-f]+\s+[0-9a-f]+\s+[0-9a-f]+\s+[0-9a-f]+\s+(\S*)",
                     line)
        if not m:
            continue
        if m.group(2) == SECTION:
            index = m.group(1)
        if "A" in m.group(3):
            loaded.add(m.group(2))
    if index is None:
        return []
    inside = {SECTION}
    for line in subprocess.run([tool("readelf"), "-s", "-W", obj], capture_output=True, text=True,
                               check=True).stdout.splitlines():
        fields = line.split()
        if len(fields) == 8 and fields[6] == index:
            inside.add(fields[7])
    found = []
    section = ""
    for line in subprocess.run([tool("objdump"), "-r", obj], capture_output=True, text=True,
                               check=True).stdout.splitlines():
        m = re.match(r"RELOCATION RECORDS FOR \[(.*)\]", line)
        if m:
            section = m.group(1)
            continue
        fields = line.split()
        if len(fields) != 3 or fields[1] not in ABSOLUTE or section not in loaded:
            continue
        symbol = re.sub(r"[+-]0x[0-9a-f]+$", "", fields[2])
        if symbol not in inside:
            continue
        # Region functions are static: _ZL<length>aot_r12m in the object file.
        table = section != SECTION and fields[1] == "R_RISCV_32" and fields[2] == symbol and \
            re.fullmatch(r"(_ZL\d+)?aot_(m\d+_)?r\d+m?", symbol) is not None
        if not table:
            found.append(f"{section}+0x{fields[0].lstrip('0') or '0'}: {fields[1]} {fields[2]}")
    return found


def main() -> int:
    elf = sys.argv[1]
    obj = sys.argv[sys.argv.index("--object") + 1] if "--object" in sys.argv else None
    symbols = {}
    functions = []
    for line in subprocess.run([tool("nm"), "-S", "-C", "--defined-only", elf], capture_output=True, text=True,
                               check=True).stdout.splitlines():
        parts = line.split(None, 3)
        if len(parts) == 4 and parts[2] in "tT":
            functions.append((int(parts[0], 16), int(parts[1], 16), parts[3]))
        elif len(parts) == 3 and parts[1] in "tT":
            symbols[parts[2]] = int(parts[0], 16)
        if len(parts) == 4:
            symbols.setdefault(parts[3], int(parts[0], 16))
    if "aot_blob_begin" not in symbols or "aot_blob_end" not in symbols:
        print("no translated code in this firmware")
        return 0
    begin, end = symbols["aot_blob_begin"], symbols["aot_blob_end"]
    problems = []
    if end <= begin:
        problems.append(f"markers out of order: {begin:#x} .. {end:#x}")
    inside = outside = 0
    for address, size, name in functions:
        region = REGION.match(name) is not None
        if begin <= address < end:
            inside += 1
            if not region:
                problems.append(f"{name} at {address:#x} lies inside the translated code")
            elif address + size > end:
                problems.append(f"{name} runs past aot_blob_end")
        elif region:
            outside += 1
            problems.append(f"{name} at {address:#x} lies outside the translated code")

    listing = subprocess.run([tool("objdump"), "-d", "--no-show-raw-insn", f"--start-address={begin:#x}",
                              f"--stop-address={end:#x}", elf], capture_output=True, text=True,
                             check=True).stdout.splitlines()
    pending = None   # an auipc waiting for the instruction that completes its address
    count = 0
    lookalikes = []
    for line in listing:
        m = re.match(r"\s*([0-9a-f]+):\s+(\S+)\s*(.*)", line)
        if not m:
            continue
        count += 1
        pc, mnemonic, operands = int(m.group(1), 16), m.group(2), m.group(3)
        comment = re.search(r"#\s*([0-9a-f]+)", operands)
        if pending is not None:
            if comment is None:
                problems.append(f"{pending}: auipc with no resolved address")
            elif not begin <= int(comment.group(1), 16) < end:
                problems.append(f"{pending}: PC-relative reference to {comment.group(1)} outside the code")
            pending = None
            continue
        if mnemonic == "auipc":
            pending = f"{pc:#x}"
            continue
        if mnemonic.removeprefix("c.") in BRANCHES:
            target = re.search(r"([0-9a-f]+) <", operands)
            if target is None or not begin <= int(target.group(1), 16) < end:
                problems.append(f"{pc:#x}: {mnemonic} {operands} leaves the code")
            continue
        if comment is not None and begin <= int(comment.group(1), 16) < end:
            lookalikes.append(f"{pc:#x}: {mnemonic} {operands} holds an address of the code itself")

    if obj is None:
        problems += lookalikes
    else:
        problems += [f"{item}: an address of the code itself" for item in own_addresses(obj)]
        if lookalikes:
            print(f"{len(lookalikes)} constants that look like addresses of the code are guest data "
                  f"(no relocation)")

    print(f"translated code {begin:#x}..{end:#x}: {end - begin} bytes, {inside} functions, "
          f"{count} instructions checked")
    for problem in problems[:40]:
        print("  " + problem)
    if problems:
        print(f"{len(problems)} problems: this code must not be moved (build with -DAOT_PSRAM=0)")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
