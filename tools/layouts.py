"""The game's class layouts: what the code says about each class's fields.

tools/ghidra_scripts/CollectFieldAccesses.java writes every access the game
makes to a field of one of its classes into decomp/layouts/_accesses.tsv.
This tool reads that and summarises it per class, as the evidence for the
hand-written layouts in decomp/layouts.h.

    python tools/layouts.py summary Person      one class, field by field
    python tools/layouts.py summary --all       every class, into decomp/layouts/
    python tools/layouts.py draft Person        a first layouts.h entry, f_<offset> names
    python tools/layouts.py check               decomp/layouts.h against the evidence
"""
import argparse
import collections
import csv
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LAYOUT_DIR = ROOT / "decomp" / "layouts"
ACCESSES = LAYOUT_DIR / "_accesses.tsv"
SIZES = LAYOUT_DIR / "_sizes.tsv"
HEADER = ROOT / "decomp" / "layouts.h"


def read_tsv(path):
    with open(path, newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f, delimiter="\t"))


def as_float(value, size):
    """A stored constant as a float, if it reads as a plausible one."""
    if size != 4 or not value.startswith("0x"):
        return None
    bits = int(value, 16) & 0xFFFFFFFF
    if bits in (0, 1) or bits < 0x00800000:
        return None
    f = struct.unpack("<f", struct.pack("<I", bits))[0]
    if f != f or abs(f) > 1e9 or (abs(f) < 1e-6 and f != 0):
        return None
    return f


def describe_value(value, size):
    if not value.startswith("0x"):
        return value
    v = int(value, 16)
    f = as_float(value, size)
    signed = v - (1 << (8 * size)) if size and v >= 1 << (8 * size - 1) else v
    out = str(signed) if abs(signed) < 0x10000 else value
    if f is not None:
        out += f" ({f:g}f)"
    return out


def summarise(rows, sizes):
    """Per offset: how it is read and written, how often, and by whom."""
    by_offset = collections.defaultdict(list)
    for r in rows:
        by_offset[int(r["offset"], 16)].append(r)
    lines = []
    for size in sorted(set(sizes)):
        lines.append(f"// size {size} (operator new)")
    for off in sorted(by_offset):
        rs = by_offset[off]
        funcs = sorted({r["function"] for r in rs})
        kinds = collections.Counter()
        values = collections.Counter()
        calls = collections.Counter()
        for r in rs:
            k = r["kind"]
            if k.startswith("arg"):
                calls[f"{r['value']}({k}: {r['type']})"] += 1
            else:
                kinds[f"{k} {r['size']} {r['type']}".rstrip()] += 1
                if k == "store" and r["value"]:
                    values[describe_value(r["value"], int(r["size"]))] += 1
        parts = [f"{k} x{n}" for k, n in kinds.most_common()]
        line = f"0x{off:04x}  " + "; ".join(parts)
        if values:
            line += "  = " + ", ".join(v for v, _ in values.most_common(6))
        lines.append(line)
        for c, n in calls.most_common(4):
            lines.append(f"        passed to {c} x{n}")
        shown = ", ".join(f.split("::")[-1] if f.count("::") == 1 else f for f in funcs[:6])
        lines.append(f"        in {len(funcs)}: {shown}{' ...' if len(funcs) > 6 else ''}")
    return lines


def cmd_summary(args):
    rows = read_tsv(ACCESSES)
    sizes = collections.defaultdict(list)
    for r in read_tsv(SIZES):
        sizes[r["class"]].append(r["size"])
    by_class = collections.defaultdict(list)
    for r in rows:
        by_class[r["class"]].append(r)
    names = sorted(by_class) if args.all else args.classes
    for name in names:
        if name not in by_class:
            print(f"{name}: no accesses", file=sys.stderr)
            continue
        text = "\n".join([f"// {name}: field accesses ({len(by_class[name])})"]
                         + summarise(by_class[name], sizes.get(name, []))) + "\n"
        if args.all:
            (LAYOUT_DIR / (name.replace("::", "__") + ".txt")).write_text(text, encoding="utf-8")
        else:
            sys.stdout.write(text)
    if args.all:
        print(f"wrote {len(names)} summaries to {LAYOUT_DIR}")


# decomp/layouts.h: `struct Name : Base {  // size 0xNN` then one field per
# line, `0xOFF type name;` with an optional `// comment`, then `};`.
STRUCT_RE = re.compile(r"^struct\s+([\w:]+)(?:\s*:\s*([\w:<>, *]+?))?\s*\{\s*//\s*size\s+(0x[0-9a-fA-F]+)")
FIELD_RE = re.compile(r"^\s*(0x[0-9a-fA-F]+)\s+(.+?)\s+(\w+)(\[[^\]]+\])?;")


