// EffectsRenderer (decomp/classes/EffectsRenderer.c): the player's weapons as
// drawn, part under the people and part over them. Under: scorch marks, the
// target markers, and a helicopter on the ground (shadow and body). Over:
// parachutes, a flying helicopter's shadows, the planes' shadows, blasts,
// rotors, explosions, a flying helicopter's body and rotors, the planes.
//
// SimulationRenderer owns one. prepareRender takes what is in view (with a
// margin for what reaches in: shadows, rotors, explosions) into the
// *RenderInfo lists, in screen pixels from the top left; doRender* draws
// them. The game's two shaders, EffectDecal (the texel) and
// EffectAlphaMult (the texel times an alpha), are the vertex colour here.
#pragma once

#include <vector>

#include "cocos/base.h"
#include "game/effects.h"
#include "gfx/gfx.h"

class EffectsRenderer {
public:
    // Loads the textures. density is the screen's (1 on the 3DS).
    explicit EffectsRenderer(float density);
    ~EffectsRenderer();

    EffectsRenderer(const EffectsRenderer&) = delete;
    EffectsRenderer& operator=(const EffectsRenderer&) = delete;

    // The map, in map pixels, and the screen, in pixels.
    void setupMap(int width, int height, const cocos2d::Size& screen);
    // viewX, viewY is the map pixel at the screen's top left; mapScale the
    // zoom; t the game time.
    void prepareRender(const Effects& effects, float viewX, float viewY, float mapScale, int t);
    // transform is the node's (its space is the screen's, y up).
    void doRenderUnderPeople(const cocos2d::Mat4& transform);
    void doRenderOverPeople(const cocos2d::Mat4& transform);

private:
    enum PlaneType {
        PlaneTypeBomb,
        PlaneTypeSoldierDrop,
    };

    // marker01's rows.
    enum MarkerType {
        MarkerTypeBomb,
        MarkerTypeSoldierDrop,
        MarkerTypeHelicopter,
    };

    // 0x18 bytes.
    struct MarkRenderInfo {
        BombType type;
        float x, y;
        float dirX, dirY;
        float alpha;
    };

    // 0x0c bytes.
    struct MarkerRenderInfo {
        float x, y;
        MarkerType type;
    };

    // 0x10 bytes.
    struct BlastRenderInfo {
        float x, y;
        float scale;
        float alpha;
    };

    // 0x18 bytes. A helicopter's crash is a small bomb's explosion.
    struct ExplosionRenderInfo {
        BombType type;
        float x, y;
        float dirX, dirY;
        float progress;
    };

    // 0x0c bytes.
    struct PlaneRenderInfo {
        PlaneType type;
        float x, y;
    };

    // 0x0c bytes.
    struct ParachuteRenderInfo {
        float x, y;
        // parachute01's 128-pixel frames, 0..15 as it falls.
        int frame;
    };

    // 0x1c bytes.
    struct HelicopterRenderInfo {
        float x, y;
        // 0 on the ground .. 1 flying.
        float height;
        float rotation;
        bool rotorOn;
        // body01's 256x64 frames: 0 whole, 1..14 crashing and burning, 15
        // contaminated.
        int frame;
        // Bigger the higher: 0.6 .. 0.8.
        float scale;
    };

    void prepareBombsToRender(const Effects& effects, int t);
    void prepareMarksToRender(const Effects& effects, int t);
    void prepareSoldierDropsToRender(const Effects& effects, int t);
    void prepareHelicoptersToRender(const Effects& effects, int t);

    void doRenderMarks(const cocos2d::Mat4& transform);
    void doRenderMarkers(const cocos2d::Mat4& transform);
    // onGround: those at height 0, or those above it.
    void doRenderHelicopterBodyShadows(const cocos2d::Mat4& transform, bool onGround);
    void doRenderHelicopterBodies(const cocos2d::Mat4& transform, bool onGround);
    void doRenderParachutes(const cocos2d::Mat4& transform);
    // Those whose rotor turns (rotorOn), or not.
    void doRenderHelicopterRotorShadows(const cocos2d::Mat4& transform, bool rotorOn, const gfx::Texture& texture);
    void doRenderPlaneShadows(const cocos2d::Mat4& transform, PlaneType type, const gfx::Texture& texture);
    void doRenderBlasts(const cocos2d::Mat4& transform);
    void doRenderHelicopterRotors(const cocos2d::Mat4& transform, bool onGround, bool rotorOn,
                                  const gfx::Texture& texture);
    // sizeScale: 1 for a small bomb's, 1.5 for a large one's.
    void doRenderExplosions(const cocos2d::Mat4& transform, BombType type, const gfx::Texture& texture,
                            float sizeScale);
    void doRenderPlanes(const cocos2d::Mat4& transform, PlaneType type, const gfx::Texture& texture);

    // A quad of half size hw x hh about the screen point (cx, cy), its x
    // axis along (ax, ay) and its y axis along (-ay, ax); the image's
    // u0..u1, v0..v1 across it.
    void quad(const cocos2d::Mat4& transform, float cx, float cy, float hw, float hh, float ax, float ay, float u0,
              float v0, float u1, float v1, u32 colour) const;
    // A screen point (y down) in the node's space.
    cocos2d::Vec2 toNode(const cocos2d::Mat4& transform, float x, float y) const;

    float _density;                     // 0x00
    cocos2d::Size _screen;              // 0x04
    // The map, in map pixels.
    int _width = -1;                    // 0x14
    int _height = -1;                   // 0x18
    gfx::Texture _explosion01;          // 0x2c
    gfx::Texture _explosion02;          // 0x30
    gfx::Texture _mark;                 // 0x58
    gfx::Texture _marker;               // 0x64
    gfx::Texture _blast;                // 0x70
    gfx::Texture _plane01;              // 0x7c
    gfx::Texture _planeShadow01;        // 0x88
    gfx::Texture _plane02;              // 0x94
    gfx::Texture _planeShadow02;        // 0xa0
    gfx::Texture _parachute;            // 0xac
    gfx::Texture _helicopterBody;       // 0xb8
    gfx::Texture _helicopterShadow;     // 0xc4
    gfx::Texture _rotorFast;            // 0xd0
    gfx::Texture _rotorSlow;            // 0xdc
    gfx::Texture _rotorFastShadow;      // 0xe8
    gfx::Texture _rotorSlowShadow;      // 0xf4
    // The largest of a body frame and the rotors, for the margin.
    int _helicopterMaxWidth;            // 0x100
    int _helicopterMaxHeight;           // 0x104
    float _viewX = -1;                  // 0x108
    float _viewY = -1;                  // 0x10c
    float _mapScale = -1;               // 0x110
    int _gameTime = -1;                 // 0x114
    std::vector<MarkRenderInfo> _marks;             // 0x118
    std::vector<MarkerRenderInfo> _markers;         // 0x124
    std::vector<BlastRenderInfo> _blasts;           // 0x130
    std::vector<ExplosionRenderInfo> _explosions;   // 0x13c
    std::vector<PlaneRenderInfo> _planes;           // 0x148
    std::vector<ParachuteRenderInfo> _parachutes;   // 0x154
    std::vector<HelicopterRenderInfo> _helicopters; // 0x160
};
