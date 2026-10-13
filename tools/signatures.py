"""The game methods' real signatures: static or not, and what they return.

A mangled name gives a method's parameters but not its return type, and it
mangles a static method the same as an instance one (only `const` shows, as
K). Ghidra's demangler guesses, and it guessed static for a few hundred of
the game's methods, so `this` became the first parameter and every argument
after it moved up a register.

tools/ghidra_scripts/DumpFunctions.java (python tools/ghidra.py functions)
lets Ghidra's decompiler recover each method's inputs from its code alone and
writes decomp/layouts/_functions.tsv. This tool lines that up with the
mangled parameters, counted in AAPCS softfp argument words:

    words(static)  = the parameters
    words(method)  = 1 (this) + the parameters
    + 1 at the front if the result is a class returned through a hidden
      pointer (r0 = where to put it, then this in r1)

A method whose code reads more words than its parameters fill takes `this`
(or a hidden result pointer). One that reads exactly its parameters, starting
at r0, is static. The rest (a method that ignores `this` and its last
argument reads the same as a static one) are decided by hand in
decomp/signatures.txt, and so are the return types.

    python tools/signatures.py report            the classification, ambiguous first
    python tools/signatures.py draft > x.txt     a first decomp/signatures.txt
    python tools/signatures.py check             decomp/signatures.txt against the evidence
"""
import argparse
import collections
import csv
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FUNCTIONS = ROOT / "decomp" / "layouts" / "_functions.tsv"
VTABLES = ROOT / "decomp" / "vtables.txt"
CLASSES = ROOT / "decomp" / "classes.txt"
SIGNATURES = ROOT / "decomp" / "signatures.txt"

CXXFILT = shutil.which("arm-none-eabi-c++filt") or "C:/devkitPro/devkitARM/bin/arm-none-eabi-c++filt.exe"


def demangle_all(names):
    names = list(names)
    if not names:
        return {}
    out = subprocess.run([CXXFILT], input="\n".join(names), capture_output=True, text=True).stdout.splitlines()
    return dict(zip(names, out))


def split_params(text):
    """'(a, b<c, d>, e)' -> ['a', 'b<c, d>', 'e']"""
    depth = 0
    cur = ""
    out = []
    for ch in text:
        if ch in "<(":
            depth += 1
        elif ch in ">)":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return [p for p in out if p != "void"]


def parse_demangled(d):
    """'Simulation::update(cocos2d::Rect const&, float) const' ->
    ('Simulation::update', ['cocos2d::Rect const&', 'float'], True)"""
    is_const = d.endswith(" const")
    if is_const:
        d = d[:-6]
    depth = 0
    for i in range(len(d) - 1, -1, -1):
        if d[i] == ")":
            depth += 1
        elif d[i] == "(":
            depth -= 1
            if depth == 0:
                return d[:i], split_params(d[i + 1:-1]), is_const
    return d, [], is_const


# Classes passed or returned by value, and their sizes. A class that is not
# trivially copyable (std::string, std::function, cocos2d::Vector) goes by
# invisible reference: one word.
VALUE_SIZES = {
    "cocos2d::Vec2": 8, "cocos2d::Size": 8, "cocos2d::Rect": 16, "cocos2d::Vec3": 12,
    "cocos2d::Color3B": 3, "cocos2d::Color4B": 4, "cocos2d::Color4F": 16,
}
WIDE = {"double", "long long", "unsigned long long", "long double"}


def param_words(params):
    """Argument words the parameters take, with doubleword alignment."""
    n = 0
    for p in params:
        t = p.strip()
        if t.endswith("&") or t.endswith("*"):
            n += 1
        elif t in WIDE:
            n = (n + 1) & ~1
            n += 2
        elif t in VALUE_SIZES:
            n += (VALUE_SIZES[t] + 3) // 4
        else:
            n += 1
    return n


