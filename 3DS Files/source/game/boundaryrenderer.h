// BoundaryRenderer (decomp/classes/BoundaryRenderer.c): a 5-pixel blue line
// just outside the playing area's edges, where they are on screen.
//
// BoundaryLine.fsh's colour, (0x20, 0x80, 0xff) at 0.7, is not
// premultiplied; under the premultiplied blend it adds its whole colour and
// keeps 0.3 of what is under it. The vertex colour here is the same four
// numbers, so the blend comes out the same.
#pragma once

#include "cocos/base.h"

class BoundaryRenderer {
public:
    explicit BoundaryRenderer(float density) : _density(density) {}

    // The map, in map pixels.
    void setupMap(int width, int height)
    {
        _width = width;
        _height = height;
    }
    void onSurfaceCreated() {}
    // The screen, in pixels.
    void onSurfaceChanged(int width, int height) { _screen = cocos2d::Size((float)width, (float)height); }
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
