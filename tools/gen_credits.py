"""Generate the game's credits text from the .so.

CreditsScene::init (decomp/classes/CreditsScene.c) lays out two strings from
a table at DAT_0056daa8: the game's credits, then Android's "Software
Licenses". This reads the first and writes
"3DS Files/source/game/credits_data.cpp"; the port's licenses replace the
second (game/creditsscene.cpp).

    python tools/gen_credits.py
"""
import struct
from pathlib import Path

from disasm import GHIDRA_BASE, Library
from sodata import offset_of

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "3DS Files" / "source" / "game" / "credits_data.cpp"
TABLE = 0x56daa8


def c_bytes(lib, ghidra_addr):
    off = offset_of(lib, ghidra_addr)
    return lib.data[off:lib.data.index(b"\0", off)]


def literal(line):
    """A C++ string literal of UTF-8 bytes; octal escapes, which stop at three digits."""
    out = []
    for b in line:
        c = chr(b)
        if c in '\\"':
            out.append("\\" + c)
        elif c == "\n":
            out.append("\\n")
        elif 0x20 <= b < 0x7f:
            out.append(c)
        else:
            out.append(f"\\{b:03o}")
    return '"' + "".join(out) + '"'


def main():
    lib = Library()
    target = struct.unpack_from("<I", lib.data, offset_of(lib, TABLE))[0] + GHIDRA_BASE
    text = c_bytes(lib, target)
    lines = text.split(b"\n")
    out = [
        "// Generated from CreditsScene::init's first string",
        f"// (DAT_{TABLE:08x}). Do not edit.",
        "",
        '#include "game/creditsscene.h"',
        "",
        "const char* const CreditsScene::kGameCredits =",
    ]
    for i, line in enumerate(lines):
        last = i == len(lines) - 1
        body = literal(line + (b"" if last else b"\n"))
        out.append("    " + body + (";" if last else ""))
    out.append("")
    OUT.write_text("\n".join(out), newline="\n")
    print(f"{OUT.relative_to(ROOT)}: {len(text)} bytes, {len(lines)} lines")


if __name__ == "__main__":
    main()
