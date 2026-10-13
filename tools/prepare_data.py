"""Convert the APK's assets into the 3DS's data folder.

    python tools/prepare_data.py            # build out/ZombieOutbreakSim/
    python tools/prepare_data.py --azahar   # ... and copy it onto Azahar's SD card

The folder goes on the SD card as sdmc:/ZombieOutbreakSim/. Each step is a
function below.

The card's tree mirrors the APK's assets/ folder: a file the game opens as
"images/maps/map01/tile_16_00_00.jpg" is images/maps/map01/tile_16_00_00.t3x
here, so the ported code's paths change only in their extension. What the
port adds (the logo) goes under gfx/.

Textures go through devkitPro's tex3ds into .t3x, which citro2d loads with
C2D_SpriteSheetLoad and citro3d with Tex3DS_TextureImport. A step skips an
output newer than its input, so a rerun only redoes what changed.
"""

import argparse
import concurrent.futures
import glob
import os
import shutil
import subprocess
import sys

from PIL import Image, ImageChops

import apk

TEX3DS = os.path.join(os.environ.get("DEVKITPRO", r"C:\devkitPro"), "tools", "bin", "tex3ds.exe")
if not os.path.exists(TEX3DS):
    TEX3DS = r"C:\devkitPro\tools\bin\tex3ds.exe"

WORK = os.path.join(apk.ROOT, "out", "work")


def tex3ds(png, t3x, fmt="rgba8888", compress="auto", quality=None):
    """One image -> one .t3x holding it as a single sub-image."""
    os.makedirs(os.path.dirname(t3x), exist_ok=True)
    cmd = [TEX3DS, "-f", fmt, "-z", compress, "-o", t3x, png]
    if quality:
        cmd[1:1] = ["-q", quality]
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)


def fresh(src, dst):
    return os.path.exists(dst) and os.path.getmtime(dst) >= os.path.getmtime(src)


def out(*parts):
    return os.path.join(apk.OUT, *parts)


def logo():
    """gfx/logo.t3x: the top screen's title image, 400x240.

    The home screen's 1024 backdrop has the Z.O.S logo centred on about
    (510, 370). A 5:3 window of 800x480 around it, halved, fills the top
    screen. The image is 400x240, so tex3ds pads it into a 512x256 texture
    and the sub-image's UVs cover the 400x240 part.
    """
    home = Image.open(apk.asset("images", "Fullscreen", "1024", "Background_HomeScreen.png")).convert("RGBA")
    top = home.crop((110, 130, 910, 610)).resize((400, 240), Image.LANCZOS)
    png = os.path.join(WORK, "logo.png")
    os.makedirs(WORK, exist_ok=True)
    top.save(png)
    tex3ds(png, out("gfx", "logo.t3x"))


def collision():
    """data/: the collision layers and the helicopter's overlay, unchanged.

    The port reads them as the game does, inflating a .cl with zlib: a byte
    per 4x4 map pixels (3DS Files/source/game/collisionlayer.h). All 20
    layers are copied, the 15
    featured maps' too, though their imagery was Google's and is not here.
    """
    os.makedirs(out("data"), exist_ok=True)
    files = glob.glob(apk.asset("data", "*.cl")) + [apk.asset("data", "helicopter.co")]
    for f in files:
        shutil.copy2(f, out("data", os.path.basename(f)))
    print(f"  {len(files)} files")


def maps():
    """images/maps/mapNN/tile_Z_X_Y.t3x: every tile of the five offline maps.

    Each map is a 256-pixel JPEG tile pyramid, zoom 16 (1 tile) to 20 (16x16
    tiles), 341 tiles; zoom 20's pixels are map units. A tile becomes a
    256x256 ETC1 texture, 32 KB, the same size the tile has in the game
    (MapRenderer::loadEmbeddedTileTexture). ETC1 at "high" is within 37 dB of
    the JPEG. It is left uncompressed: lz11/huff save only ~13% and the tile
    streamer reads it straight into a texture.
    """
    jobs = []
    for jpg in sorted(glob.glob(apk.asset("images", "maps", "map*", "tile_*.jpg"))):
        rel = os.path.relpath(jpg, apk.asset())
        t3x = out(os.path.splitext(rel)[0] + ".t3x")
        if not fresh(jpg, t3x):
            jobs.append((jpg, t3x))
    with concurrent.futures.ThreadPoolExecutor(os.cpu_count()) as pool:
        for f in [pool.submit(tex3ds, j, t, "etc1", "none", "high") for j, t in jobs]:
            f.result()
    total = len(glob.glob(out("images", "maps", "map*", "tile_*.t3x")))
    print(f"  {len(jobs)} converted, {total} tiles")


def premultiplied(img):
    """cocos2d premultiplies a PNG's alpha when it loads it, and the game's
    shaders blend premultiplied (Person.fsh scales all four channels by its
    alpha). The port does the multiply here, once."""
    r, g, b, a = img.convert("RGBA").split()
    return Image.merge("RGBA", (ImageChops.multiply(r, a), ImageChops.multiply(g, a), ImageChops.multiply(b, a), a))


