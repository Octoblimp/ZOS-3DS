# ZOS-3DS

An unofficial port of **Zombie Outbreak Simulator** (Binary Space, Android)
to the Nintendo 3DS, built with devkitARM, libctru and citro3d.

It runs the real game. The Android build's C++ was decompiled and ported
class by class, and its art, sound and maps are converted from the APK. It
is not a remake.

The game's art, sound and maps are not in this repository. To build the port
yourself you need your own copy of the Android game; see
[Getting the assets](#getting-the-assets).

## Features

- **The whole game on the bottom screen:** the 5 built-in maps, the outbreak
  settings, every action (bombs, soldiers, helicopters and the rest), the
  tutorial and the credits. Everything the Android version sold or unlocked
  with ads is unlocked.
- **The stats on the top screen:** the counters, the population graph and a
  minimap.
- **The 15 online city maps** with satellite imagery from Esri World Imagery.
  A map downloads whole when it starts and is then kept on the SD card.
- **Player maps of anywhere.** The game's own map server is gone, so the port
  makes them on the 3DS:
  - browse the imagery, press New map and place the square;
  - the collision (buildings, roads, water) comes from OpenStreetMap;
  - Search (X) finds a place by name.
- **Saving:** the game saves on START, HOME and sleep, and goes back to the
  saved game the next time it starts.
- **Positional sound (optional):** SELECT pans each sound effect towards
  where it happens on the map. It is off by default, as in the original, and
  the setting is remembered.
- **New 3DS:** the game runs in the 178 MB memory mode at 804 MHz.

Left out, because they are Android services: ads, in-app purchases, video
unlocks and sharing.

Tested on a New 3DS XL and on the Azahar emulator. An Old 3DS gets a smaller
memory mode and has not been tested.

## Controls

| Input | In game |
| --- | --- |
| Stylus | Menus and buttons; drag to pan the map; tap to use the chosen action |
| Circle Pad / D-pad | Pan |
| R / L | Zoom in / out |
| B | Cancel the chosen action |
| Y | Evacuate the waiting helicopter |
| SELECT | Positional sound on or off |
| START | Save and quit |

On the map select screen:
- X searches for a place.
- While you place a new player map, A downloads it and B cancels.

## Installing a release

You need a 3DS with custom firmware (Luma3DS), and either FBI (or another CIA
installer) or the Homebrew Launcher.

1. Install `ZOS-3DS.cia` with FBI. Or copy `ZOS-3DS.3dsx` to `sdmc:/3ds/` and
   start it from the Homebrew Launcher. The `.cia` is the better choice: it
   gets the New 3DS's large memory mode, and it has a HOME Menu icon.
2. For sound, the SD card needs `sdmc:/3ds/dspfirm.cdc`. If it isn't there,
   dump it once with [DSP1](https://github.com/zoogie/DSP1).
3. The online maps and player maps need Wi-Fi the first time they load. After
   that they are on the card.

What the game writes to the SD card:
- saves and map caches in `sdmc:/ZombieOutbreakSim/` (it makes the folder);
- its log in `sdmc:/zos_debug.txt`.

On a New 3DS, the system restarts into the 178 MB mode when the game starts
and back to the HOME Menu when it ends.

## Building from source

### Requirements

- **Windows.** The build script is PowerShell. To build by hand on another
  system, see [Building without the script](#building-without-the-script).
- **[devkitPro](https://devkitpro.org/wiki/Getting_Started)**, installed to
  `C:\devkitPro`, with the 3DS packages and the port libraries. From
  devkitPro's MSYS2 shell, run:

  ```bash
  pacman -S 3ds-dev 3ds-curl 3ds-mbedtls 3ds-zlib
  ```

  `3ds-dev` brings devkitARM, libctru, citro3d, picasso, tex3ds and
  mkbcfnt.
- **Python 3** with Pillow. `certifi` is optional: pip's own copy is used
  without it.

  ```bash
  pip install pillow
  ```

- **ffmpeg** on `PATH`, to convert the audio.
- **makerom and bannertool,** which aren't in devkitPro's packages. Put their
  Windows builds in `tools/3ds-bin/`:
  - `makerom.exe` from
    [Project_CTR's releases](https://github.com/3DSGuy/Project_CTR/releases)
    (makerom v0.18.4 or later);
  - `bannertool.exe` from
    [bannertool's releases](https://github.com/Epicpkmn11/bannertool/releases)
    (v1.2.2 or later).

### Getting the assets

1. **Get the APK.** Copy the game's APK from an Android device it is
   installed on, to `APK and Source/Zombie Outbreak Simulator.apk` in this
   repository. With adb:

   ```bash
   adb shell pm path com.binaryspacegames.zombieoutbreaksimulator
   ```

   ```bash
   adb pull /data/app/<the path it printed>/base.apk "APK and Source/Zombie Outbreak Simulator.apk"
   ```

   The port was made from version 1.6.4. That APK is about 70 MB, and its
   SHA-256 is
   `8005c00823422574c0951d84b1d3a1ce97992b698e38c5feb7bb2173d8cc67a0`.
   Other versions may differ.

   A split install keeps the game's `assets/` in `base.apk`, which is all
   the build needs.

2. **Convert the data** into `out/ZombieOutbreakSim/`:

   ```bash
   python tools/prepare_data.py
   ```

   - The first run takes about 20 minutes and writes about 126 MB. Later
     runs only redo what changed.
   - The textures become `.t3x`, the sounds `.wav`, and the font `.bcfnt`.
   - `--azahar` also copies the result onto the Azahar emulator's SD card.

3. **The HOME Menu icon, banner and banner sound** are made from the game
   too, into `3DS Files/meta`. The build script makes them when they are
   missing. To remake them:

   ```bash
   python tools/make_meta.py
   ```

   ```bash
   python tools/banner_sound.py
   ```

### Building

```powershell
powershell -ExecutionPolicy Bypass -File tools/build_cia.ps1
```

This builds `3DS Files/ZOS-3DS.cia` and `ZOS-3DS.3dsx` (about 124 MB each).
The game's data is in their RomFS, so they contain Binary Space's art and
sound.

```powershell
powershell -ExecutionPolicy Bypass -File tools/build_cia.ps1 -NoData
```

This builds the engine alone, about 1 MB. It reads the data from the SD card
instead, so copy `out/ZombieOutbreakSim/` to the card's root, as
`sdmc:/ZombieOutbreakSim/`. If the data is missing, the game says which file
it looked for and waits for START.

`-DriveLetter Y:` picks the temporary drive letter (see below).

### Building without the script

The Makefile can't run from a path with a space in it, and the project folder
is `3DS Files`. The script works around that:

1. It maps a temporary drive letter onto the folder with `subst`.
2. It links `3DS Files/romfs` to `out/ZombieOutbreakSim` (a junction).
3. It runs `make cia` in devkitPro's MSYS2 shell.

By hand, on Windows, map the drive first:

```bat
subst Z: "C:\path\to\ZOS-3DS\3DS Files"
```

Then link the data, so `3DS Files/romfs` points to `out/ZombieOutbreakSim`:

```bat
mklink /J "C:\path\to\ZOS-3DS\3DS Files\romfs" "C:\path\to\ZOS-3DS\out\ZombieOutbreakSim"
```

Then, in devkitPro's MSYS2 shell:

```bash
cd /z && make cia ROMFS=romfs
```

Leave out `ROMFS=romfs` for the build without data. Afterwards, remove the
drive mapping:

```bat
subst Z: /D
```

On Linux or macOS:
- Build from a path without spaces, for example through a symlink.
- Use native builds of
  [makerom](https://github.com/3DSGuy/Project_CTR) and
  [bannertool](https://github.com/Epicpkmn11/bannertool), passed as
  `make cia MAKEROM=... BANNERTOOL=...`.
- The Python tools look for devkitPro's tools under `$DEVKITPRO`.

### Running on Azahar

1. Open `ZOS-3DS.3dsx` in [Azahar](https://azahar-emu.org/) (File > Load
   File).
2. A `-NoData` build needs the data on the emulator's SD card:
   `python tools/prepare_data.py --azahar` puts it there.

To boot it and check it automatically, run:

```powershell
powershell -ExecutionPolicy Bypass -File tools/run_azahar_test.ps1
```

The script boots the build, saves screenshots to `%TEMP%\zos_azahar`, and
judges the run from the game's log.

## How the port works

The Android game is cocos2d-x 3.x, with its game logic in C++ compiled into
`lib/armeabi-v7a/libZombieOutbreakSimulator.so`. The `.so` keeps its dynamic
symbol table, so Ghidra names every method (`Simulation::updatePolice`,
`ZombieWave::addZombie`, and so on).

The port has three layers:

1. **The game logic,** ported from the decompilation as faithfully as
   possible: `Simulation`, `PersonType`, `ZombieWave`, `GameOptions`, the
   collision layers, the bombs, the soldiers and the helicopters. Where the
   game kept data in tables (person types, maps, credits), that source was
   generated from the `.so` itself.
2. **A small cocos2d-x subset** on the port's own citro3d renderer, so the
   game's scene and UI classes port with few changes.
3. **The 3DS platform:** streaming the map tiles, touch and buttons, ndsp
   audio, the data in RomFS or on the card, HTTPS (libcurl and Mbed TLS),
   and the top screen.

Comments in the source name the game's code each part was ported from, as
`decomp/classes/<Class>.c` and Ghidra's addresses. The decompilation itself is
not in this repository.

## Repository layout

| Path | Contents |
| --- | --- |
| `3DS Files/` | The devkitARM project |
| `3DS Files/source/cocos/` | The cocos2d-x subset (Node, Director, Label, audio, …) |
| `3DS Files/source/game/` | The game's classes, one file per class, plus the port's (player maps, satellite tiles, top screen) |
| `3DS Files/source/gfx/` | The citro3d renderer and its shaders |
| `3DS Files/source/net/` | HTTPS and a JPEG reader for the satellite tiles |
| `3DS Files/meta/` | `app.rsf` (the CIA's settings) |
| `tools/` | The asset pipeline (`prepare_data.py`, `make_meta.py`, `banner_sound.py`, `apk.py`), the build script, the `.3dsx` check and the Azahar runner |

These are made locally and are not in git:
- `APK and Source/` (your APK);
- `extracted/` (the APK unzipped);
- `out/` (the converted data);
- `tools/3ds-bin/` (makerom and bannertool);
- the icon, banner and banner sound in `3DS Files/meta/`;
- the build's output.

## Credits

- **Zombie Outbreak Simulator** is by Binary Space. This is an unofficial fan
  port, not made or endorsed by Binary Space or Nintendo. If you like it, buy
  the game.
- **Satellite imagery:** Source: Esri, Vantor, Earthstar Geographics, and
  the GIS User Community (Esri World Imagery).
- **Map data:** © OpenStreetMap contributors (ODbL), through the Overpass
  API, and Nominatim for the search.
- **Libraries:** libctru and citro3d (devkitPro, zlib licence), libcurl (curl
  licence), Mbed TLS (Apache License 2.0) and zlib.

The game's art, sound and maps, and the data `tools/prepare_data.py` makes
from them, belong to Binary Space.

## License

The port's own code is under the [MIT License](LICENSE). The game, and the
libraries and services the port uses, keep their own terms: see
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
