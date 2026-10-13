// A JPEG reader for the online maps' satellite tiles: baseline
// and extended sequential Huffman JPEGs, 8 bits, grey or YCbCr, any chroma
// sampling, with restart intervals and scans interleaved or not. Not
// progressive or arithmetic-coded images (Esri's tiles are baseline). Safe
// on any thread.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <3ds/types.h>

namespace jpeg {

// RGBA, 8 bits a channel, rows top down, alpha 255. False with error set if
// it is not a JPEG this reads.
bool decode(const u8* data, size_t size, std::vector<u8>& rgba, int& width, int& height, std::string& error);

// RGB565 in the GPU's tiled order (gfx::createTiledTexture's): 8 x 8 tiles,
// each in Morton order, the tiles in rows from the image's top. The image's
// sides must be multiples of 8.
bool decodeTiledRGB565(const u8* data, size_t size, std::vector<u16>& pixels, int& width, int& height,
                       std::string& error);

// Whether data starts with JPEG's start-of-image marker.
bool isJpeg(const u8* data, size_t size);

} // namespace jpeg
