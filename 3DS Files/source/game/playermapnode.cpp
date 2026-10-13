#include "game/playermapnode.h"

#include <algorithm>
#include <cmath>

#include "game/gamenode.h"
#include "game/guiglobals.h"
#include "game/maprenderer.h"
#include "game/playermaps.h"
#include "gfx/gfx.h"

using namespace cocos2d;

namespace {

const double kWorld = 268435456.0; // 2^28: zoom-20 pixels around the world
// The view's start, the game's: the North Atlantic, the Americas to the left
// and Europe and Africa to the right.
const double kStartX = 8.321499e7;
const double kStartY = 1.0200547e8;
// The imagery's lowest level, the game's Google Maps minimum: the world 512
// pixels across at the most zoomed out.
const int kMinZoom = 1;
const double kAnimDuration = 0.2;
// The port's buttons, as GameNode's (L/R faster: the world to a street is
// fourteen doublings).
const float kZoomRate = 2.5f;
const float kPanSpeed = 420.0f;
const int kCirclePadMax = 156;
const int kCirclePadDeadZone = 16;

} // namespace

PlayerMapNode* PlayerMapNode::create(GameNode* game, const Rect& rect)
{
    PlayerMapNode* node = new PlayerMapNode();
    if (!node->init(game, rect)) {
        delete node;
        return nullptr;
    }
    node->autorelease();
    return node;
}

bool PlayerMapNode::init(GameNode* game, const Rect& rect)
{
    if (!Node::init())
        return false;
    _game = game;
    _rect = rect;
    _screen = Director::getInstance()->getVisibleSize();
    setContentSize(_screen);

    // The whole world across the map, at the most what the imagery goes down
    // to, centred on the game's start.
    const float dp = GUIGlobals::dpToSize;
    _minScale = std::max((float)(rect.size.width / dp / kWorld), (float)(1.0 / std::exp2(20.0 - kMinZoom)));
    _scale = _minScale;
    _viewX = kStartX - (rect.origin.x + rect.size.width * 0.5f) / dp / _scale;
    _viewY = kStartY - (_screen.height - rect.origin.y - rect.size.height * 0.5f) / dp / _scale;
    clampVisibleArea();

    _squares = DrawNode::create(dp * 2.0f);
    addChild(_squares, 0);
    scheduleUpdate();
    return true;
}

void PlayerMapNode::onExit()
{
    clear();
    Node::onExit();
}

void PlayerMapNode::clear()
{
    if (_setUp) {
        _game->getMapRenderer()->clear();
        _setUp = false;
    }
}

Vec2 PlayerMapNode::toScreen(double worldX, double worldY) const
{
    const double pixels = (double)_scale * GUIGlobals::dpToSize;
    return Vec2((float)((worldX - _viewX) * pixels), (float)((worldY - _viewY) * pixels));
}

void PlayerMapNode::clampVisibleArea()
{
    // The map's rectangle stays on the world.
    const double pixels = (double)_scale * GUIGlobals::dpToSize;
    const double left = _rect.getMinX() / pixels;
    const double right = _rect.getMaxX() / pixels;
    const double top = (_screen.height - _rect.getMaxY()) / pixels;
    const double bottom = (_screen.height - _rect.getMinY()) / pixels;
    if (right - left >= kWorld)
        _viewX = (kWorld - left - right) * 0.5;
    else
        _viewX = std::min(std::max(_viewX, -left), kWorld - right);
    if (bottom - top >= kWorld)
        _viewY = (kWorld - top - bottom) * 0.5;
    else
        _viewY = std::min(std::max(_viewY, -top), kWorld - bottom);
}

void PlayerMapNode::draw(Renderer* renderer, const Mat4& transform, u32 flags)
{
    Node::draw(renderer, transform, flags);
    MapRenderer* map = _game->getMapRenderer();
    if (!_setUp) {
        map->setupMap(MapRenderer::MapType::Online, 0, 0, 0);
        _setUp = true;
    }
    gfx::blendPremultiplied();
    map->render(_viewX, _viewY, _scale, transform);
}

void PlayerMapNode::update(float dt)
{
    if (!isVisible() || !getParent() || !getParent()->isVisible())
        return;
    updateAnimatedZoom();

    // The port's: L/R zoom about the middle, the circle pad and D-pad pan.
    const u32 held = Director::getInstance()->buttonsHeld();
    const Vec2 middle(_rect.getMidX(), _screen.height - _rect.getMidY());
    if (!_animating && (held & (KEY_L | KEY_R))) {
        float scale = _scale;
        if (held & KEY_R)
            scale *= std::pow(2.0f, dt * kZoomRate);
        if (held & KEY_L)
            scale /= std::pow(2.0f, dt * kZoomRate);
        zoomAbout(scale, middle);
    }
    circlePosition pad;
    hidCircleRead(&pad);
    float dx = 0.0f, dy = 0.0f;
    if (std::abs(pad.dx) > kCirclePadDeadZone || std::abs(pad.dy) > kCirclePadDeadZone) {
        dx = std::max(-1.0f, std::min(1.0f, (float)pad.dx / kCirclePadMax));
        dy = -std::max(-1.0f, std::min(1.0f, (float)pad.dy / kCirclePadMax));
    }
    if (held & KEY_DLEFT)
        dx = -1.0f;
    if (held & KEY_DRIGHT)
        dx = 1.0f;
    if (held & KEY_DUP)
        dy = -1.0f;
    if (held & KEY_DDOWN)
        dy = 1.0f;
    if (!_animating && (dx != 0.0f || dy != 0.0f)) {
        const double pixels = (double)_scale * GUIGlobals::dpToSize;
        _viewX += dx * kPanSpeed * dt / pixels;
        _viewY += dy * kPanSpeed * dt / pixels;
        clampVisibleArea();
    }
    updateMarkers();
}

