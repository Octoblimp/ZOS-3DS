# The data on the card

`tools/prepare_data.py` converts your own APK into `out/ZombieOutbreakSim/`
(126 MB). `tools/build_cia.ps1` puts that folder in the build's RomFS, and
the game reads it as `romfs:/`.
A `-NoData` build carries none of it and reads `sdmc:/ZombieOutbreakSim/`
instead (`zosAssetRoot()`, set in main()).

## Layout: the APK's `assets/`, mirrored

Every file sits at the path the game opens, with only the extension
changed. For example, `MapRenderer::loadEmbeddedTileTexture` asks for
`images/maps/map%02d/tile_%02d_%02d_%02d.jpg`, and the card has
`images/maps/map01/tile_16_00_00.t3x`. So the ported code keeps its path
strings and changes one extension in one place (the file layer).
`gfx/` holds what the port adds.

| On the card | From | Format | Step |
|---|---|---|---|
| `gfx/logo.t3x` | `images/Fullscreen/1024/Background_HomeScreen.png`, cropped | RGBA8, 400x240 | `logo` |
| `data/collision_layer_*.cl`, `data/helicopter.co` | the same | unchanged ([collision-layer.md](formats/collision-layer.md)) | `collision` |
| `images/maps/mapNN/tile_Z_X_Y.t3x` | the `.jpg` | ETC1 256x256, uncompressed, 1,705 tiles | `maps` |
| `images/GUI/MDPI/<atlas>.t3x` | the `.png` | RGBA8, premultiplied | `gui` |
| `images/GUI/MDPI/<atlas>.frames` | the `.plist` | text ([frames.md](formats/frames.md)) | `gui` |
| `images/Fullscreen/512/*.t3x` | the `.png`/`.jpg` | RGBA8 premultiplied, or RGB8 if opaque | `gui` |
| `images/animations/*.t3x` | the `.png` | RGBA4444, premultiplied, 1024 square | `sprites` |
| `images/effects/**.t3x`, `images/misc/*.t3x` | the `.png` | RGBA4444, premultiplied; the explosions RGBA8, halved | `sprites` |
| `audio/music/music01.wav`, `audio/sound/*.wav` | the `.mp3`/`.wav` | PCM16 at the source's rate and channels | `audio` |
| `fonts/<name>.bcfnt` | the `.ttf` | 24 pt, ASCII and Latin-1 | `fonts` |

## Choices, and why

- **Which GUI set.** `AppDelegate::applicationDidFinishLaunching` picks the
  GUI folder by DPI: the first of MDPI 160, HDPI 240, XHDPI 320 and
  XXHDPI 480 that is at least the device's. It then sets
  `GUIGlobals::dpToSize` to that DPI over 160. The bottom screen is about
  132 dpi, so the game's own rule picks MDPI at `dpToSize` 1. The
  full-screen set goes by visible width: 512 up to 768 pixels. The other
  sets are for screens the 3DS lacks and are not converted.
- **Premultiplied alpha.** cocos2d premultiplies a PNG's alpha as it loads
  it, and the game's shaders assume that: `Person.fsh` scales all four
  channels by its alpha. The conversion does the multiply once, and the
  port draws these textures with the blend `ONE, ONE_MINUS_SRC_ALPHA`.
  citro2d's own default is `SRC_ALPHA, ONE_MINUS_SRC_ALPHA`, which is right
  for its text and wrong for these.
- **Map tiles as ETC1.** A tile is 32 KB, within 37 dB of the JPEG, and
  reads straight into a texture. `-q high` costs about 17 minutes for the
  first run (`medium` is 10x faster and 0.1 dB worse). Compression would
  save only 13%.
- **The 1024 limit.** Both the Old and the New 3DS have the same GPU and
  take textures up to 1024 on a side. Only `explosion01/02` (2048 square)
  are over that. They are 8x8 grids of frames, and
  `EffectsRenderer::doRenderExplosions` addresses them in eighths, so the
  conversion halves them and the code does not change.
- **Sprite frames are the game's data.** A person's frames are not in a
  plist. `PersonTypeFactory::setupPersonTypes` passes each
  `Animation`/`AnimationPair` an `AnimationIndexImage` table in `.data`:
  `{x, y, w, h, cellW, cellH}` per frame, ended by x = -1 (guy01 walk01:
  (640, 112) 15x14 in a 16x16 cell, at Ghidra 0x0058c004). These tables
  come over with the code (`tools/gen_persontypes.py` writes them into
  `source/game/persontypes_data.cpp`); the sheets are converted whole.
- **Sprites at 16 bits, as the game had them.** Everything under
  `animations/`, `effects/` and `misc/` but the two explosions was loaded
  with `OpenGLUtils::create16bppTextureFromFile`: GL_RGBA with
  GL_UNSIGNED_SHORT_4_4_4_4, each channel's top four bits (`v & 0xf0`, no
  rounding) after cocos2d's premultiply. The conversion does the same and
  tex3ds writes RGBA4444, which the 3DS draws directly. It is also what
  fits: a 1024 square sheet is 4 MB at RGBA8, and the linear heap has about
  28 MB free after start-up. At 16 bits the seven sheets take 14 MB
  (measured on Azahar). The explosions were 32 bpp and stay RGBA8.
- **Sound as PCM.** The game plays effects through `AudioEngineWrapper`
  (preload, play with volume and pitch, stop, set volume) and music through
  `SimpleAudioEngine`. ndsp does all of that on PCM16, and nothing is
  resampled. The music is 45 MB and streams off the card.

## Measured on Azahar

Early in the port, a test program loaded and drew each kind of file:

| Load | Time |
|---|---|
| `gfx/logo.t3x` | 117 ms |
| one zoom 16 tile | 50 ms |
| `guy01.t3x` | 134 ms |
| 20 zoom 20 tiles | 1,086 ms |
| Visitor's `.bcfnt` | 134 ms |

These are the emulator's file speeds, not the hardware's. Either way, tiles
must stream off the main thread, as `MapRenderer::onBackgroundThread` does
in the original.
