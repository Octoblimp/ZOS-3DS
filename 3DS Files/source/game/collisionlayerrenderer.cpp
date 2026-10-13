#include "game/collisionlayerrenderer.h"

#include <algorithm>
#include <stdexcept>
#include <vector>

#include "game/collisionlayer.h"
#include "game/utils.h"

using cocos2d::Mat4;
using cocos2d::Vec2;

void CollisionLayerRenderer::render(float viewX, float viewY, float mapScale, const Mat4& transform)
{
    if (!_visible || _layer == nullptr)
        return;
    if (!_texture.valid || _version != _layer->getVersion()) {
        deleteTexture();
        createTexture();
    }
    const int width = _layer->getWidth();
    const int height = _layer->getHeight();
    const float s = _density * mapScale;
    // The layer covers 4x4 map pixels a cell; on screen, clipped to it.
    const float left = std::max(-(s * viewX), 0.0f);
    const float right = std::min(s * ((float)(width << 2) - viewX), _screen.width);
    const float top = std::max(-(s * viewY), 0.0f);
    const float bottom = std::min(s * ((float)(height << 2) - viewY), _screen.height);
    if (left == right || top == bottom)
        return;
    // Screen pixel -> cell -> the texture, which is padded to powers of two.
    // createTextureFromData does not flip, so v runs down the map.
    const float u0 = (viewX + left / s) / 4.0f / (float)_textureWidth;
    const float u1 = (viewX + right / s) / 4.0f / (float)_textureWidth;
    const float v0 = (viewY + top / s) / 4.0f / (float)_textureHeight;
    const float v1 = (viewY + bottom / s) / 4.0f / (float)_textureHeight;
    const Vec2 a = transform.apply(Vec2(left, _screen.height - top));
    const Vec2 b = transform.apply(Vec2(right, _screen.height - bottom));
    gfx::bindTexture(&_texture, gfx::Combine::Modulate);
    gfx::quad(a.x, a.y, b.x, b.y, u0, v0, u1, v1, gfx::kWhite);
}

void CollisionLayerRenderer::createTexture()
{
    const int width = _layer->getWidth();
    const int height = _layer->getHeight();
    const std::vector<unsigned char>& data = _layer->getData();
    _textureWidth = Utils::roundUpToPowerOf2(width);
    _textureHeight = Utils::roundUpToPowerOf2(height);
    // The 3DS takes 1024 a side at most; a bigger layer is sampled every
    // step cells (a deviation; the game's is never shown anyway).
    int step = 1;
    while (_textureWidth / step > 1024 || _textureHeight / step > 1024)
        step <<= 1;
    const int tw = _textureWidth / step;
    const int th = _textureHeight / step;
    std::vector<unsigned char> pixels((size_t)tw * th * 4, 0xff);
    for (int y = 0; y < th && y * step < height; y++) {
        for (int x = 0; x < tw && x * step < width; x++) {
            const int value = data[(size_t)width * (y * step) + x * step];
            unsigned int colour;
            switch (value) {
            case 0x00: colour = 0xff0000; break;
            case 0x03: colour = 0x808080; break;
            case 0x07: colour = 0x008000; break;
            case 0x0f: colour = 0x0040ff; break;
            case 0x16: colour = 0xffffff; break;
            case 0x1f: colour = 0xd06000; break;
            case 0x22: colour = 0x800000; break;
            case 0x42: colour = 0xffc000; break;
            case 0x82: colour = 0x4090ff; break;
            default:
                throw std::runtime_error("Invalid collision value: " + Utils::intToString(value));
            }
            // Premultiplied by 3/8, the alpha of 0x60.
            unsigned char* p = &pixels[((size_t)y * tw + x) * 4];
            p[0] = (unsigned char)(((colour >> 16) & 0xff) * 3 >> 3);
            p[1] = (unsigned char)(((colour >> 8) & 0xff) * 3 >> 3);
            p[2] = (unsigned char)((colour & 0xff) * 3 >> 3);
            p[3] = 0x60;
        }
    }
    gfx::createTexture(&_texture, pixels.data(), tw, th, true);
    _version = _layer->getVersion();
}
