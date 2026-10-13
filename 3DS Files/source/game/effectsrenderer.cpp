#include "game/effectsrenderer.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

#include "cocos/director.h"

using cocos2d::Mat4;
using cocos2d::Vec2;

namespace {

// A mark's size by its bomb (the .so's 0x5136ec).
const float kMarkScale[] = {1.0f, 2.0f};

void load(gfx::Texture* texture, const char* name)
{
    const std::string path = cocos2d::FileUtils::getInstance()->fullPathForFilename(name);
    if (!gfx::loadTexture(texture, path.c_str()))
        throw std::runtime_error(std::string("Failed to load file: ") + name);
}

} // namespace

EffectsRenderer::EffectsRenderer(float density) : _density(density)
{
    load(&_explosion01, "images/effects/explosion01.png");
    load(&_explosion02, "images/effects/explosion02.png");
    load(&_mark, "images/effects/mark01.png");
    load(&_marker, "images/effects/marker01.png");
    load(&_blast, "images/effects/blast01.png");
    load(&_plane01, "images/effects/plane01.png");
    load(&_planeShadow01, "images/effects/planeshadow01.png");
    load(&_plane02, "images/effects/plane02.png");
    load(&_planeShadow02, "images/effects/planeshadow02.png");
    load(&_parachute, "images/effects/parachute01.png");
    load(&_helicopterBody, "images/effects/helicopter/body01.png");
    load(&_helicopterShadow, "images/effects/helicopter/shadow01.png");
    load(&_rotorFast, "images/effects/helicopter/rotorfast01.png");
    load(&_rotorSlow, "images/effects/helicopter/rotorslow01.png");
    load(&_rotorFastShadow, "images/effects/helicopter/rotorfastshadow01.png");
    load(&_rotorSlowShadow, "images/effects/helicopter/rotorslowshadow01.png");
    _helicopterMaxWidth = std::max(std::max(_rotorFast.width, 256), _rotorSlow.width);
    _helicopterMaxHeight = std::max(std::max(_rotorFast.height, 64), _rotorSlow.height);
    _marks.reserve(20);
    _markers.reserve(8);
    _blasts.reserve(8);
    _explosions.reserve(16);
    _planes.reserve(16);
    _parachutes.reserve(16);
    _helicopters.reserve(8);
}

EffectsRenderer::~EffectsRenderer()
{
    for (gfx::Texture* t : {&_explosion01, &_explosion02, &_mark, &_marker, &_blast, &_plane01, &_plane02,
                            &_planeShadow01, &_planeShadow02, &_parachute, &_helicopterBody, &_helicopterShadow,
                            &_rotorFast, &_rotorSlow, &_rotorFastShadow, &_rotorSlowShadow})
        gfx::destroyTexture(t);
}

void EffectsRenderer::setupMap(int width, int height, const cocos2d::Size& screen)
{
    _width = width;
    _height = height;
    _screen = screen;
}

void EffectsRenderer::prepareRender(const Effects& effects, float viewX, float viewY, float mapScale, int t)
{
    _viewX = viewX;
    _viewY = viewY;
    _mapScale = mapScale;
    _gameTime = t;
    _markers.clear();
    _planes.clear();
    prepareBombsToRender(effects, t);
    prepareMarksToRender(effects, t);
    prepareSoldierDropsToRender(effects, t);
    prepareHelicoptersToRender(effects, t);
}

void EffectsRenderer::prepareBombsToRender(const Effects& effects, int t)
{
    _blasts.clear();
    _explosions.clear();
    const float s = _mapScale * _density;
    // Bombs within 192 map pixels of the screen: the explosion reaches in.
    const int minX = std::max((int)(_viewX - 192.0f), 0);
    const int minY = std::max((int)(_viewY - 192.0f), 0);
    const int maxX = std::min((int)(_viewX + _screen.width / s + 192.0f), _width - 1);
    const int maxY = std::min((int)(_viewY + _screen.height / s + 192.0f), _height - 1);
    const float planeHalfW = s * (float)_plane01.width * 0.5f;
    const float planeHalfH = s * (float)_plane01.height * 0.5f;
    for (const Effects::Bomb& bomb : effects.getActiveBombs()) {
        bool active;
        float px, py;
        effects.getBombPlaneInfo(bomb, t, active, px, py);
        if (active) {
            const float sx = s * (px - _viewX);
            const float sy = s * (py - _viewY);
            // The shadow is 220 x 120 up and left of the plane.
            if (sx >= -planeHalfW && sy >= -planeHalfH && sx < s * 220.0f + planeHalfW + _screen.width &&
                sy < s * 120.0f + planeHalfH + _screen.height)
                _planes.push_back({PlaneTypeBomb, sx, sy});
        }
        if (bomb.x < minX || bomb.x > maxX || bomb.y < minY || bomb.y > maxY)
            continue;
        const float sx = s * ((float)bomb.x - _viewX);
        const float sy = s * ((float)bomb.y - _viewY);
        if (bomb.getMarkerActive(t))
            _markers.push_back({sx, sy, MarkerTypeBomb});
        float scale, alpha;
        bomb.getBlastInfo(t, active, scale, alpha);
        if (active)
            _blasts.push_back({sx, sy, scale, alpha});
        float progress;
        bomb.getExplosionInfo(t, active, progress);
        if (active)
            _explosions.push_back({bomb.type, sx, sy, bomb.dirX, bomb.dirY, progress});
    }
}

