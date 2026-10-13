"""Generate the port's person types from the game's own code and data.

PersonTypeFactory::setupPersonTypes (decomp/classes/PersonTypeFactory.c) is
9.7 KB of one pattern: a PersonType per character sheet, twelve
AnimationPairs each built from two AnimationIndexImage tables in .data, a
few speeds and timings, and the vector it is filed in. This reads that
function, reads every table it names out of the .so, and writes
"3DS Files/source/game/persontypes_data.cpp": the same construction in the
same order, with the tables as C arrays. Nothing is retyped by hand.

    python tools/gen_persontypes.py
"""
import re
import struct
import sys
from pathlib import Path

from disasm import GHIDRA_BASE, Library

ROOT = Path(__file__).resolve().parent.parent
DECOMP = ROOT / "decomp" / "classes" / "PersonTypeFactory.c"
OUT = ROOT / "3DS Files" / "source" / "game" / "persontypes_data.cpp"

# PersonType's AnimationPair slots (offsets in the original object).
SLOTS = {
    0x0c: "Walk",
    0x10: "Idle1",
    0x14: "Idle2",
    0x18: "Run",
    0x1c: "ZombieWalk",
    0x20: "Die",
    0x28: "Turn",
    0x30: "ZombieRise",
    0x38: "ZombieAttack",
    0x40: "Shoot",
    0x48: "ZombieDie1",
    0x4c: "ZombieDie2",
}

# PersonType's fields that setupPersonTypes sets: offset -> (member, kind).
FIELDS = {
    0x58: ("_walkSpeed", "float"),
    0x5c: ("_runSpeed", "float"),
    0x60: ("_zombieSpeedBase", "float"),
    0x68: ("_accuracy", "float"),
    0x6c: ("_armed", "bool"),
    0x6d: ("_rescuable", "bool"),
    0x70: ("_shootInterval", "int"),
    0x74: ("_searchInterval", "int"),
    0x78: ("_zombieSearchInterval", "int"),
    0x84: ("_halfTurnMs", "int"),
    0x90: ("_shootSound", "int"),
    0x94: ("_zombieAttackSound", "int"),
}


def float32_literal(raw):
    """The shortest decimal that reads back as the same float32, as a C++ literal."""
    bits = raw & 0xffffffff
    value = struct.unpack("<f", struct.pack("<I", bits))[0]
    for digits in range(1, 10):
        text = f"{value:.{digits}g}"
        if struct.unpack("<I", struct.pack("<f", float(text)))[0] == bits:
            break
    if "." not in text and "e" not in text:
        text += ".0"
    return text + "f"


def offset_of(lib, ghidra_addr):
    vaddr = ghidra_addr - GHIDRA_BASE
    for start, off, size in lib.segments:
        if start <= vaddr < start + size:
            return off + (vaddr - start)
    raise SystemExit(f"{ghidra_addr:08x} is not in a loaded segment")


def read_table(lib, addr, frames):
    """frames entries of {x, y, w, h, cellW, cellH}, then the x == -1 end."""
    off = offset_of(lib, addr)
    rows = [struct.unpack_from("<6i", lib.data, off + 24 * i) for i in range(frames + 1)]
    if rows[-1][0] != -1:
        raise SystemExit(f"table {addr:08x}: no terminator after {frames} frames")
    if any(r[0] == -1 for r in rows[:-1]):
        raise SystemExit(f"table {addr:08x}: ends before {frames} frames")
    return rows


def number(text):
    text = text.strip()
    return int(text, 16) if text.lower().startswith(("0x", "-0x")) else int(text)


def setup_body():
    src = DECOMP.read_text()
    start = src.index("PersonTypeFactory::setupPersonTypes(PersonTypeFactory *this,int param_1)")
    end = src.index("// ====", start)
    return src[start:end]


