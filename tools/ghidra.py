"""Run the project's Ghidra scripts headless, without typing the command out.

    python tools/ghidra.py decompile cocos2d::Node::Node _ZN7cocos2d3RefC2Ev 0019a7d4
    python tools/ghidra.py export          ExportGameClasses.java into decomp/
    python tools/ghidra.py collect         CollectFieldAccesses.java into decomp/layouts/
    python tools/ghidra.py functions       DumpFunctions.java into decomp/layouts/_functions.tsv
    python tools/ghidra.py run Script.java [--write]

Every run opens the Ghidra project ZombieOutbreakSim read-only unless it saves
(run --write). Ghidra holds a lock on the project, so close the
GUI's copy of it first.

Where Ghidra is: GHIDRA_HOME (the folder with support/analyzeHeadless.bat),
by default a "ghidra" folder beside this repository. Where the project is:
GHIDRA_PROJECTS, by default "GhidraProjects" beside this repository. The
project imports the APK's lib/armeabi-v7a/libZombieOutbreakSimulator.so.
"""
import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HEADLESS = Path(os.environ.get("GHIDRA_HOME", ROOT.parent / "ghidra")) / "support" / "analyzeHeadless.bat"
PROJECTS = Path(os.environ.get("GHIDRA_PROJECTS", ROOT.parent / "GhidraProjects"))
PROJECT = "ZombieOutbreakSim"
PROGRAM = "libZombieOutbreakSimulator.so"
SCRIPTS = ROOT / "tools" / "ghidra_scripts"


def run(script, write=False, env=None, quiet=True):
    cmd = [str(HEADLESS), str(PROJECTS), PROJECT, "-process", PROGRAM, "-noanalysis",
           "-scriptPath", str(SCRIPTS), "-postScript", script]
    if not write:
        cmd.append("-readOnly")
    e = dict(os.environ)
    e["ZOS_DECOMP"] = str(ROOT / "decomp")
    e.update(env or {})
    p = subprocess.run(cmd, env=e, capture_output=True, text=True, errors="replace")
    lines = (p.stdout + p.stderr).splitlines()
    failed = p.returncode != 0 or any("ERROR" in l for l in lines)
    for l in lines:
        # A script's println comes out as "INFO  Script.java> text (GhidraScript)".
        if "(GhidraScript)" in l and "> " in l:
            print(l.split("> ", 1)[1].replace("(GhidraScript)", "").rstrip())
        elif failed or not quiet:
            print(l)
    if failed:
        sys.exit(p.returncode or 1)
    return p


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("decompile")
    p.add_argument("names", nargs="+")
    p.add_argument("-o", "--out")
    sub.add_parser("export")
    sub.add_parser("collect")
    sub.add_parser("functions")
    p = sub.add_parser("run")
    p.add_argument("script")
    p.add_argument("--write", action="store_true")
    p.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    if args.cmd == "decompile":
        fd, tmp = tempfile.mkstemp(suffix=".c")
        os.close(fd)
        run("DecompileByName.java", env={"ZOS_NAMES": ",".join(args.names), "ZOS_OUT": tmp})
        text = Path(tmp).read_text(encoding="utf-8")
        os.unlink(tmp)
        if args.out:
            Path(args.out).write_text(text, encoding="utf-8")
        else:
            sys.stdout.write(text)
    elif args.cmd == "export":
        run("ExportGameClasses.java")
    elif args.cmd == "collect":
        run("CollectFieldAccesses.java")
    elif args.cmd == "functions":
        run("DumpFunctions.java")
    elif args.cmd == "run":
        run(args.script, write=args.write, quiet=not args.verbose)


if __name__ == "__main__":
    main()