def to_4bit(img):
    """OpenGLUtils::convert32bppTo16bpp: each channel's top four bits (v & 0xf0),
    no rounding. Written back as v | v >> 4 so tex3ds's own conversion to 4 bits
    lands on the same value whichever way it rounds."""
    lut = [(v & 0xf0) | (v >> 4) for v in range(256)]
    return img.point(lut * len(img.getbands()))


def texture(src, rel, scale=1, bits16=False, fmt=None):
    """One image under assets/ -> the same path as .t3x. RGBA images are
    premultiplied and kept at 8 bits a channel; images with no alpha are RGB8.
    bits16: RGBA4444, for what the game loaded with create16bppTextureFromFile.
    `scale` shrinks an image the 3DS cannot hold (over 1024 on a side).
    `fmt` overrides the format (tex3ds's name)."""
    t3x = out(os.path.splitext(rel)[0] + ".t3x")
    if fresh(src, t3x):
        return False
    img = Image.open(src)
    alpha = img.mode in ("RGBA", "LA", "P") and img.convert("RGBA").getextrema()[3][0] < 255
    img = premultiplied(img) if alpha or bits16 else img.convert("RGB")
    if scale != 1:
        img = img.resize((img.width // scale, img.height // scale), Image.LANCZOS)
    if max(img.size) > 1024:
        raise ValueError(f"{rel}: {img.size} is over the 3DS's 1024 texture limit")
    if bits16:
        img = to_4bit(img)
    png = os.path.join(WORK, os.path.splitext(rel)[0] + ".png")
    os.makedirs(os.path.dirname(png), exist_ok=True)
    img.save(png)
    tex3ds(png, t3x, fmt or ("rgba4444" if bits16 else "rgba8888" if alpha else "rgb888"))
    return True


def textures(pattern, scale_of=lambda rel: 1, bits16_of=lambda rel: False, fmt_of=lambda rel: None):
    jobs = []
    for src in sorted(glob.glob(apk.asset(*pattern.split("/")), recursive=True)):
        rel = os.path.relpath(src, apk.asset()).replace(os.sep, "/")
        jobs.append((src, rel, scale_of(rel), bits16_of(rel), fmt_of(rel)))
    with concurrent.futures.ThreadPoolExecutor(os.cpu_count()) as pool:
        done = sum(f.result() for f in [pool.submit(texture, *j) for j in jobs])
    print(f"  {pattern}: {done} converted, {len(jobs)} images")


# AppDelegate::applicationDidFinishLaunching picks the GUI folder by the
# screen's DPI: the first of MDPI 160, HDPI 240, XHDPI 320, XXHDPI 480 that is
# at least the device's, with dpToSize = that / 160. The bottom screen is
# 320x240 on a 3.02" diagonal, 132 dpi, which picks MDPI at dpToSize 1. The
# full-screen images go by the visible width, 512 up to 768 pixels; both
# screens are under that. The larger sets are for screens the 3DS does not
# have, so they are not converted.
GUI_DPI = "MDPI"
FULLSCREEN = "512"


def gui():
    """images/GUI/MDPI/<atlas>.t3x and <atlas>.frames; images/Fullscreen/512/.

    A .frames file is the atlas's .plist (cocos2d's format 2) as text, one
    frame a line: name x y w h offsetX offsetY sourceW sourceH rotated.
    None of the game's frames is rotated or trimmed.
    """
    import plistlib
    import re
    textures(f"images/GUI/{GUI_DPI}/*.png")
    textures(f"images/Fullscreen/{FULLSCREEN}/*")
    nums = lambda s: [int(v) for v in re.findall(r"-?\d+", s)]
    for plist in sorted(glob.glob(apk.asset("images", "GUI", GUI_DPI, "*.plist"))):
        p = plistlib.load(open(plist, "rb"))
        if p["metadata"]["format"] != 2:
            raise ValueError(f"{plist}: plist format {p['metadata']['format']}")
        tw, th = nums(p["metadata"]["size"])
        lines = [f"# {p['metadata']['textureFileName']} {tw} {th}"]
        for name, fr in p["frames"].items():
            x, y, w, h = nums(fr["frame"])
            ox, oy = nums(fr["offset"])
            sw, sh = nums(fr["sourceSize"])
            lines.append(f"{name} {x} {y} {w} {h} {ox} {oy} {sw} {sh} {int(fr['rotated'])}")
        dst = out("images", "GUI", GUI_DPI, os.path.splitext(os.path.basename(plist))[0] + ".frames")
        with open(dst, "w", newline="\n") as f:
            f.write("\n".join(lines) + "\n")
    print(f"  frame tables for {GUI_DPI}")


def sprites():
    """images/animations/, images/effects/, images/misc/: the people's sheets
    and the effects, at the game's paths.

    explosion01/02 are 2048 square, over the 3DS's 1024 limit on both models.
    They are 8x8 grids that EffectsRenderer::doRenderExplosions addresses in
    eighths, never pixels, so they are halved and nothing else changes.

    The game loaded everything here but the explosions with
    create16bppTextureFromFile (PersonType::loadTexture, EffectsRenderer's
    constructor, SimulationRenderer's person_shadow): RGBA4444. So are these.
    That is also what fits: a 1024 square sheet is 4 MB at RGBA8 and the
    seven people's sheets did not all fit in the linear heap.

    The explosions the game kept at 32 bits. At RGBA8 the two take 8 MB of
    the linear heap, which left under 1 MB for the map's tiles; as ETC1A4
    (8 bits a pixel, alpha in 4) they take 2 MB, and smoke and fire survive
    ETC1 better than RGBA4444's banding.
    """
    explosion = lambda rel: rel.startswith("images/effects/explosion")
    halve = lambda rel: 2 if explosion(rel) else 1
    bits16 = lambda rel: not explosion(rel)
    etc1a4 = lambda rel: "etc1a4" if explosion(rel) else None
    textures("images/animations/*.png", bits16_of=bits16)
    textures("images/effects/**/*.png", halve, bits16, etc1a4)
    textures("images/misc/*.png", bits16_of=bits16)


FFMPEG = shutil.which("ffmpeg") or "ffmpeg"
MKBCFNT = os.path.join(os.path.dirname(TEX3DS), "mkbcfnt.exe")


def audio():
    """audio/music/music01.wav, audio/sound/*.wav: 16-bit PCM for ndsp.

    The game plays music through SimpleAudioEngine and effects through
    AudioEngineWrapper (preload, play with volume and pitch, stop, set
    volume), all of which ndsp does on PCM. Each file keeps its own rate and
    channels; nothing is resampled. The music is 4m15s of 44.1 kHz stereo,
    45 MB as PCM, streamed off the card.
    """
    jobs = []
    for src in sorted(glob.glob(apk.asset("audio", "**", "*.*"), recursive=True)):
        rel = os.path.relpath(src, apk.asset())
        wav = out(os.path.splitext(rel)[0] + ".wav")
        if not fresh(src, wav):
            os.makedirs(os.path.dirname(wav), exist_ok=True)
            jobs.append([FFMPEG, "-v", "error", "-y", "-i", src, "-map_metadata", "-1",
                         "-c:a", "pcm_s16le", "-f", "wav", "-bitexact", wav])
    with concurrent.futures.ThreadPoolExecutor(os.cpu_count()) as pool:
        for f in [pool.submit(subprocess.run, j, check=True) for j in jobs]:
            f.result()
    print(f"  {len(jobs)} converted")


# The fonts the game names (Label::createWithTTF): Visitor TT2 BRK for the
# counters, Roboto and Open Sans for text. arial.ttf is in the APK but nothing
# opens it. A .bcfnt is a bitmap font at one size that citro2d scales; 24 is
# the largest size the game asks for at dpToSize 1. mkbcfnt's size is points
# at 96 dpi, so these are 32 pixels (Label's kFontAtlasPixels), a little over
# the largest asked for.
FONTS = ["Visitor TT2 BRK", "Roboto", "Open Sans"]
FONT_SIZE = 24


def fonts():
    """fonts/<name>.bcfnt: printable ASCII and Latin-1, at 24."""
    keep = os.path.join(WORK, "font_codepoints.txt")
    os.makedirs(WORK, exist_ok=True)
    with open(keep, "w") as f:
        f.write(" ".join(str(c) for c in list(range(0x20, 0x7F)) + list(range(0xA0, 0x100))))
    for name in FONTS:
        src = apk.asset("fonts", name + ".ttf")
        dst = out("fonts", name + ".bcfnt")
        if fresh(src, dst):
            continue
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        subprocess.run([MKBCFNT, "-s", str(FONT_SIZE), "-w", keep, "-o", dst, src], check=True,
                       stdout=subprocess.DEVNULL)
    print(f"  {len(FONTS)} fonts")


def cacert():
    """data/cacert.pem: the certificate authorities HTTPS trusts.

    The 3DS has no store mbedTLS can read, so libcurl is given Mozilla's, as
    certifi packs it (pip carries a copy when certifi is not installed).
    tile.openstreetmap.org and the Overpass API are HTTPS only.
    """
    try:
        import certifi
        src = certifi.where()
    except ImportError:
        from pip._vendor import certifi
        src = certifi.where()
    dst = out("data", "cacert.pem")
    if not fresh(src, dst):
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(src, dst)
    print(f"  {os.path.getsize(dst)} bytes from {src}")


STEPS = [logo, collision, maps, gui, sprites, audio, fonts, cacert]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--azahar", action="store_true", help="copy the result onto Azahar's SD card")
    args = ap.parse_args()

    for step in STEPS:
        print(f"[{step.__name__}]")
        step()

    if args.azahar:
        dest = os.path.join(apk.AZAHAR_SDMC, "ZombieOutbreakSim")
        if not os.path.isdir(apk.AZAHAR_SDMC):
            sys.exit(f"Azahar's SD card not found at {apk.AZAHAR_SDMC}")
        shutil.copytree(apk.OUT, dest, dirs_exist_ok=True)
        print(f"copied to {dest}")
    print(f"done: {apk.OUT}")


if __name__ == "__main__":
    main()