void EffectsRenderer::prepareMarksToRender(const Effects& effects, int t)
{
    _marks.clear();
    const float s = _mapScale * _density;
    // A large bomb's mark is twice the texture.
    const float mw = (float)_mark.width;
    const float mh = (float)_mark.height;
    const float minX = (float)std::max((int)(_viewX - mw), 0);
    const float minY = (float)std::max((int)(_viewY - mh), 0);
    const float maxX = (float)std::min((int)(mw + _viewX + _screen.width / s), _width - 1);
    const float maxY = (float)std::min((int)(mh + _viewY + _screen.height / s), _height - 1);
    for (const Effects::Mark& mark : effects.getMarks()) {
        if (mark.x < minX || mark.x > maxX || mark.y < minY || mark.y > maxY)
            continue;
        _marks.push_back(
            {mark.type, s * (mark.x - _viewX), s * (mark.y - _viewY), mark.dirX, mark.dirY, mark.getAlpha(t)});
    }
}

void EffectsRenderer::prepareSoldierDropsToRender(const Effects& effects, int t)
{
    _parachutes.clear();
    const float s = _mapScale * _density;
    const float planeHalfW = s * (float)_plane02.width * 0.5f;
    const float planeHalfH = s * (float)_plane02.height * 0.5f;
    const float markerHalfW = s * (float)_marker.width * 0.5f;
    const float markerHalfH = s * (float)_marker.height * 0.5f;
    const float chuteHalfW = s * (float)_parachute.width * 0.5f;
    const float chuteHalfH = s * (float)_parachute.height * 0.5f;
    for (const Effects::SoldierDrop& drop : effects.getActiveSoldierDrops()) {
        if (drop.getMarkerActive()) {
            const float sx = s * ((float)drop.x - _viewX);
            const float sy = s * ((float)drop.y - _viewY);
            if (sx >= -markerHalfW && sy >= -markerHalfH && sx < markerHalfW + _screen.width &&
                sy < markerHalfH + _screen.height)
                _markers.push_back({sx, sy, MarkerTypeSoldierDrop});
        }
        bool active;
        float x, y, progress;
        drop.getParachuteInfo(t, active, x, y, progress);
        if (active) {
            const float sx = s * (x - _viewX);
            const float sy = s * (y - _viewY);
            if (sx >= -chuteHalfW && sy >= -chuteHalfH && sx < chuteHalfW + _screen.width &&
                sy < chuteHalfH + _screen.height)
                _parachutes.push_back({sx, sy, (int)(progress * 16.0f)});
        }
        effects.getSoldierDropPlaneInfo(drop, t, active, x, y);
        if (active) {
            const float sx = s * (x - _viewX);
            const float sy = s * (y - _viewY);
            if (sx >= -planeHalfW && sy >= -planeHalfH && sx < s * 220.0f + planeHalfW + _screen.width &&
                sy < s * 120.0f + planeHalfH + _screen.height)
                _planes.push_back({PlaneTypeSoldierDrop, sx, sy});
        }
    }
}

