"""The game classes' vtables and base classes, read from the .so's RTTI.

The .so keeps its vtables (_ZTV...) and type_info objects (_ZTI...) in the
dynamic symbol table. A vtable slot is a 32-bit word with an R_ARM_ABS32
relocation naming the function (or R_ARM_RELATIVE to an address), so each
slot resolves to a symbol. A type_info says what kind of class it is
(__class_type_info: no base; __si_class_type_info: one public base;
__vmi_class_type_info: several) and points to the bases' type_infos.

Writes decomp/vtables.txt: per game class, its bases and every vtable slot
(byte offset from the vptr, the function, and which class defines it). The
slots name the virtual calls in the decompilation (`(**(code **)(*p + 0x5c))`
is slot 0x5c), and a function in a game class's slots is a method, not
static.

    python tools/vtables.py                write decomp/vtables.txt
    python tools/vtables.py GameNode       one class, to stdout
"""
import argparse
import struct
import subprocess
import sys
from pathlib import Path

from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection

import apk

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "decomp" / "vtables.txt"
CLASSES = ROOT / "decomp" / "classes.txt"

R_ARM_ABS32 = 2
R_ARM_RELATIVE = 23


def demangle_all(names, types=False):
    names = list(names)
    if not names:
        return {}
    cmd = ["arm-none-eabi-c++filt"] + (["-t"] if types else [])
    out = subprocess.run(cmd, input="\n".join(names), capture_output=True, text=True).stdout.splitlines()
    return dict(zip(names, out))


class SO:
    def __init__(self):
        apk.extracted()
        self.data = Path(apk.SO_V7A).read_bytes()
        f = open(apk.SO_V7A, "rb")
        elf = ELFFile(f)
        self.segments = [(s["p_vaddr"], s["p_offset"], s["p_filesz"])
                         for s in elf.iter_segments() if s["p_type"] == "PT_LOAD"]
        dynsym = elf.get_section_by_name(".dynsym")
        self.syms = list(dynsym.iter_symbols())
        self.by_name = {s.name: s for s in self.syms if s.name}
        self.by_addr = {}
        for s in self.syms:
            if s.name and s["st_value"]:
                self.by_addr.setdefault(s["st_value"] & ~1, s.name)
        self.relocs = {}
        for sec in elf.iter_sections():
            if isinstance(sec, RelocationSection) and sec.name == ".rel.dyn":
                for r in sec.iter_relocations():
                    sym = self.syms[r["r_info_sym"]].name if r["r_info_sym"] else None
                    self.relocs[r["r_offset"]] = (r["r_info_type"], sym)

    def word(self, vaddr):
        for va, off, sz in self.segments:
            if va <= vaddr < va + sz:
                return struct.unpack_from("<I", self.data, off + vaddr - va)[0]
        return 0

    def pointer(self, vaddr):
        """What the word at vaddr points to after relocation: a symbol name,
        or a raw address."""
        kind, sym = self.relocs.get(vaddr, (None, None))
        value = self.word(vaddr)
        if kind == R_ARM_ABS32 and sym:
            return sym if value == 0 else f"{sym}+0x{value:x}"
        if kind == R_ARM_RELATIVE or value:
            return self.by_addr.get(value & ~1, f"0x{value:x}") if value else "0"
        return "0"


def class_name(mangled_ztv):
    return mangled_ztv[4:]


def read_typeinfo(so, ti_name, dem):
    """[(base type_info symbol, offset)] for a class's type_info."""
    s = so.by_name.get(ti_name)
    if s is None:
        return None, []
    a = s["st_value"]
    kind = so.pointer(a)
    if "__si_class_type_info" in kind:
        return "si", [(so.pointer(a + 8), 0)]
    if "__vmi_class_type_info" in kind:
        n = so.word(a + 12)
        bases = []
        for i in range(n):
            base = so.pointer(a + 16 + 8 * i)
            flags = so.word(a + 20 + 8 * i)
            bases.append((base, flags >> 8))
        return "vmi", bases
    return "none", []


def read_vtable(so, ztv):
    """Slots of a vtable as [(offset from the address point, target)],
    split into primary and secondary tables."""
    s = so.by_name[ztv]
    a, size = s["st_value"], s["st_size"]
    tables = []
    i = 0
    while i < size:
        top = struct.unpack("<i", struct.pack("<I", so.word(a + i)))[0]
        ti = so.pointer(a + i + 4)
        slots = []
        j = i + 8
        while j < size:
            # A secondary table starts with offset-to-top then a type_info.
            nxt = so.pointer(a + j + 4) if j + 4 < size else ""
            if nxt.startswith("_ZTI") and j + 8 <= size and so.pointer(a + j) in ("0",) or \
               (nxt.startswith("_ZTI") and struct.unpack("<i", struct.pack("<I", so.word(a + j)))[0] < 0):
                break
            slots.append((j - (i + 8), so.pointer(a + j)))
            j += 4
        tables.append((top, ti, slots))
        i = j
    return tables


def game_classes():
    names = []
    for line in CLASSES.read_text(encoding="utf-8").splitlines():
        if line and not line.startswith("#"):
            names.append(line.split("\t")[0])
    return names


def mangle_simple(name):
    parts = name.split("::")
    if len(parts) == 1:
        return f"{len(name)}{name}"
    return "N" + "".join(f"{len(p)}{p}" for p in parts) + "E"


def describe(so, cls, dem):
    m = mangle_simple(cls)
    lines = []
    kind, bases = read_typeinfo(so, "_ZTI" + m, dem)
    if kind is None:
        return [f"{cls}: no type_info (no virtual functions)"]
    base_text = ", ".join(dem.get(b[4:] if b.startswith("_ZTI") else b, b)
                          + (f" at +0x{off:x}" if off else "") for b, off in bases) or "none"
    lines.append(f"{cls}: bases {base_text}")
    ztv = "_ZTV" + m
    if ztv not in so.by_name:
        lines.append("  (no vtable)")
        return lines
    for top, ti, slots in read_vtable(so, ztv):
        lines.append(f"  vtable (offset to top {top}):")
        for off, target in slots:
            lines.append(f"    0x{off:03x}  {dem.get(target, target)}")
    return lines


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("classes", nargs="*")
    args = ap.parse_args()
    so = SO()
    names = args.classes or game_classes()
    wanted = set()
    for cls in names:
        m = mangle_simple(cls)
        if "_ZTV" + m in so.by_name:
            for _, _, slots in read_vtable(so, "_ZTV" + m):
                wanted.update(t.split("+")[0] for _, t in slots)
        kind, bases = read_typeinfo(so, "_ZTI" + m, {})
        wanted.update(b[4:] if b.startswith("_ZTI") else b for b, _ in bases)
    dem = demangle_all(sorted(w for w in wanted if w.startswith("_Z")))
    # A base is named by its type_info's mangled type (_ZTI stripped).
    dem.update(demangle_all(sorted(w for w in wanted if not w.startswith("_Z")), types=True))
    out = []
    for cls in names:
        out.extend(describe(so, cls, dem))
        out.append("")
    text = "\n".join(out)
    if args.classes:
        sys.stdout.write(text)
    else:
        header = ("# The game classes' bases and vtables, from the .so's RTTI (tools/vtables.py).\n"
                  "# Slot offsets are from the vptr: a virtual call (**(code **)(*p + 0x5c))(p, ...)\n"
                  "# calls slot 0x5c. Each slot names the function the class's vtable holds.\n\n")
        OUT.write_text(header + text, encoding="utf-8")
        print(f"wrote {OUT}")


if __name__ == "__main__":
    main()
