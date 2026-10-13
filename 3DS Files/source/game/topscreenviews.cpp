#include "game/topscreenviews.h"

#include <3ds.h>

#include <algorithm>
#include <cmath>

#include "game/gamenode.h"
#include "game/maprenderer.h"
#include "game/person.h"
#include "gfx/gfx.h"

using namespace cocos2d;

namespace {

const float kTopHeight = 240.0f;
// PopulationGraph's samples before it halves them.
const size_t kMaxSamples = 128;

const Color4F kCivColor(0.92f, 0.92f, 0.92f, 1.0f);
const Color4F kArmedColor(0.35f, 0.65f, 1.0f, 1.0f);
const Color4F kZombieColor(0.95f, 0.25f, 0.2f, 1.0f);
const Color4F kDeadColor(0.5f, 0.5f, 0.5f, 1.0f);

} // namespace

// ------------------------------------------------------------------ MiniMap

MiniMap* MiniMap::create(GameNode* game)
{
    MiniMap* node = new MiniMap();
    if (!node->init(game)) {
        delete node;
        return nullptr;
    }
    node->autorelease();
    return node;
}

bool MiniMap::init(GameNode* game)
{
    if (!Node::init())
        return false;
    _game = game;
    _dots = DrawNode::create(1.0f);
    addChild(_dots, 1);
    return true;
}

void MiniMap::fitInto(float width, float height)
{
    const MapConfig* config = _game->getMapConfig();
    if (config == nullptr || config->width <= 0 || config->height <= 0 || width <= 0.0f || height <= 0.0f) {
        _pixels = 0.0f;
        setContentSize(Size::ZERO);
        return;
    }
    _pixels = std::min(width / (float)config->width, height / (float)config->height);
    const Size size(std::floor((float)config->width * _pixels), std::floor((float)config->height * _pixels));
    if (size.equals(getContentSize()))
        return;
    setContentSize(size);
    // Black under the imagery while its tiles come.
    removeChildByTag(1);
    DrawNode* back = DrawNode::create();
    back->drawSolidRect(Vec2::ZERO, Vec2(size.width, size.height), Color4F(0.0f, 0.0f, 0.0f, 1.0f));
    addChild(back, -1, 1);
    refresh();
}

void MiniMap::draw(Renderer* renderer, const Mat4& transform, u32 flags)
{
    Node::draw(renderer, transform, flags);
    const MapConfig* config = _game->getMapConfig();
    MapRenderer* map = _game->getMapRenderer();
    const Size size = getContentSize();
    if (config == nullptr || map == nullptr || _pixels <= 0.0f)
        return;
    // The renderer covers whole tiles: clipped to the box, in the top
    // screen's own pixels (the scissor's y from the top).
    const Vec2 a = transform.apply(Vec2::ZERO);
    const Vec2 b = transform.apply(Vec2(size.width, size.height));
    const int x0 = (int)std::floor(a.x + 0.5f), y0 = (int)std::floor(kTopHeight - b.y + 0.5f);
    gfx::scissor(x0, y0, (int)std::floor(b.x + 0.5f) - x0, (int)std::floor(kTopHeight - a.y + 0.5f) - y0);
    // MapRenderer draws down from the top of GameNode's screen: that top
    // goes to the box's.
    Mat4 shift;
    shift.ty = size.height - Director::getInstance()->getVisibleSize().height;
    gfx::blendPremultiplied();
    map->render((double)config->worldX, (double)config->worldY, _pixels / _game->getScreenDensityScale(),
                transform * shift, size.width, size.height);
    gfx::scissorOff();
}

