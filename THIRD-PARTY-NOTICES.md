# Third-party notices

The [MIT License](LICENSE) covers the port's own work: the code in this
repository written for the Nintendo 3DS. It does not cover the game, or the
libraries and services below, which keep their own terms.

## Zombie Outbreak Simulator

Zombie Outbreak Simulator is by Binary Space. Its name, art, sound, maps and
text belong to Binary Space, and so do the game logic and data tables this
port reproduces from it. No rights to any of them are granted here. This is
an unofficial fan port, not made or endorsed by Binary Space or Nintendo.

Of the game's art and sound, the repository holds only the HOME Menu's
icon, banner and banner sound in `3DS Files/meta`. They are made from the
game's launcher icon, its home screen's logo and one of its zombie moans
(`tools/make_meta.py`, `tools/banner_sound.py`). It holds none of the game's
maps or code: `tools/prepare_data.py` makes the data from your own copy of
the APK, and the reverse-engineering tools make the decompilation from it.

## Libraries in the build

The 3DS build links these libraries, all from
[devkitPro](https://devkitpro.org/):

| Library | Licence | Notice |
| --- | --- | --- |
| [libctru](https://github.com/devkitPro/libctru) | zlib License | The libctru authors (originally ctrulib) |
| [citro3d](https://github.com/devkitPro/citro3d) | zlib License | Copyright (C) 2014-2018 fincs |
| [libcurl](https://curl.se/) 8.4.0 | curl License (MIT-style) | Copyright (C) Daniel Stenberg, <daniel@haxx.se>, and many contributors |
| [Mbed TLS](https://www.trustedfirmware.org/projects/mbed-tls/) 2.28.8 | Apache License 2.0 (dual-licensed with GPL-2.0-or-later; used here under Apache 2.0) | Copyright The Mbed TLS Contributors |
| [zlib](https://zlib.net/) | zlib License | Copyright (C) 1995-2023 Jean-loup Gailly and Mark Adler |

The release ZIP carries the full licence texts of libcurl, Mbed TLS and zlib
in its `licenses/` folder. The game's credits screen lists these libraries
too.

## Online services

- **Satellite imagery:** Source: Esri, Vantor, Earthstar Geographics, and the
  GIS User Community. This is
  [Esri World Imagery](https://www.arcgis.com/home/item.html?id=10df2279f9684e4a9f6a7f08febac2a9),
  used under Esri's terms of use. The game shows this credit wherever the
  imagery is on screen.
- **Map data:** © [OpenStreetMap](https://www.openstreetmap.org/copyright)
  contributors, under the Open Database License (ODbL). The data comes from
  the [Overpass API](https://overpass-api.de/), and from
  [Nominatim](https://nominatim.org/) for place search. The game shows this
  credit with player maps.

## Build tools

These tools build and package the port. They are not in this repository.

- [makerom](https://github.com/3DSGuy/Project_CTR), from Project_CTR.
- [bannertool](https://github.com/Epicpkmn11/bannertool) (MIT License).
- devkitPro's devkitARM (whose C and C++ runtime is linked into the build),
  tex3ds, mkbcfnt and picasso.
- [Ghidra](https://ghidra-sre.org/) (Apache License 2.0), which only the
  reverse-engineering tools use.
