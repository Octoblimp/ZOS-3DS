#include "game/boundaryrenderer.h"

#include <algorithm>

#include "gfx/gfx.h"

using cocos2d::Mat4;
using cocos2d::Vec2;

void BoundaryRenderer::render(float viewX, float viewY, float mapScale, const Mat4& transform)
{
    const float s = _density * mapScale;
    // The playing area on screen, clipped to it.
    const float left = s * viewX <= 0.0f ? -(s * viewX) : 0.0f;
    const float right = std::min(s * ((float)_width - viewX), _screen.width);
    const float top = s * viewY <= 0.0f ? -(s * viewY) : 0.0f;
    const float bottom = std::min(s * ((float)_height - viewY), _screen.height);
    if (left == right || top == bottom)
        return;
    gfx::bindTexture(nullptr, gfx::Combine::Color);
    if (left > 0.0f)
        drawBox(transform, std::max(left - 5.0f, 0.0f), left, top, bottom);
    if (right < _screen.width)
        drawBox(transform, right, std::min(right + 5.0f, _screen.width), top, bottom);
    if (top > 0.0f)
        drawBox(transform, std::max(left - 5.0f, 0.0f), std::min(right + 5.0f, _screen.width),
                std::max(top - 5.0f, 0.0f), top);
    if (bottom < _screen.height)
        drawBox(transform, std::max(left - 5.0f, 0.0f), std::min(right + 5.0f, _screen.width), bottom,
                std::min(bottom + 5.0f, _screen.height));
}

void BoundaryRenderer::drawBox(const Mat4& transform, float x0, float x1, float y0, float y1)
{
    const Vec2 a = transform.apply(Vec2(x0, _screen.height - y0));
    const Vec2 b = transform.apply(Vec2(x1, _screen.height - y1));
    gfx::quad(a.x, a.y, b.x, b.y, 0.0f, 0.0f, 0.0f, 0.0f, gfx::rgba(0x20, 0x80, 0xff, 179));
}