void EffectsRenderer::prepareHelicoptersToRender(const Effects& effects, int t)
{
    _helicopters.clear();
    const float s = _mapScale * _density;
    const float markerHalfW = s * (float)_marker.width * 0.5f;
    const float markerHalfH = s * (float)_marker.height * 0.5f;
    const float heliHalfW = s * (float)_helicopterMaxWidth * 0.8f * 0.5f;
    const float heliHalfH = s * (float)_helicopterMaxHeight * 0.8f * 0.5f;
    const float explosionHalf = s * 384.0f * 0.5f;
    for (const Effects::Helicopter& heli : effects.getActiveHelicopters()) {
        if (heli.getMarkerActive()) {
            const float sx = s * ((float)heli.x - _viewX);
            const float sy = s * ((float)heli.y - _viewY);
            if (sx >= -markerHalfW && sy >= -markerHalfH && sx < markerHalfW + _screen.width &&
                sy < markerHalfH + _screen.height)
                _markers.push_back({sx, sy, MarkerTypeHelicopter});
        }
        float x, y, height, rotation;
        bool rotorOn;
        Effects::Helicopter::State state;
        int stateTime;
        heli.getHelicopterInfo(t, x, y, height, rotation, rotorOn, state, stateTime);
        if (state != Effects::Helicopter::StateCrashed) {
            const float sx = s * (x - _viewX);
            const float sy = s * (y - _viewY);
            // The shadow trails up and left as it climbs.
            const float shadowX = s * -11.0f + (s * -110.0f - s * -11.0f) * height;
            const float shadowY = s * -6.0f + (s * -60.0f - s * -6.0f) * height;
            if (sx >= -heliHalfW && sy >= -heliHalfH && sx < (heliHalfW + _screen.width) - height * shadowX &&
                sy < (heliHalfH + _screen.height) - height * shadowY) {
                int frame;
                if (state == Effects::Helicopter::StateNormal) {
                    frame = 0;
                } else if (state == Effects::Helicopter::StateCrashing) {
                    // Frames 1..11 over 1.65 s, then 12..14 over and over.
                    const int elapsed = t - stateTime;
                    frame = elapsed < 1650 ? elapsed / 150 + 1 : (elapsed / 150 - 11) % 3 + 12;
                } else if (state == Effects::Helicopter::StateContaminated) {
                    frame = 15;
                } else {
                    throw std::runtime_error("Invalid helicopter state");
                }
                _helicopters.push_back({sx, sy, height, rotation, rotorOn, frame, height * 0.2f + 0.6f});
            }
        }
        bool active;
        float progress;
        heli.getCrashExplosionInfo(t, active, progress, x, y);
        if (active) {
            const float sx = s * (x - _viewX);
            const float sy = s * (y - _viewY);
            if (sx >= -explosionHalf && sy >= -explosionHalf && sx < explosionHalf + _screen.width &&
                sy < explosionHalf + _screen.height)
                _explosions.push_back({BombTypeSmall, sx, sy, heli.dirX, heli.dirY, progress});
        }
    }
}

void EffectsRenderer::doRenderUnderPeople(const Mat4& transform)
{
    doRenderMarks(transform);
    doRenderMarkers(transform);
    doRenderHelicopterBodyShadows(transform, true);
    doRenderHelicopterBodies(transform, true);
}

void EffectsRenderer::doRenderOverPeople(const Mat4& transform)
{
    doRenderParachutes(transform);
    doRenderHelicopterBodyShadows(transform, false);
    doRenderHelicopterRotorShadows(transform, false, _rotorSlowShadow);
    doRenderHelicopterRotorShadows(transform, true, _rotorFastShadow);
    doRenderPlaneShadows(transform, PlaneTypeSoldierDrop, _planeShadow02);
    doRenderPlaneShadows(transform, PlaneTypeBomb, _planeShadow01);
    doRenderBlasts(transform);
    doRenderHelicopterRotors(transform, true, false, _rotorSlow);
    doRenderHelicopterRotors(transform, true, true, _rotorFast);
    doRenderExplosions(transform, BombTypeSmall, _explosion01, 1.0f);
    doRenderExplosions(transform, BombTypeLarge, _explosion02, 1.5f);
    doRenderHelicopterBodies(transform, false);
    doRenderHelicopterRotors(transform, false, false, _rotorSlow);
    doRenderHelicopterRotors(transform, false, true, _rotorFast);
    doRenderPlanes(transform, PlaneTypeSoldierDrop, _plane02);
    doRenderPlanes(transform, PlaneTypeBomb, _plane01);
}

Vec2 EffectsRenderer::toNode(const Mat4& transform, float x, float y) const
{
    return transform.apply(Vec2(x, _screen.height - y));
}

void EffectsRenderer::quad(const Mat4& transform, float cx, float cy, float hw, float hh, float ax, float ay,
                           float u0, float v0, float u1, float v1, u32 colour) const
{
    auto corner = [&](float lx, float ly) { return toNode(transform, cx + (lx * ax - ly * ay), cy + (lx * ay + ly * ax)); };
    const Vec2 tl = corner(-hw, -hh), tr = corner(hw, -hh), bl = corner(-hw, hh), br = corner(hw, hh);
    gfx::Vertex* v = gfx::triangles(6);
    if (!v)
        return;
    v[0] = {tl.x, tl.y, u0, v0, colour};
    v[1] = {tr.x, tr.y, u1, v0, colour};
    v[2] = {bl.x, bl.y, u0, v1, colour};
    v[3] = {tr.x, tr.y, u1, v0, colour};
    v[4] = {br.x, br.y, u1, v1, colour};
    v[5] = {bl.x, bl.y, u0, v1, colour};
}

