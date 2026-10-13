"""The collision layers (data/collision_layer_*.cl): read them, and draw them
over their map as the game's own debug view does.

    python tools/collision.py                 # out/debug/collision_mapNN.png for map01..05
    python tools/collision.py map03 --scale 2

A .cl file is one zlib stream. Inflated, it is a byte per 4x4 cell of the
map, row by row: the offline maps are 4096 units square, so 1024x1024 bytes
(docs/formats/collision-layer.md). The bits, as Simulation reads them:

    0x01  walkable          Simulation::canWalk
    0x02  sight passes      CollisionLayer::isVisibleFrom
    0x18  both set: inside a building   Simulation::isBuilding

The overlay colours are CollisionLayerRenderer::createTexture's (0x0019ba2c):
each value has an RGB, scaled by 3/8 and given alpha 0x60, that is, a
premultiplied colour at 96/255. Any other value is an error there, and here.
Zoom 18 is 4x4 tiles of 256 pixels, so its pixels are the layer's cells.
"""

import argparse
import os
import zlib

from PIL import Image

import apk

# CollisionLayerRenderer::createTexture's colours, by value.
PALETTE = {
    0x00: 0xFF0000,
    0x03: 0x808080,
    0x07: 0x008000,
    0x0F: 0x0040FF,
    0x16: 0xFFFFFF,
    0x1F: 0xD06000,
    0x22: 0x800000,
    0x42: 0xFFC000,
    0x82: 0x4090FF,
}
ALPHA = 0x60

MAPS = ["map01", "map02", "map03", "map04", "map05"]
OFFLINE_SIZE = 4096          # MapConfigFactory: every offline map is 0x1000 square
CELL = 4                     # CollisionLayer's constructor: width >> 2, height >> 2


def load(name, size=OFFLINE_SIZE):
    """('map01' or a featured map's id) -> (width, height, bytes), as
    CollisionLayer's constructor and Utils::decompressData make it."""
    file = f"collision_layer_{name}.cl"
    raw = open(apk.asset("data", file), "rb").read()
    w = h = size // CELL
    z = zlib.decompressobj()
    data = z.decompress(raw)
    # decompressData inflates into exactly w*h bytes with Z_FINISH and throws
    # unless that ends the stream with no room left over.
    if not z.eof or len(data) != w * h:
        raise ValueError(f"{file}: inflated to {len(data)} bytes, expected {w}x{h}, eof={z.eof}")
    bad = set(data) - set(PALETTE)
    if bad:
        raise ValueError(f"{file}: invalid collision values {sorted(bad)}")
    return w, h, data


def overlay_image(w, h, data):
    """The layer as CollisionLayerRenderer's texture: premultiplied RGBA."""
    lut = bytearray()
    for v in range(256):
        c = PALETTE.get(v, 0)
        lut += bytes(((c >> 16) * 3 >> 3, ((c >> 8) & 0xFF) * 3 >> 3, (c & 0xFF) * 3 >> 3))
    rgb = bytearray(w * h * 3)
    for i, v in enumerate(data):
        rgb[i * 3:i * 3 + 3] = lut[v * 3:v * 3 + 3]
    return Image.frombytes("RGB", (w, h), bytes(rgb))


def map_image(name, zoom=18):
    """The map's tiles at one zoom level, stitched."""
    n = 1 << (zoom - 16)
    img = Image.new("RGB", (256 * n, 256 * n))
    for y in range(n):
        for x in range(n):
            tile = Image.open(apk.asset("images", "maps", name, f"tile_{zoom}_{x:02d}_{y:02d}.jpg")).convert("RGB")
            img.paste(tile, (x * 256, y * 256))
    return img


def composite(name, scale=1):
    w, h, data = load(name)
    base = map_image(name)
    over = overlay_image(w, h, data)
    # premultiplied "over": out = src + dst * (1 - a)
    keep = Image.eval(base, lambda c: c * (255 - ALPHA) // 255)
    out = Image.frombytes("RGB", base.size, bytes(min(255, a + b) for a, b in zip(over.tobytes(), keep.tobytes())))
    if scale != 1:
        out = out.resize((out.width * scale, out.height * scale), Image.NEAREST)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("maps", nargs="*", default=MAPS)
    ap.add_argument("--scale", type=int, default=1)
    args = ap.parse_args()
    dest = os.path.join(apk.ROOT, "out", "debug")
    os.makedirs(dest, exist_ok=True)
    for name in args.maps:
        path = os.path.join(dest, f"collision_{name}.png")
        composite(name, args.scale).save(path)
        print(path)


if __name__ == "__main__":
    main()
