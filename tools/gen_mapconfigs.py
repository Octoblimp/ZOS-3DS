"""Generate the port's map list from MapConfigFactory's constructor.

MapConfigFactory::MapConfigFactory (decomp/classes/MapConfigFactory.c) makes
every map the game knows: a MapConfig, a few fields set, pushed onto the
list. This reads those assignments and writes
"3DS Files/source/game/mapconfigs_data.cpp" in the same order.

    python tools/gen_mapconfigs.py
"""
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DECOMP = ROOT / "decomp" / "classes" / "MapConfigFactory.c"
OUT = ROOT / "3DS Files" / "source" / "game" / "mapconfigs_data.cpp"

# MapConfig's fields (game/mapconfig.h): offset -> (member, kind).
FIELDS = {
    0x00: ("id", "int"),
    0x04: ("type", "type"),
    0x08: ("worldX", "int"),
    0x0c: ("worldY", "int"),
    0x10: ("width", "int"),
    0x14: ("height", "int"),
    0x18: ("startX", "int"),
    0x1c: ("startY", "int"),
    0x20: ("pinX", "float"),
    0x24: ("pinY", "float"),
    0x28: ("zombieOutbreak", "int"),
    0x2c: ("populationLimit", "int"),
    0x30: ("free", "bool"),
    0x31: ("available", "bool"),
    0x32: ("hasPresetOptions", "bool"),
}
TYPES = {1: "Google", 2: "Embedded", 3: "Player"}


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


def value(kind, raw):
    if kind == "float":
        return float32_literal(raw)
    if kind == "bool":
        return "true" if raw else "false"
    if kind == "type":
        return f"MapConfig::MapType::{TYPES[raw]}"
    return str(raw)


def main():
    src = DECOMP.read_text()
    start = src.index("MapConfigFactory::MapConfigFactory(MapConfigFactory *this)")
    body = src[start:src.index("// ====", start)]
    maps = []
    for line in body.splitlines():
        if "operator_new(0x4c)" in line:
            maps.append([])
        elif m := re.search(r"\*\(undefined4 \*\)pMVar1 = ([0-9a-fx]+);", line):
            maps[-1].append((0, int(m.group(1), 0)))
        elif m := re.search(r"\*\(undefined4 \*\)\(pMVar1 \+ (0x[0-9a-f]+|\d+)\) = ([0-9a-fx]+);", line):
            maps[-1].append((int(m.group(1), 0), int(m.group(2), 0)))
        elif m := re.search(r"pMVar1\[0x([0-9a-f]+)\] = \(MapConfig\)0x([0-9a-f]+);", line):
            maps[-1].append((int(m.group(1), 16), int(m.group(2), 16)))
        elif " = " in line and "pMVar1" in line.split(" = ")[0] and "pMVar1 = operator_new" not in line:
            # A store into the MapConfig this does not understand.
            raise SystemExit(f"unparsed: {line.strip()}")

    out = [
        "// Generated from MapConfigFactory::MapConfigFactory.",
        "// Do not edit.",
        "",
        '#include "game/mapconfigfactory.h"',
        "",
        "void MapConfigFactory::createMaps()",
        "{",
        "    MapConfig* m;",
    ]
    for fields in maps:
        out.append("")
        out.append("    m = new MapConfig();")
        for off, raw in fields:
            if off not in FIELDS:
                raise SystemExit(f"unknown MapConfig field 0x{off:x}")
            member, kind = FIELDS[off]
            out.append(f"    m->{member} = {value(kind, raw)};")
        out.append("    _maps.push_back(m);")
    out.append("}")
    out.append("")
    OUT.write_text("\n".join(out), newline="\n")
    kinds = {}
    for fields in maps:
        t = dict(fields).get(4)
        kinds[TYPES[t]] = kinds.get(TYPES[t], 0) + 1
    print(f"{OUT.relative_to(ROOT)}: {len(maps)} maps {kinds}")


if __name__ == "__main__":
    sys.exit(main())