void EffectsRenderer::doRenderMarks(const Mat4& transform)
{
    if (_marks.empty() || !_mark.valid)
        return;
    gfx::bindTexture(&_mark);
    const float s = _mapScale * _density;
    for (const MarkRenderInfo& m : _marks) {
        const float hw = s * (float)_mark.width * 0.5f * kMarkScale[m.type];
        const float hh = s * (float)_mark.height * 0.5f * kMarkScale[m.type];
        // Turned to the plane's heading.
        quad(transform, m.x, m.y, hw, hh, m.dirX, m.dirY, 0.0f, 0.0f, 1.0f, 1.0f, gfx::alphaColor(m.alpha));
    }
}

void EffectsRenderer::doRenderMarkers(const Mat4& transform)
{
    if (_markers.empty() || !_marker.valid)
        return;
    gfx::bindTexture(&_marker);
    // 64-pixel frames, a row a kind, a frame every 100 ms.
    const int column = (_gameTime / 100) % (_marker.width / 64);
    const float half = _mapScale * _density * 64.0f * 0.5f;
    const float u0 = (float)column * 64.0f / (float)_marker.width;
    const float u1 = (float)(column + 1) * 64.0f / (float)_marker.width;
    for (const MarkerRenderInfo& m : _markers) {
        const float v0 = (float)m.type * 64.0f / (float)_marker.height;
        const float v1 = (float)(m.type + 1) * 64.0f / (float)_marker.height;
        quad(transform, m.x, m.y, half, half, 1.0f, 0.0f, u0, v0, u1, v1, gfx::kWhite);
    }
}

void EffectsRenderer::doRenderHelicopterBodyShadows(const Mat4& transform, bool onGround)
{
    if (_helicopters.empty() || !_helicopterShadow.valid)
        return;
    gfx::bindTexture(&_helicopterShadow);
    const float s = _mapScale * _density;
    const float hw = s * (float)_helicopterShadow.width * 0.5f * 0.6f;
    const float hh = s * (float)_helicopterShadow.height * 0.5f * 0.6f;
    for (const HelicopterRenderInfo& h : _helicopters) {
        if ((h.height <= 0.0f) != onGround)
            continue;
        const float cx = (s * -110.0f - s * -11.0f) * h.height + s * -11.0f + h.x;
        const float cy = (s * -60.0f - s * -6.0f) * h.height + s * -6.0f + h.y;
        quad(transform, cx, cy, hw, hh, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, gfx::kWhite);
    }
}

void EffectsRenderer::doRenderHelicopterBodies(const Mat4& transform, bool onGround)
{
    if (_helicopters.empty() || !_helicopterBody.valid)
        return;
    gfx::bindTexture(&_helicopterBody);
    const float s = _mapScale * _density;
    const int columns = _helicopterBody.width / 256;
    for (const HelicopterRenderInfo& h : _helicopters) {
        if ((h.height <= 0.0f) != onGround)
            continue;
        const float hw = s * 256.0f * 0.5f * h.scale;
        const float hh = s * 64.0f * 0.5f * h.scale;
        const int row = h.frame / columns;
        const int column = h.frame % columns;
        const float u0 = (float)column * 256.0f / (float)_helicopterBody.width;
        const float u1 = (float)(column + 1) * 256.0f / (float)_helicopterBody.width;
        const float v0 = (float)row * 64.0f / (float)_helicopterBody.height;
        const float v1 = (float)(row + 1) * 64.0f / (float)_helicopterBody.height;
        quad(transform, h.x, h.y, hw, hh, 1.0f, 0.0f, u0, v0, u1, v1, gfx::kWhite);
    }
}

void EffectsRenderer::doRenderParachutes(const Mat4& transform)
{
    if (_parachutes.empty() || !_parachute.valid)
        return;
    gfx::bindTexture(&_parachute);
    const float half = _mapScale * _density * 128.0f * 0.5f;
    const int columns = _parachute.width / 128;
    for (const ParachuteRenderInfo& p : _parachutes) {
        const int row = p.frame / columns;
        const int column = p.frame % columns;
        const float u0 = (float)column * 128.0f / (float)_parachute.width;
        const float u1 = (float)(column + 1) * 128.0f / (float)_parachute.width;
        const float v0 = (float)row * 128.0f / (float)_parachute.height;
        const float v1 = (float)(row + 1) * 128.0f / (float)_parachute.height;
        quad(transform, p.x, p.y, half, half, 1.0f, 0.0f, u0, v0, u1, v1, gfx::kWhite);
    }
}

