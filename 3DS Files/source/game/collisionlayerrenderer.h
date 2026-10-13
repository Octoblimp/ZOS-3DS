// CollisionLayerRenderer (decomp/classes/CollisionLayerRenderer.c): a debug
// overlay tinting each collision cell by its value. It starts hidden and
// nothing in the game calls toggleVisibility, so it is never seen; it is
// ported for completeness.
#pragma once

#include "cocos/base.h"
#include "gfx/gfx.h"

class CollisionLayer;

class CollisionLayerRenderer {
public:
    explicit CollisionLayerRenderer(float density) : _density(density) {}
    ~CollisionLayerRenderer() { deleteTexture(); }
    CollisionLayerRenderer(const CollisionLayerRenderer&) = delete;
    CollisionLayerRenderer& operator=(const CollisionLayerRenderer&) = delete;

    void onSurfaceCreated() {}
    // The screen, in pixels.
    void onSurfaceChanged(int width, int height) { _screen = cocos2d::Size((float)width, (float)height); }
    void setCollisionLayer(const CollisionLayer* layer)
    {
        _layer = layer;
        deleteTexture();
    }
    // viewX, viewY is the map pixel at the screen's top left; transform is
    // the node's (its space is the screen's, y up).
    void render(float viewX, float viewY, float mapScale, const cocos2d::Mat4& transform);
    void toggleVisibility() { _visible = !_visible; }

private:
    void createTexture();
    void deleteTexture() { gfx::destroyTexture(&_texture); }

    float _density;                         // 0x00
    const CollisionLayer* _layer = nullptr; // 0x04
    cocos2d::Size _screen;                  // 0x08
    bool _visible = false;                  // 0x10
    gfx::Texture _texture;                  // 0x24
    // The texture's size in cells: the layer's, rounded up to powers of two.
    int _textureWidth = 0;                  // 0x28
    int _textureHeight = 0;                 // 0x2c
    int _version = 0;                       // 0x30
};