void PlayerMapNode::zoomAbout(float scale, const Vec2& about)
{
    scale = std::max(std::min(scale, 1.0f), _minScale);
    const double dp = GUIGlobals::dpToSize;
    const double x = _viewX + about.x / (_scale * dp);
    const double y = _viewY + about.y / (_scale * dp);
    _scale = scale;
    _viewX = x - about.x / (scale * dp);
    _viewY = y - about.y / (scale * dp);
    clampVisibleArea();
}

void PlayerMapNode::startAnimatedZoom(double worldX, double worldY, float scale, const Vec2& to)
{
    _animFrom = _scale;
    _animTo = std::max(std::min(scale, 1.0f), _minScale);
    _animX = worldX;
    _animY = worldY;
    _animStart = toScreen(worldX, worldY);
    _animEnd = to;
    _animTimer.reset();
    _animating = true;
}

void PlayerMapNode::updateAnimatedZoom()
{
    if (!_animating)
        return;
    double t = _animTimer.getTime() / kAnimDuration;
    if (t >= 1.0) {
        t = 1.0;
        _animating = false;
    }
    _scale = (float)(_animFrom + (_animTo - _animFrom) * t);
    const Vec2 at = _animStart + (_animEnd - _animStart) * (float)t;
    const double pixels = (double)_scale * GUIGlobals::dpToSize;
    _viewX = _animX - at.x / pixels;
    _viewY = _animY - at.y / pixels;
    clampVisibleArea();
}

void PlayerMapNode::updateMarkers()
{
    const std::vector<PlayerMaps::Map>& maps = PlayerMaps::getInstance()->getMaps();
    while (_markers.size() < maps.size()) {
        Sprite* marker = Sprite::createWithSpriteFrameName("UnlockedMapMarker.png");
        addChild(marker, 1);
        _markers.push_back(marker);
    }
    // A deleted map's.
    while (_markers.size() > maps.size()) {
        _markers.back()->removeFromParent();
        _markers.pop_back();
    }
    const float dp = GUIGlobals::dpToSize;
    const double pixels = (double)_scale * dp;
    const float minSize = dp * 8.0f;
    const Color3B red(0xaf, 0x22, 0x22);
    const Color4F fill(red.r / 255.0f, red.g / 255.0f, red.b / 255.0f, 0.1f);
    const Color4F outline(red.r / 255.0f, red.g / 255.0f, red.b / 255.0f, 0.5f);
    const float angle = (float)std::fmod(_timer.getTime() * -90.0, 360.0);
    _squares->clear();
    for (size_t i = 0; i < maps.size(); i++) {
        const PlayerMaps::Map& m = maps[i];
        const Vec2 c = toScreen(m.worldX + m.size * 0.5, m.worldY + m.size * 0.5);
        const Vec2 p(c.x, _screen.height - c.y);
        const float half = (float)(m.size * pixels * 0.5);
        if (half * 2.0f >= minSize) {
            _squares->drawSolidRect(p - Vec2(half, half), p + Vec2(half, half), fill);
            _squares->drawRect(p - Vec2(half, half), p + Vec2(half, half), outline);
        }
        _markers[i]->setPosition(p);
        _markers[i]->setRotation(angle + (float)i * 36.0f);
    }

    // The new map's square in the middle, or where it will be while too
    // small to see.
    if (_picking) {
        const Vec2 p(_rect.getMidX(), _rect.getMidY());
        const Color4F yellow(1.0f, 0.85f, 0.1f, 1.0f);
        const float half = (float)(_pickSize * pixels * 0.5);
        if (pickVisible()) {
            _squares->drawSolidRect(p - Vec2(half, half), p + Vec2(half, half), Color4F(1.0f, 0.85f, 0.1f, 0.15f));
            _squares->drawRect(p - Vec2(half, half), p + Vec2(half, half), yellow);
        } else {
            const float gap = dp * 3.0f, arm = dp * 10.0f;
            _squares->drawLine(p + Vec2(gap, 0.0f), p + Vec2(arm, 0.0f), yellow);
            _squares->drawLine(p - Vec2(gap, 0.0f), p - Vec2(arm, 0.0f), yellow);
            _squares->drawLine(p + Vec2(0.0f, gap), p + Vec2(0.0f, arm), yellow);
            _squares->drawLine(p - Vec2(0.0f, gap), p - Vec2(0.0f, arm), yellow);
        }
    }
}

