"""Disassemble the game's .so (Thumb-2) at a Ghidra address or a symbol.

Ghidra loaded the .so at image base 0x10000, so its addresses are the ELF's
plus 0x10000. This tool takes Ghidra's (as decomp/ prints them) unless
--elf is given.

    python tools/disasm.py 001ae374 40          40 instructions from there
    python tools/disasm.py Simulation::calculateNewTarget
"""
import argparse
import bisect
import subprocess
import sys

import capstone
from elftools.elf.elffile import ELFFile

import apk
from pathlib import Path

GHIDRA_BASE = 0x10000


class Library:
    def __init__(self):
        apk.extracted()
        self.path = Path(apk.SO_V7A)
        self.data = self.path.read_bytes()
        with open(self.path, "rb") as f:
            elf = ELFFile(f)
            self.segments = [(s["p_vaddr"], s["p_offset"], s["p_filesz"])
                             for s in elf.iter_segments() if s["p_type"] == "PT_LOAD"]
            self.symbols = {}
            for sec in ("dynsym", ".dynsym"):
                t = elf.get_section_by_name(sec)
                if t is None:
                    continue
                for s in t.iter_symbols():
                    if s["st_value"] and s.name:
                        self.symbols[s.name] = (s["st_value"] & ~1, s["st_size"], s["st_info"]["type"])
        by_addr = sorted((v[0], n) for n, v in self.symbols.items() if v[2] == "STT_FUNC")
        self.addrs = [a for a, _ in by_addr]
        self.names = [n for _, n in by_addr]

    def read(self, vaddr, size):
        for va, off, sz in self.segments:
            if va <= vaddr < va + sz:
                return self.data[off + vaddr - va: off + vaddr - va + size]
        raise ValueError(f"0x{vaddr:x} is not in a loaded segment")

    def symbol_at(self, vaddr):
        i = bisect.bisect_right(self.addrs, vaddr) - 1
        if i < 0:
            return None
        return self.names[i], vaddr - self.addrs[i]


def demangle(names):
    try:
        out = subprocess.run(["arm-none-eabi-c++filt"], input="\n".join(names), capture_output=True, text=True).stdout
        return out.splitlines()
    except FileNotFoundError:
        return names


def find_symbol(lib, wanted):
    mangled = list(lib.symbols)
    for m, d in zip(mangled, demangle(mangled)):
        if d.split("(")[0] == wanted or m == wanted:
            return lib.symbols[m]
    raise SystemExit(f"no symbol {wanted}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("where")
    ap.add_argument("count", nargs="?", type=int, default=0)
    ap.add_argument("--elf", action="store_true", help="the address is the ELF's, not Ghidra's")
    args = ap.parse_args()

    lib = Library()
    try:
        addr = int(args.where, 16)
        if not args.elf:
            addr -= GHIDRA_BASE
        size = None
    except ValueError:
        addr, size, _ = find_symbol(lib, args.where)
    count = args.count or (0 if size else 40)
    nbytes = size if size and not args.count else count * 4

    md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB)
    code = lib.read(addr, nbytes)
    targets = []
    lines = []
    for n, ins in enumerate(md.disasm(code, addr)):
        if args.count and n >= count:
            break
        note = ""
        if ins.mnemonic in ("bl", "blx") and ins.op_str.startswith("#"):
            t = int(ins.op_str[1:], 16)
            s = lib.symbol_at(t)
            if s:
                targets.append(s[0])
                note = s[0] + (f"+0x{s[1]:x}" if s[1] else "")
        lines.append((ins.address + GHIDRA_BASE, ins.mnemonic, ins.op_str, note))
    pretty = dict(zip(targets, demangle(targets)))
    for a, m, o, note in lines:
        print(f"{a:08x}  {m:8} {o}" + (f"    ; {pretty.get(note.split('+')[0], note)}" if note else ""))


if __name__ == "__main__":
    sys.exit(main())