def input_words(inputs):
    """Words read on entry: r0..r3 are 0..3, the stack slot at 4k is 4+k."""
    hi = 0
    regs = set()
    for item in inputs.split(","):
        if not item or item == "?":
            continue
        where = item.split("/")[0]
        for part in where.split("+"):
            m = re.fullmatch(r"r([0-3])", part)
            if m:
                regs.add(int(m.group(1)))
                hi = max(hi, int(m.group(1)) + 1)
                continue
            m = re.fullmatch(r"s(\d+)", part)
            if m:
                hi = max(hi, 4 + int(m.group(1)) // 4 + 1)
    return hi, regs


def virtual_methods():
    """Qualified names (with parameters) that sit in a game class's vtable."""
    out = set()
    if not VTABLES.exists():
        return out
    for line in VTABLES.read_text(encoding="utf-8").splitlines():
        m = re.match(r"\s+0x[0-9a-f]+\s+(.+)$", line)
        if m:
            out.add(m.group(1).strip())
    return out


def load():
    rows = list(csv.DictReader(open(FUNCTIONS, encoding="utf-8", newline=""), delimiter="\t"))
    dropped = set()
    for line in CLASSES.read_text(encoding="utf-8").splitlines():
        cols = line.split("\t")
        if len(cols) > 3 and cols[3] == "dropped":
            dropped.add(cols[0])
    syms = [r["symbols"].split()[0] for r in rows if r["symbols"]]
    dem = demangle_all(syms)
    virt = virtual_methods()
    out = []
    for r in rows:
        if r["class"] in dropped or not r["symbols"]:
            continue
        sym = r["symbols"].split()[0]
        full = dem.get(sym, sym)
        name, params, is_const = parse_demangled(full)
        # A thunk for a secondary base (_ZThn576_...) is a method by definition.
        is_thunk = sym.startswith("_ZTh")
        hi, regs = input_words(r["inputs"])
        words = param_words(params)
        is_ctor = name.split("::")[-1] == name.split("::")[-2] if "::" in name else False
        is_dtor = name.split("::")[-1].startswith("~")
        is_virtual = full in virt or full.replace("non-virtual thunk to ", "") in virt
        out.append(dict(r, sym=sym, full=full, qname=name, params=params, const=is_const, words=words,
                        read=hi, regs=regs, ctor=is_ctor, dtor=is_dtor, virtual=is_virtual, thunk=is_thunk))
    return out


def classify(f):
    """('method' | 'static' | 'method?' | 'static?', why)"""
    if f["ctor"] or f["dtor"]:
        return "method", "constructor/destructor"
    if f["const"]:
        return "method", "const"
    if f["virtual"] or f["thunk"]:
        return "method", "virtual"
    if f["read"] > f["words"]:
        return "method", f"reads {f['read']} words, parameters fill {f['words']}"
    if f["read"] == f["words"] and f["words"] > 0 and 0 in f["regs"]:
        return "static?", f"reads exactly its {f['words']} parameter words"
    if f["read"] == 0 and f["words"] == 0:
        return "static?", "reads nothing"
    return "method?", f"reads {f['read']} of {f['words']} parameter words"


def cmd_report(args):
    fs = load()
    groups = collections.defaultdict(list)
    for f in fs:
        kind, why = classify(f)
        groups[kind].append((f, why))
    for kind in ("static?", "method?", "static", "method"):
        items = groups.get(kind, [])
        print(f"== {kind}: {len(items)}")
        if kind == "method" and not args.all:
            continue
        for f, why in items:
            print(f"  {f['address']}  {f['full']}  -- {why}; now {f['cc']}")


def guess_return(f):
    """A first guess at the return type, for the draft."""
    res = f["result"]
    name = f["qname"].split("::")[-1]
    cls = f["class"]
    if f["ctor"] or f["dtor"] or res == "void":
        return "void"
    if name == "create":
        return cls + "*"
    if re.match(r"(is|can|has|should|get\w*Paused|check|was|did|are)[A-Z_]?", name) and "r0" in res:
        return "bool"
    t = res.split("/", 1)[1] if "/" in res else res
    if "+" in res.split("/")[0]:
        return "double" if "double" in t else "int64"
    return {"undefined4": "int", "undefined1": "bool", "undefined2": "short", "undefined8": "int64"}.get(t, t)


def cmd_draft(args):
    fs = load()
    print("# decomp/signatures.txt draft (tools/signatures.py draft): check every line.")
    for f in fs:
        kind, why = classify(f)
        static = "static " if kind.startswith("static") else ""
        print(f"{static}{guess_return(f)}  {f['full']}")


SIG_RE = re.compile(r"^(static\s+)?(.+?)\s{2,}(\S.*)$")


def read_signatures(path=SIGNATURES):
    """{demangled signature: (static, return type, line)}"""
    out = {}
    for n, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        s = line.split("#", 1)[0].rstrip() if not line.lstrip().startswith("#") else ""
        if not s.strip():
            continue
        m = SIG_RE.match(s.strip())
        if not m:
            raise SystemExit(f"{path.name}:{n}: cannot read: {line}")
        out[m.group(3).strip()] = (bool(m.group(1)), m.group(2).strip(), n)
    return out


def cmd_check(args):
    fs = load()
    sigs = read_signatures()
    by_full = {f["full"]: f for f in fs}
    bad = 0
    for full, (static, ret, n) in sigs.items():
        f = by_full.get(full)
        if f is None:
            print(f"signatures.txt:{n}: no such method: {full}")
            bad += 1
            continue
        kind, why = classify(f)
        sret = ret.rstrip() in VALUE_SIZES or ret.startswith("std::") and not ret.endswith(("*", "&"))
        need = f["words"] + (0 if static else 1) + (1 if sret else 0)
        if static and kind == "method":
            print(f"signatures.txt:{n}: {full} is static here but {why}")
            bad += 1
        elif f["read"] > need:
            print(f"signatures.txt:{n}: {full} reads {f['read']} words, this signature passes {need}")
            bad += 1
    missing = [f for f in fs if f["full"] not in sigs]
    for f in missing:
        print(f"missing from signatures.txt: {f['full']}")
    print(f"{len(sigs)} signatures, {bad} problems, {len(missing)} methods missing")
    return 1 if bad or missing else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("report")
    p.add_argument("--all", action="store_true")
    sub.add_parser("draft")
    sub.add_parser("check")
    args = ap.parse_args()
    if args.cmd == "report":
        cmd_report(args)
    elif args.cmd == "draft":
        cmd_draft(args)
    else:
        sys.exit(cmd_check(args))


if __name__ == "__main__":
    main()
