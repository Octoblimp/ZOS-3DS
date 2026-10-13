"""Read the game's .so data at a Ghidra address: strings, words, floats.

The decompilation names data by address (DAT_005ab34c, &DAT_004fd436), and
the port needs the values. Addresses are Ghidra's (ELF + 0x10000), as
decomp/ prints them. A pointer stored in .data holds an ELF address (the
R_ARM_RELATIVE addend, in place); `ptr` follows it.

    python tools/sodata.py str 004fd436          the C string there
    python tools/sodata.py ptr 005ab34c          the string a pointer points to
    python tools/sodata.py words 0058c004 12     12 little-endian u32s (and s32)
    python tools/sodata.py floats 00507bf0 4     4 floats
    python tools/sodata.py bytes 00507bef 16     hex dump
"""
import argparse
import struct
import sys

from disasm import GHIDRA_BASE, Library


def offset_of(lib, ghidra_addr):
    vaddr = ghidra_addr - GHIDRA_BASE
    for start, off, size in lib.segments:
        if start <= vaddr < start + size:
            return off + (vaddr - start)
    raise SystemExit(f"{ghidra_addr:08x} is not in a loaded segment")


def c_string(lib, ghidra_addr):
    off = offset_of(lib, ghidra_addr)
    end = lib.data.index(b"\0", off)
    return lib.data[off:end].decode("latin-1")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("kind", choices=["str", "ptr", "words", "floats", "bytes"])
    ap.add_argument("addr")
    ap.add_argument("count", nargs="?", type=int, default=1)
    args = ap.parse_args()
    lib = Library()
    addr = int(args.addr, 16)

    if args.kind == "str":
        print(repr(c_string(lib, addr)))
    elif args.kind == "ptr":
        for i in range(args.count):
            off = offset_of(lib, addr + 4 * i)
            target = struct.unpack_from("<I", lib.data, off)[0] + GHIDRA_BASE
            print(f"{addr + 4 * i:08x} -> {target:08x} {c_string(lib, target)!r}")
    elif args.kind == "words":
        off = offset_of(lib, addr)
        for i in range(args.count):
            u = struct.unpack_from("<I", lib.data, off + 4 * i)[0]
            s = struct.unpack_from("<i", lib.data, off + 4 * i)[0]
            print(f"{addr + 4 * i:08x}  0x{u:08x}  {s}")
    elif args.kind == "floats":
        off = offset_of(lib, addr)
        for i in range(args.count):
            print(f"{addr + 4 * i:08x}  {struct.unpack_from('<f', lib.data, off + 4 * i)[0]!r}")
    else:
        off = offset_of(lib, addr)
        chunk = lib.data[off:off + args.count]
        for i in range(0, len(chunk), 16):
            row = chunk[i:i + 16]
            print(f"{addr + i:08x}  {row.hex(' '):48s}  {''.join(chr(b) if 32 <= b < 127 else '.' for b in row)}")


if __name__ == "__main__":
    sys.exit(main())