void PlayerMapNode::setPicking(bool picking, int size)
{
    _picking = picking;
    _pickSize = size;
    if (picking && _scale < pickScale(size) && !_animating) {
        const Vec2 middle(_rect.getMidX(), _screen.height - _rect.getMidY());
        const double pixels = (double)_scale * GUIGlobals::dpToSize;
        startAnimatedZoom(_viewX + middle.x / pixels, _viewY + middle.y / pixels, pickScale(size), middle);
    }
}

float PlayerMapNode::pickScale(int size) const
{
    return _rect.size.height * 0.5f / GUIGlobals::dpToSize / (float)size;
}

bool PlayerMapNode::pickVisible() const
{
    return (float)_pickSize * _scale * GUIGlobals::dpToSize >= GUIGlobals::dpToSize * 8.0f;
}

void PlayerMapNode::showPlace(double x, double y, double width, double height)
{
    const float dp = GUIGlobals::dpToSize;
    float scale;
    if (_picking) {
        scale = pickScale(_pickSize);
    } else {
        const double fitX = _rect.size.width / dp / std::max(width * 1.25, 1.0);
        const double fitY = _rect.size.height / dp / std::max(height * 1.25, 1.0);
        scale = std::min((float)std::min(fitX, fitY), pickScale(PlayerMaps::kLargeSize));
    }
    // A glide when the place is on screen; from elsewhere in the world, a
    // jump (gliding there would ask for every tile on the way).
    const Vec2 middle(_rect.getMidX(), _screen.height - _rect.getMidY());
    const Vec2 at = toScreen(x, y);
    if (_rect.containsPoint(Vec2(at.x, _screen.height - at.y))) {
        startAnimatedZoom(x, y, scale, middle);
        return;
    }
    _animating = false;
    _scale = std::max(std::min(scale, 1.0f), _minScale);
    const double pixels = (double)_scale * dp;
    _viewX = x - middle.x / pixels;
    _viewY = y - middle.y / pixels;
    clampVisibleArea();
}

void PlayerMapNode::getPickCentre(int& x, int& y) const
{
    const double pixels = (double)_scale * GUIGlobals::dpToSize;
    x = (int)std::floor(_viewX + _rect.getMidX() / pixels);
    y = (int)std::floor(_viewY + (_screen.height - _rect.getMidY()) / pixels);
}

void PlayerMapNode::touchBegan(Touch*)
{
    _movedX = 0.0f;
    _movedY = 0.0f;
}

void PlayerMapNode::touchMoved(Touch* touch)
{
    // GestureUtils::handlePan: the world follows the stylus.
    const Vec2 delta = touch->getDelta();
    const double pixels = (double)_scale * GUIGlobals::dpToSize;
    _animating = false;
    _viewX -= delta.x / pixels;
    _viewY += delta.y / pixels;
    clampVisibleArea();
    _movedX += std::abs(delta.x);
    _movedY += std::abs(delta.y);
}

void PlayerMapNode::touchEnded(Touch* touch)
{
    // A tap: within 5 dp of where it began, and not wandering on the way.
    const float dp = GUIGlobals::dpToSize;
    const float tap = dp * 5.0f * dp * 5.0f;
    const Vec2 location = touch->getLocation();
    if ((location - touch->getStartLocation()).lengthSquared() > tap ||
        _movedX * _movedX + _movedY * _movedY >= tap)
        return;
    const Vec2 p(location.x, _screen.height - location.y);
    const double pixels = (double)_scale * dp;
    const double worldX = _viewX + p.x / pixels;
    const double worldY = _viewY + p.y / pixels;

    // The game's: a marker within 24 dp plays its map; two zoom in on them.
    // (Not while picking: the stylus aims there.)
    if (!_picking) {
        const float reach = dp * 24.0f;
        const std::vector<PlayerMaps::Map>& maps = PlayerMaps::getInstance()->getMaps();
        int hit = -1;
        for (size_t i = 0; i < maps.size(); i++) {
            const Vec2 c = toScreen(maps[i].worldX + maps[i].size * 0.5, maps[i].worldY + maps[i].size * 0.5);
            if ((c - p).lengthSquared() >= reach * reach)
                continue;
            if (hit >= 0) {
                startAnimatedZoom(worldX, worldY, _scale * 2.0f, p);
                return;
            }
            hit = (int)i;
            if (_scale >= 1.0f)
                break;
        }
        if (hit >= 0) {
            if (_listener)
                _listener->onPlayerMapSelected(maps[hit].id);
            return;
        }
    }

    // The port's: zoom in twice, the spot to the middle.
    startAnimatedZoom(worldX, worldY, _scale * 2.0f, Vec2(_rect.getMidX(), _screen.height - _rect.getMidY()));
}