void MiniMap::refresh()
{
    _dots->clear();
    Simulation* simulation = _game->getSimulation();
    if (simulation == nullptr || _pixels <= 0.0f)
        return;
    const Size size = getContentSize();
    // The living, a pixel each: civilians, then the armed and the zombies
    // over them.
    static std::vector<Vec2> civs, armed, zombies;
    civs.clear();
    armed.clear();
    zombies.clear();
    for (const Person& p : simulation->getPeople()) {
        switch (p.state) {
        case PersonStateNone:
        case PersonStateDying:
        case PersonStateDead:
        case PersonStateZombieDying:
        case PersonStateZombieDead:
            continue;
        default:
            break;
        }
        const Vec2 at(std::floor(p.x * _pixels), std::floor(size.height - p.y * _pixels));
        if (p.state & PersonStateFlag::Zombie)
            zombies.push_back(at);
        else if (p.armed)
            armed.push_back(at);
        else
            civs.push_back(at);
    }
    auto dots = [&](const std::vector<Vec2>& points, const Color4F& color) {
        for (const Vec2& at : points)
            _dots->drawSolidRect(at, at + Vec2(1.0f, 1.0f), color);
    };
    dots(civs, kCivColor);
    dots(armed, kArmedColor);
    dots(zombies, kZombieColor);

    // The bottom screen's view, kept inside the box.
    const float pixels = _game->getMapScale() * _game->getScreenDensityScale();
    const Size screen = Director::getInstance()->getVisibleSize();
    const float left = _game->getMapOffsetX() * _pixels;
    const float top = size.height - _game->getMapOffsetY() * _pixels;
    const float right = left + screen.width / pixels * _pixels;
    const float bottom = top - screen.height / pixels * _pixels;
    const Vec2 from(std::max(left, 0.5f), std::max(bottom, 0.5f));
    const Vec2 to(std::min(right, size.width - 0.5f), std::min(top, size.height - 0.5f));
    if (from.x < to.x && from.y < to.y)
        _dots->drawRect(from, to, Color4F(1.0f, 0.85f, 0.1f, 1.0f));
}

// ---------------------------------------------------------- PopulationGraph

PopulationGraph* PopulationGraph::create()
{
    PopulationGraph* node = new PopulationGraph();
    if (!node->init()) {
        delete node;
        return nullptr;
    }
    node->autorelease();
    return node;
}

bool PopulationGraph::init()
{
    if (!Node::init())
        return false;
    _lines = DrawNode::create(1.0f);
    addChild(_lines);
    return true;
}

void PopulationGraph::setGraphSize(float width, float height)
{
    const Size size(std::floor(width), std::floor(height));
    if (size.equals(getContentSize()))
        return;
    setContentSize(size);
    redraw();
}

void PopulationGraph::addStats(const Simulation::Stats& stats)
{
    if (!_samples.empty() && stats.gameTime < _samples.back().time) {
        _samples.clear();
        _interval = 1000;
    }
    if (!_samples.empty() && stats.gameTime < _samples.back().time + _interval)
        return;
    _samples.push_back({stats.gameTime, stats.numCivs, stats.numZombies, stats.numDead});
    if (_samples.size() > kMaxSamples) {
        // Every other one, from the first; the newest stays.
        const Sample newest = _samples.back();
        size_t n = 0;
        for (size_t i = 0; i < _samples.size(); i += 2)
            _samples[n++] = _samples[i];
        _samples.resize(n);
        if (_samples.back().time != newest.time)
            _samples.push_back(newest);
        _interval *= 2;
    }
    redraw();
}

void PopulationGraph::redraw()
{
    _lines->clear();
    const Size size = getContentSize();
    if (size.width < 4.0f || size.height < 4.0f)
        return;
    // The axes.
    const Color4F axis(0.3f, 0.3f, 0.3f, 1.0f);
    _lines->drawLine(Vec2(0.0f, 0.5f), Vec2(size.width, 0.5f), axis);
    _lines->drawLine(Vec2(0.5f, 0.0f), Vec2(0.5f, size.height), axis);
    if (_samples.size() < 2)
        return;

    int most = 1;
    for (const Sample& s : _samples)
        most = std::max(most, std::max(s.civs, std::max(s.zombies, s.dead)));
    const int first = _samples.front().time;
    const float span = (float)std::max(1, _samples.back().time - first);
    const float w = size.width - 2.0f, h = size.height - 2.0f;
    auto point = [&](const Sample& s, int value) {
        return Vec2(1.5f + (float)(s.time - first) / span * w, 1.5f + (float)value / (float)most * h);
    };
    auto line = [&](int Sample::*field, const Color4F& color) {
        for (size_t i = 1; i < _samples.size(); i++)
            _lines->drawSegment(point(_samples[i - 1], _samples[i - 1].*field),
                                point(_samples[i], _samples[i].*field), 0.75f, color);
    };
    line(&Sample::dead, kDeadColor);
    line(&Sample::civs, kCivColor);
    line(&Sample::zombies, kZombieColor);
}
