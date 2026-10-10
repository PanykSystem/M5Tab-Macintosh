#!/usr/bin/env python3
"""Build a synthetic ROM and execution profile for testing aot_translate.py.

    make_test_rom.py --optable optable.tsv --rom test.rom --hot test-hot.txt \
        [--ram-profile test-ram.txt] [--seed N]

The repository cannot carry a Macintosh ROM, so the translator's regression
test runs on generated 68k code instead: for every opcode handler the
translator accepts, a few instances with different registers and random
extension words, laid out in short straight-line runs, plus hand-assembled
loops, branches and subroutine calls. test/test_aot.py translates this ROM and
runs the differential test (test/aot_host) over it.
"""

from __future__ import annotations

import argparse
import random
from collections import defaultdict
from pathlib import Path

import aot_translate as aot

ROM_SIZE = 0x40000
CODE_START = 0x400

# Hand-assembled control flow. Branch displacements are relative to the
# address after the opcode word.
SNIPPETS = [
    # MOVE.B (A0)+,(A1)+ ; DBF D0,*-2 ; RTS
    "12d8 51c8 fffc 4e75",
    # ADD.L D2,D2 ; SUBQ.W #1,D1 ; BNE.S *-4 ; RTS
    "d482 5341 66fa 4e75",
    # TST.L D0 ; BEQ.S +4 ; ADDQ.L #1,D0 ; ADDQ.L #1,D1 ; MOVEQ #5,D7 ; RTS
    "4a80 6704 5280 5281 7e05 4e75",
    # BSR.W +6 ; MOVEQ #1,D1 ; RTS ; ADDQ.L #1,D2 ; RTS
    "6100 0006 7201 4e75 5282 4e75",
    # MOVE.L (A0)+,(A1)+ ; SUBQ.L #1,D0 ; BGT.S *-4 ; MOVEQ #0,D0 ; RTS
    "22d8 5380 6efa 7000 4e75",
    # CMP.L (A0)+,D1 ; DBEQ D0,*-2 ; SEQ D3 ; RTS
    "b298 57c8 fffc 57c3 4e75",
    # MOVEM.L D3-D7/A2-A4,-(A7) ; MOVEM.L (A7)+,D3-D7/A2-A4 ; RTS
    "48e7 1f38 4cdf 1cf8 4e75",
    # LINK A6,#-8 ; MOVE.L D0,-4(A6) ; MOVE.L -4(A6),D1 ; UNLK A6 ; RTS
    "4e56 fff8 2d40 fffc 222e fffc 4e5e 4e75",
    # LEA 6(PC),A0 ; JMP (A0) ; MOVEQ #1,D0 ; MOVEQ #2,D0 ; RTS
    "41fa 0006 4ed0 7001 7002 4e75",
    # Nested loop: MOVEQ #3,D1 ; MOVEQ #5,D0 ; ADDQ.L #1,D2 ; DBF D0,*-2 ; DBF D1,*-8 ; RTS
    "7203 7005 5282 51c8 fffc 51c9 fff6 4e75",
    # MOVE.W 10(PC,D0.W*2),D1 ; ADD.W D1,D2 ; RTS, with D0 masked: ANDI.W #3,D0 first
    "0240 0003 323b 0206 d441 4e75 0011 0022 0033 0044",
]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--optable", type=Path, required=True)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--hot", type=Path, required=True)
    parser.add_argument("--ram-profile", type=Path,
                        help="also write the same code as a RAM code profile, as if it ran from RAM")
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--per-handler", type=int, default=3)
    parser.add_argument("--run-length", type=int, default=6)
    args = parser.parse_args()

    rng = random.Random(args.seed)
    optable = aot.load_optable(args.optable)
    handlers = aot.parse_cpuemu(aot.GENERATED / "cpuemu.cpp")
    handler_map = aot.build_handler_map(optable, aot.parse_cpustbl(aot.GENERATED / "cpustbl.cpp"))

    by_handler = defaultdict(list)
    for opcode, info in optable.items():
        if info.clev <= 4 and opcode in handler_map:
            by_handler[handler_map[opcode]].append(opcode)
    chosen = []
    for name in sorted(by_handler):
        opcodes = by_handler[name]
        chosen += rng.sample(opcodes, min(args.per_handler, len(opcodes)))
    rng.shuffle(chosen)

    rom = bytearray(rng.randrange(256) for _ in range(ROM_SIZE))
    starts = []
    off = CODE_START

    def place(words: str) -> None:
        nonlocal off
        data = bytes.fromhex(words.replace(" ", ""))
        rom[off:off + len(data)] = data
        # Every opcode word of a snippet that is an instruction start is
        # found by decoding; data words at the end are never reached.
        off += len(data)

    # Hand-assembled snippets first, each on its own and decoded linearly.
    snippet_ranges = []
    for words in SNIPPETS:
        off = (off + 15) & ~15
        begin = off
        place(words)
        snippet_ranges.append((begin, off))

    off = (off + 15) & ~15
    generated_start = off
    run = 0
    for opcode in chosen:
        if off + 64 >= ROM_SIZE:
            break
        # Random extension words; one instruction in four may get a full
        # extension word where it has an indexed operand.
        tail = bytearray()
        brief = rng.randrange(4) != 0
        for _ in range(7):
            word = rng.randrange(0x10000)
            if brief:
                word &= 0xFEFF
            tail += bytes([word >> 8, word & 0xFF])
        rom[off] = opcode >> 8
        rom[off + 1] = opcode & 0xFF
        rom[off + 2:off + 16] = tail
        tr = aot.Translator(bytes(rom), 0x40800000, optable, handlers, handler_map)
        result = tr.decode(off)
        if isinstance(result, aot.Skipped):
            continue
        starts.append(off)
        off += result.length
        run += 1
        if result.kind != "normal" or run >= args.run_length:
            if result.kind in ("normal", "bcc", "dbcc", "bsr", "jsr"):
                rom[off:off + 2] = b"\x4e\x75"   # RTS
                starts.append(off)
                off += 2
            off = (off + 3) & ~3
            run = 0
    rom[off:off + 2] = b"\x4e\x75"
    starts.append(off)

    # Instruction starts of the snippets: decode each linearly.
    tr = aot.Translator(bytes(rom), 0x40800000, optable, handlers, handler_map)
    snippet_starts = []
    for begin, end in snippet_ranges:
        position = begin
        pending = [begin]
        seen = set()
        while pending:
            position = pending.pop()
            while begin <= position < end and position not in seen:
                result = tr.decode(position)
                if isinstance(result, aot.Skipped):
                    raise SystemExit(f"snippet at {begin:#x}: {result.reason} at {position:#x}")
                seen.add(position)
                if result.kind in ("bcc", "dbcc", "bra", "bsr") and tr.contains(result.target):
                    pending.append(result.target - tr.base)
                if result.kind == "jmp":
                    # LEA 6(PC),A0 ; JMP (A0) lands on the second MOVEQ.
                    pending.append(position + 4)
                if result.kind in ("bra", "jmp", "rts", "dyn"):
                    break
                position += result.length
        snippet_starts += sorted(seen)

    args.rom.write_bytes(bytes(rom))
    lines = ["# synthetic test ROM: offset (hex) and execution count"]
    lines += [f"{start:x} 100000" for start in sorted(set(starts + snippet_starts))]
    args.hot.write_text("\n".join(lines) + "\n")
    if args.ram_profile:
        # The code as it would look had it been loaded into RAM at this
        # address. PC-relative branches land where they do in the ROM copy.
        load = 0x20000
        ram = [f"# synthetic RAM code profile, code loaded at {load:#x}"]
        ram += [f"C {load + start:x} 100000" for start in sorted(set(starts + snippet_starts))]
        for position in range(CODE_START & ~127, off + 144, 128):
            ram.append(f"B {load + position:x} {bytes(rom[position:position + 144]).hex()}")
        args.ram_profile.write_text("\n".join(ram) + "\n")
    print(f"{len(starts)} generated and {len(snippet_starts)} hand-assembled instructions, "
          f"code {generated_start:#x}..{off:#x}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
