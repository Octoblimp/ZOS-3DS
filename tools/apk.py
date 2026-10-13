"""Where the tools find the game: the APK, its extracted tree, and the data root.

Every tool reads the APK through here, so the paths live in one place.

    APK            APK and Source/Zombie Outbreak Simulator.apk (your own copy)
    EXTRACTED      extracted/apk/  -- the APK unzipped, made on demand (ignored by git)
    OUT            out/ZombieOutbreakSim/ -- the converted data, laid out as on the card
    AZAHAR_SDMC    %APPDATA%/Azahar/sdmc -- the emulator's SD card

On the 3DS the data root is sdmc:/ZombieOutbreakSim/ (source/paths.h).
"""

import os
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
APK = os.path.join(ROOT, "APK and Source", "Zombie Outbreak Simulator.apk")
EXTRACTED = os.path.join(ROOT, "extracted", "apk")
OUT = os.path.join(ROOT, "out", "ZombieOutbreakSim")
AZAHAR_SDMC = os.path.join(os.environ.get("APPDATA", ""), "Azahar", "sdmc")

# The game's code, for reverse engineering: the armeabi-v7a library.
# lib/armeabi/ holds an ARMv5TE build of the same code, kept for cross-checks.
SO_V7A = os.path.join(EXTRACTED, "lib", "armeabi-v7a", "libZombieOutbreakSimulator.so")
SO_V5 = os.path.join(EXTRACTED, "lib", "armeabi", "libZombieOutbreakSimulator.so")


def extracted():
    """The extracted APK's root, unzipping it first if it is not there yet."""
    marker = os.path.join(EXTRACTED, "AndroidManifest.xml")
    if not os.path.exists(marker):
        if not os.path.exists(APK):
            raise SystemExit(f"APK not found: {APK}")
        zipfile.ZipFile(APK).extractall(EXTRACTED)
    return EXTRACTED


def asset(*parts):
    """A path under the APK's assets/ folder."""
    return os.path.join(extracted(), "assets", *parts)
