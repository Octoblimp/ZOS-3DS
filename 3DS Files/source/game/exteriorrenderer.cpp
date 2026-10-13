#include "game/exteriorrenderer.h"

#include <algorithm>

#include "gfx/gfx.h"

using cocos2d::Mat4;
using cocos2d::Vec2;

void ExteriorRenderer::render(float viewX, float viewY, float mapScale, const Mat4& transform)
{
    const float s = _density * mapScale;
    // The playing area's edges on screen.
    const float left = (10.0f - viewX) * s;
    const float top = (10.0f - viewY) * s;
    const float right = s * ((float)(_width - 10) - viewX);
    const float bottom = s * ((float)(_height - 10) - viewY);
    if (left <= 0.0f && top <= 0.0f && !(right < _screen.width) && !(bottom < _screen.height))
        return;
    const float l = left > 0.0f ? std::min(left, _screen.width) : 0.0f;
    const float r = right > 0.0f ? std::min(right, _screen.width) : 0.0f;
    const float t = top > 0.0f ? std::min(top, _screen.height) : 0.0f;
    const float b = bottom > 0.0f ? std::min(bottom, _screen.height) : 0.0f;
    gfx::bindTexture(nullptr, gfx::Combine::Color);
    // Above and below across the screen, then the sides between.
    if (t > 0.0f)
        drawBox(transform, 0.0f, _screen.width, 0.0f, t);
    if (b < _screen.height)
        drawBox(transform, 0.0f, _screen.width, b, _screen.height);
    if (t != b) {
        if (l > 0.0f)
            drawBox(transform, 0.0f, l, t, b);
        if (r < _screen.width)
            drawBox(transform, r, _screen.width, t, b);
    }
}

void ExteriorRenderer::drawBox(const Mat4& transform, float x0, float x1, float y0, float y1)
{
    const Vec2 a = transform.apply(Vec2(x0, _screen.height - y0));
    const Vec2 c = transform.apply(Vec2(x1, _screen.height - y1));
    gfx::quad(a.x, a.y, c.x, c.y, 0.0f, 0.0f, 0.0f, 0.0f, gfx::rgba(0, 0, 0, 255));
}