def parse_header(path=HEADER):
    structs = {}
    cur = None
    for n, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        s = line.strip()
        m = STRUCT_RE.match(s)
        if m:
            cur = {"name": m.group(1), "base": m.group(2), "size": int(m.group(3), 16), "fields": [], "line": n}
            structs[cur["name"]] = cur
            continue
        if s.startswith("};"):
            cur = None
            continue
        if cur is not None:
            m = FIELD_RE.match(line)
            if m:
                cur["fields"].append({"offset": int(m.group(1), 16), "type": m.group(2),
                                      "name": m.group(3), "array": m.group(4), "line": n})
    return structs


PRIMITIVE = {"float": "float", "double": "double", "int": "int", "uint": "uint", "bool": "bool",
             "char": "char", "byte": "uchar", "short": "short", "ushort": "ushort", "longlong": "int64",
             "ulonglong": "uint64", "undefined1": "uchar", "undefined2": "ushort", "undefined4": "uint",
             "undefined8": "uint64", "char *": "char*"}


def guess_type(rs, size):
    """The likeliest type for a field from its loads and stores."""
    votes = collections.Counter()
    for r in rs:
        if r["kind"].startswith("arg") or int(r["size"] or 0) != size:
            continue
        t = r["type"].strip()
        if t.startswith("undefined") or t in ("?", ""):
            votes[PRIMITIVE.get(t, "uint")] += 0.1
        elif t.endswith("*"):
            base = t[:-1].strip()
            votes[(base.split("<")[0].replace(" ", "") + "*") if base not in ("void", "undefined") else "void*"] += 1
        else:
            votes[PRIMITIVE.get(t, t if size >= 4 else {1: "uchar", 2: "ushort"}[size])] += 1
    if not votes:
        return {1: "uchar", 2: "ushort", 4: "uint", 8: "uint64"}.get(size, f"uchar[{size}]")
    return votes.most_common(1)[0][0]


def cmd_draft(args):
    """A first layouts.h entry for a class, from the evidence. Every field is
    named f_<offset> and wants a real name from reading the code."""
    rows = [r for r in read_tsv(ACCESSES) if r["class"] == args.cls]
    sizes = {int(r["size"], 16) for r in read_tsv(SIZES) if r["class"] == args.cls}
    by_offset = collections.defaultdict(list)
    for r in rows:
        by_offset[int(r["offset"], 16)].append(r)
    fields = []
    covered_to = 0
    for off in sorted(by_offset):
        rs = by_offset[off]
        if off < covered_to:
            continue
        embedded = [r for r in rs if r["kind"].startswith("arg") and off != 0]
        access = [int(r["size"]) for r in rs if not r["kind"].startswith("arg") and int(r["size"] or 0) > 0]
        note = ""
        if embedded and not access:
            callee = collections.Counter(r["value"] for r in embedded).most_common(1)[0][0]
            fields.append((off, "?", f"f_{off:x}", f"passed to {callee}"))
            continue
        if not access:
            continue
        size = collections.Counter(access).most_common(1)[0][0]
        typ = guess_type(rs, size)
        values = [describe_value(r["value"], size) for r in rs if r["kind"] == "store" and r["value"]]
        if values:
            note = "= " + ", ".join(sorted(set(values))[:4])
        fields.append((off, typ, f"f_{off:x}", note))
        covered_to = off + size
    total = max(sizes) if sizes else (covered_to + 3) & ~3
    print(f"struct {args.cls} {{  // size 0x{total:x}")
    for off, typ, name, note in fields:
        line = f"    0x{off:03x} {typ} {name};"
        print(line + (f"  // {note}" if note else ""))
    print("};")


def cmd_check(args):
    structs = parse_header()
    rows = read_tsv(ACCESSES)
    sizes = collections.defaultdict(set)
    for r in read_tsv(SIZES):
        sizes[r["class"]].add(int(r["size"], 16))
    problems = 0
    for name, s in structs.items():
        if sizes.get(name) and s["size"] not in sizes[name]:
            print(f"{name}: size 0x{s['size']:x}, but operator new says {sorted(hex(x) for x in sizes[name])}")
            problems += 1
        offs = [f["offset"] for f in s["fields"]]
        if offs != sorted(offs):
            print(f"{name}: fields out of order")
            problems += 1
        if len(set(offs)) != len(offs):
            print(f"{name}: two fields at one offset")
            problems += 1
        if any(o >= s["size"] for o in offs):
            print(f"{name}: a field past the end")
            problems += 1
    print(f"{len(structs)} structs, {problems} problems")
    return 1 if problems else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("summary")
    p.add_argument("classes", nargs="*")
    p.add_argument("--all", action="store_true")
    sub.add_parser("check")
    p = sub.add_parser("draft")
    p.add_argument("cls")
    args = ap.parse_args()
    if args.cmd == "summary":
        cmd_summary(args)
    elif args.cmd == "draft":
        cmd_draft(args)
    elif args.cmd == "check":
        sys.exit(cmd_check(args))


if __name__ == "__main__":
    main()