void EffectsRenderer::doRenderHelicopterRotorShadows(const Mat4& transform, bool rotorOn,
                                                     const gfx::Texture& texture)
{
    if (_helicopters.empty() || !texture.valid)
        return;
    gfx::bindTexture(&texture);
    const float s = _mapScale * _density;
    const float hw = (float)texture.width * s * 0.5f * 0.6f;
    const float hh = (float)texture.height * s * 0.5f * 0.6f;
    for (const HelicopterRenderInfo& h : _helicopters) {
        if (h.rotorOn != rotorOn)
            continue;
        const float cx = s * -11.0f + h.x + (s * -110.0f - s * -11.0f) * h.height;
        const float cy = (s * -60.0f - s * -6.0f) * h.height + s * -6.0f + h.y;
        quad(transform, cx, cy, hw, hh, cosf(h.rotation), -sinf(h.rotation), 0.0f, 0.0f, 1.0f, 1.0f, gfx::kWhite);
    }
}

void EffectsRenderer::doRenderPlaneShadows(const Mat4& transform, PlaneType type, const gfx::Texture& texture)
{
    if (_planes.empty() || !texture.valid)
        return;
    gfx::bindTexture(&texture);
    const float s = _mapScale * _density;
    const float hw = (float)texture.width * s * 0.5f * 0.75f;
    const float hh = (float)texture.height * s * 0.5f * 0.75f;
    for (const PlaneRenderInfo& p : _planes) {
        if (p.type != type)
            continue;
        quad(transform, p.x - s * 220.0f, p.y - s * 120.0f, hw, hh, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f,
             gfx::kWhite);
    }
}

void EffectsRenderer::doRenderBlasts(const Mat4& transform)
{
    if (_blasts.empty() || !_blast.valid)
        return;
    gfx::bindTexture(&_blast);
    const float s = _mapScale * _density;
    for (const BlastRenderInfo& b : _blasts) {
        const float hw = s * (float)_blast.width * 0.5f * b.scale;
        const float hh = s * (float)_blast.height * 0.5f * b.scale;
        quad(transform, b.x, b.y, hw, hh, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, gfx::alphaColor(b.alpha));
    }
}

void EffectsRenderer::doRenderHelicopterRotors(const Mat4& transform, bool onGround, bool rotorOn,
                                               const gfx::Texture& texture)
{
    if (_helicopters.empty() || !texture.valid)
        return;
    gfx::bindTexture(&texture);
    const float s = _mapScale * _density;
    for (const HelicopterRenderInfo& h : _helicopters) {
        if ((h.height <= 0.0f) != onGround || h.rotorOn != rotorOn)
            continue;
        const float hw = (float)texture.width * s * 0.5f * h.scale;
        const float hh = (float)texture.height * s * 0.5f * h.scale;
        quad(transform, h.x, h.y, hw, hh, cosf(h.rotation), -sinf(h.rotation), 0.0f, 0.0f, 1.0f, 1.0f, gfx::kWhite);
    }
}

void EffectsRenderer::doRenderExplosions(const Mat4& transform, BombType type, const gfx::Texture& texture,
                                         float sizeScale)
{
    if (_explosions.empty() || !texture.valid)
        return;
    gfx::bindTexture(&texture);
    const float half = _mapScale * _density * 256.0f * sizeScale * 0.5f;
    for (const ExplosionRenderInfo& e : _explosions) {
        if (e.type != type)
            continue;
        // An 8x8 grid of 64 frames over the explosion's life.
        const int frame = (int)(e.progress * 64.0f);
        const float u0 = (float)(frame % 8) * 0.125f;
        const float v0 = (float)(frame / 8) * 0.125f;
        quad(transform, e.x, e.y, half, half, e.dirX, e.dirY, u0, v0, u0 + 0.125f, v0 + 0.125f, gfx::kWhite);
    }
}

void EffectsRenderer::doRenderPlanes(const Mat4& transform, PlaneType type, const gfx::Texture& texture)
{
    if (_planes.empty() || !texture.valid)
        return;
    gfx::bindTexture(&texture);
    const float s = _mapScale * _density;
    const float hw = (float)texture.width * s * 0.5f;
    const float hh = (float)texture.height * s * 0.5f;
    for (const PlaneRenderInfo& p : _planes) {
        if (p.type != type)
            continue;
        quad(transform, p.x, p.y, hw, hh, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, gfx::kWhite);
    }
}