def parse(body):
    types = []
    cur = None
    pending_name = None
    pending_anim = None
    lines = body.splitlines()
    i = 0
    while i < len(lines):
        line = lines[i]
        if "operator_new(0x9c)" in line:
            cur = {"texture": None, "name": None, "anims": [], "fields": [], "lists": []}
            types.append(cur)
        elif cur is not None and (m := re.search(r'FUN_00492314\(\w+,"([^"]+)"', line)):
            if cur["texture"] is None:
                cur["texture"] = m.group(1)
            else:
                pending_name = m.group(1)
        elif cur is not None and (m := re.search(r'FUN_00491324\(pPVar2,"([^"]+)",\d+\)', line)):
            cur["name"] = m.group(1)
        elif "AnimationPair::AnimationPair" in line:
            text = line
            while ");" not in text:
                i += 1
                text += lines[i]
            m = re.search(r"AnimationPair\s*\(\s*\w+,\s*\w+,([^,]+),([^,]+),(true|false),"
                          r"\s*\(AnimationIndexImage \*\)&DAT_([0-9a-f]+),"
                          r"\s*\(AnimationIndexImage \*\)&DAT_([0-9a-f]+)\)", text)
            if not m:
                raise SystemExit(f"cannot parse: {text}")
            pending_anim = {
                "name": pending_name,
                "frames": number(m.group(1)),
                "duration": number(m.group(2)),
                "loop": m.group(3) == "true",
                "visible": int(m.group(4), 16),
                "hidden": int(m.group(5), 16),
            }
        elif pending_anim and (m := re.search(r"\(pPVar2 \+ 0x([0-9a-f]+)\),", line)):
            slot = int(m.group(1), 16)
            if slot not in SLOTS:
                raise SystemExit(f"unknown animation slot 0x{slot:x}")
            pending_anim["slot"] = SLOTS[slot]
            cur["anims"].append(pending_anim)
            pending_anim = None
        elif m := re.search(r"\*\(undefined4 \*\)\(pPVar2 \+ 0x([0-9a-f]+)\) = ([0-9a-fx]+);", line):
            cur["fields"].append((int(m.group(1), 16), number(m.group(2))))
        elif m := re.search(r"pPVar2\[0x([0-9a-f]+)\] = \(PersonType\)0x([0-9a-f]+);", line):
            cur["fields"].append((int(m.group(1), 16), int(m.group(2), 16)))
        elif "(this + 0x28) == *(undefined4 **)(this + 0x2c)" in line:
            cur["lists"].append("_civPersonTypes")
        elif "*(PersonType **)(this + 0x30) = pPVar2;" in line:
            cur["lists"].append("police")
        elif "*(PersonType **)(this + 0x34) = pPVar2;" in line:
            cur["lists"].append("soldier")
        elif "*(undefined4 **)(this + 4) == *(undefined4 **)(this + 8)" in line:
            cur["lists"].append("_allPersonTypes")
        i += 1
    return types


def field_value(kind, raw):
    if kind == "float":
        return float32_literal(raw)
    if kind == "bool":
        return "true" if raw else "false"
    return str(raw)


def main():
    lib = Library()
    types = parse(setup_body())
    if len(types) != 7:
        raise SystemExit(f"expected 7 person types, found {len(types)}")

    tables = {}
    for t in types:
        if len(t["anims"]) != len(SLOTS):
            raise SystemExit(f"{t['name']}: {len(t['anims'])} animations, expected {len(SLOTS)}")
        for a in t["anims"]:
            for key in ("visible", "hidden"):
                addr = a[key]
                rows = read_table(lib, addr, a["frames"])
                if addr in tables and tables[addr] != rows:
                    raise SystemExit(f"table {addr:08x} read two ways")
                tables[addr] = rows

    out = []
    out.append("// Generated from PersonTypeFactory::setupPersonTypes")
    out.append("// and the AnimationIndexImage tables in the game's .data. Do not edit.")
    out.append("")
    out.append('#include "game/persontype.h"')
    out.append("")
    out.append("namespace {")
    out.append("")
    for addr in sorted(tables):
        out.append(f"const AnimationIndexImage k{addr:08x}[] = {{")
        for r in tables[addr]:
            out.append("    {" + ", ".join(str(v) for v in r) + "},")
        out.append("};")
    out.append("")
    out.append("} // namespace")
    out.append("")
    out.append("void PersonTypeFactory::createPersonTypes()")
    out.append("{")
    out.append("    PersonType* t;")
    for t in types:
        out.append("")
        out.append(f'    t = new PersonType("{t["texture"]}");')
        out.append(f'    t->_name = "{t["name"]}";')
        for a in t["anims"]:
            loop = "true" if a["loop"] else "false"
            out.append(f'    t->setAnimation(PersonType::{a["slot"]}, new AnimationPair("{a["name"]}", '
                       f'{a["frames"]}, {a["duration"]}, {loop}, k{a["visible"]:08x}, k{a["hidden"]:08x}));')
        for off, raw in t["fields"]:
            if off not in FIELDS:
                raise SystemExit(f"{t['name']}: unknown field 0x{off:x}")
            member, kind = FIELDS[off]
            out.append(f"    t->{member} = {field_value(kind, raw)};")
        for lst in t["lists"]:
            if lst == "police":
                out.append("    _policePersonType = t;")
            elif lst == "soldier":
                out.append("    _soldierPersonType = t;")
            else:
                out.append(f"    {lst}.push_back(t);")
    out.append("}")
    out.append("")
    OUT.write_text("\n".join(out), newline="\n")
    print(f"{OUT.relative_to(ROOT)}: {len(types)} person types, {len(tables)} frame tables")


if __name__ == "__main__":
    sys.exit(main())
