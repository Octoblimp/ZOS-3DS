// ExteriorRenderer (decomp/classes/ExteriorRenderer.c): opaque black over
// whatever of the screen is outside the playing area, which ends 10 map
// pixels inside the map's edges.
#pragma once

#include "cocos/base.h"

class ExteriorRenderer {
public:
    // The screen, in pixels.
    ExteriorRenderer(float density, const cocos2d::Size& screen) : _density(density), _screen(screen) {}

    // The map, in map pixels.
    void setupMap(int width, int height)
    {
        _width = width;
        _height = height;
    }
    // viewX, viewY is the map pixel at the screen's top left; transform is
    // the node's (its space is the screen's, y up).
    void render(float viewX, float viewY, float mapScale, const cocos2d::Mat4& transform);

private:
    // In screen pixels from the top left.
    void drawBox(const cocos2d::Mat4& transform, float x0, float x1, float y0, float y1);

    float _density;             // 0x00
    int _width = -1;            // 0x04
    int _height = -1;           // 0x08
    cocos2d::Size _screen;      // 0x0c
};
