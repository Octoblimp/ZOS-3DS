"""List the game's own classes from the .so's dynamic symbol table.

    python tools/list_game_classes.py          # writes decomp/classes.txt

libZombieOutbreakSimulator.so links the game with cocos2d-x 3.x and
everything that ships inside it (Bullet, Recast/Detour, Clipper, poly2tri,
tinyxml2, rapidjson, libpng/jpeg/tiff/webp, FreeType, sqlite, curl, ...).
The game's own code is what is left after those are taken out: about 60
classes, ~245 KB of the 3.2 MB .text.

The list is written one class per line with its function count and the
bytes the symbol table gives its functions. It is reviewed by hand, and
tools/ghidra_scripts/ExportGameClasses.java decompiles what it names.
"""

import collections
import os
import re
import subprocess

import apk

READELF = r"C:\devkitPro\devkitARM\bin\arm-none-eabi-readelf.exe"
CXXFILT = r"C:\devkitPro\devkitARM\bin\arm-none-eabi-c++filt.exe"

# Namespaces and prefixes that belong to cocos2d-x or a library it bundles.
LIBRARY = re.compile(
    r"^(cocos2d|std|__gnu_cxx|__cxxabiv1|CocosDenshion|ClipperLib|p2t|tinyxml2|rapidjson|"
    r"bt[A-Z]|dt[A-Z]|du[A-Z]|rc[A-Z]|gjkepa2_impl|CProfile|BroadphaseAabbTester|BroadphaseRayTester|"
    r"InplaceSolverIslandCallback|SupportVertexCallback|DebugDrawcallback|BuildContext|"
    r"SphereTriangleDetector|_JNIEnv|spine|flatbuffers)"
)

# Game classes whose only job is an Android service the port drops (the
# README's list). They are still listed, marked, so a reader can see what was
# cut.
DROPPED = {
    "AdManager", "AdManager::Observer", "InAppPurchaseManager", "InAppPurchaseManager::Observer",
    "InAppPurchaseManager::Prices", "IAPPopup", "NativeBridge", "GoogleMapTileLoader",
    "GoogleMapAndroidCacheIndex", "PlayerMapCache", "PlayerMapNode", "PlayerMapMarker",
    "PlayerMapMarkerLevel", "PlayerMapMarkerManager", "PlayerMapMarkerTile",
}


def functions():
    out = subprocess.run([READELF, "--dyn-syms", "-W", apk.SO_V7A], capture_output=True, text=True, check=True).stdout
    rows = []
    for line in out.splitlines():
        f = line.split()
        if len(f) >= 8 and f[3] == "FUNC" and f[6] != "UND":
            rows.append((int(f[1], 16), int(f[2], 0) if f[2].startswith("0x") else int(f[2]), f[7]))
    names = subprocess.run([CXXFILT], input="\n".join(r[2] for r in rows), capture_output=True, text=True, check=True).stdout.splitlines()
    return [(a, s, m, d) for (a, s, m), d in zip(rows, names)]


def owner(demangled):
    """'Simulation::dropBomb(BombType, int, int)' -> 'Simulation'; None for free functions."""
    head = demangled.split("(", 1)[0]
    if " " in head.split("<", 1)[0]:          # a return type: a template instance, not a method
        return None
    if "::" not in head:
        return None
    return head.rsplit("::", 1)[0]


def main():
    per = collections.defaultdict(lambda: [0, 0])
    for addr, size, mangled, demangled in functions():
        cls = owner(demangled)
        if not cls or LIBRARY.match(cls) or demangled.startswith(("non-virtual thunk", "virtual thunk", "covariant")):
            continue
        per[cls][0] += 1
        per[cls][1] += size

    os.makedirs(os.path.join(apk.ROOT, "decomp"), exist_ok=True)
    path = os.path.join(apk.ROOT, "decomp", "classes.txt")
    with open(path, "w", newline="\n") as f:
        f.write("# The game's own classes in libZombieOutbreakSimulator.so (armeabi-v7a).\n")
        f.write("# Made by tools/list_game_classes.py. Columns: class, functions, bytes, note.\n")
        f.write("# 'dropped' marks an Android-only service the port leaves out (see the README).\n")
        for cls in sorted(per):
            n, size = per[cls]
            cols = [cls, str(n), str(size)] + (["dropped"] if cls in DROPPED else [])
            f.write("\t".join(cols) + "\n")
    total = sum(v[1] for v in per.values())
    kept = sum(v[1] for k, v in per.items() if k not in DROPPED)
    print(f"{len(per)} classes, {total} bytes ({kept} kept, {total - kept} dropped) -> {path}")


if __name__ == "__main__":
    main()
