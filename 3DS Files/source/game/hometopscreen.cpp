#include "game/hometopscreen.h"

#include <algorithm>

#include "game/gamenode.h"
#include "game/persontype.h"
#include "game/utils.h"
#include "gfx/gfx.h"

using namespace cocos2d;

namespace {

const float kWidth = 400.0f;
const float kHeight = 240.0f;
const int kCivilians = 14;
const int kZombies = 12;
// The people at twice the map's closest zoom, where they are 16 or so
// pixels across; the speeds, a civilian's 35 pixels a second and a zombie's
// 25..32, scale with them.
const float kScale = 2.0f;
// Background_General's left 35% is black (the game set a panel over it):
// the screen shows the rest.
const float kBackgroundLitFrom = 0.35f;

float random(float lo, float hi)
{
    return lo + (hi - lo) * Utils::getRandomDouble();
}

} // namespace

const char* const HomeTopScreen::kGeneralBackground = "Background_General.png";

HomeTopScreen* HomeTopScreen::create(GameNode* game, const std::string& background)
{
    HomeTopScreen* node = new HomeTopScreen();
    node->init(game, background);
    node->autorelease();
    return node;
}

bool HomeTopScreen::init(GameNode* game, const std::string& backgroundName)
{
    Node::init();
    _game = game;
    setContentSize(Size(kWidth, kHeight));
    Sprite* background = Sprite::create(backgroundName);
    const Size size = background->getContentSize();
    if (backgroundName == kGeneralBackground) {
        // Background_General is 512x256: its lit part as wide as the screen,
        // cut at the top and bottom.
        background->setScale(std::max(kWidth / (size.width * (1.0f - kBackgroundLitFrom)), kHeight / size.height));
        background->setAnchorPoint(Vec2(1.0f, 0.5f));
        background->setPosition(Vec2(kWidth, kHeight * 0.5f));
    } else {
        // Covering the screen, centred.
        background->setScale(std::max(kWidth / size.width, kHeight / size.height));
        background->setPosition(Vec2(kWidth * 0.5f, kHeight * 0.5f));
    }
    addChild(background, -1);
    scheduleUpdate();
    return true;
}

void HomeTopScreen::spawn(Walker& w, bool anywhere)
{
    const auto& civs = _game->getPersonTypeFactory().getCivPersonTypes();
    w.type = civs[(size_t)(Utils::getRandomDouble() * civs.size()) % civs.size()];
    const Animation& anim =
        w.type->getAnimation(w.zombie ? PersonType::ZombieWalk : PersonType::Run)->visible;
    w.ms = (int)random(0.0f, (float)anim.getDuration());
    w.y = random(12.0f * kScale, kHeight - 12.0f * kScale);
    w.speed = (w.zombie ? w.type->_zombieSpeedBase : w.type->_runSpeed) * kScale * random(0.85f, 1.15f);
    // The zombies come after the civilians, a screen behind at most.
    w.x = anywhere ? random(-kWidth, kWidth) : random(-kWidth * 0.5f, -12.0f * kScale) - (w.zombie ? kWidth * 0.3f : 0.0f);
}

void HomeTopScreen::update(float dt)
{
    if (_walkers.empty()) {
        // The people are GameNode's second load step.
        const auto& civs = _game->getPersonTypeFactory().getCivPersonTypes();
        if (civs.empty())
            return;
        for (const PersonType* type : civs)
            if (!type->getTexture() || !type->getTexture()->valid)
                return;
        _walkers.resize(kCivilians + kZombies);
        for (size_t i = 0; i < _walkers.size(); i++) {
            _walkers[i].zombie = i >= (size_t)kCivilians;
            spawn(_walkers[i], true);
        }
        std::sort(_walkers.begin(), _walkers.end(), [](const Walker& a, const Walker& b) { return a.y > b.y; });
        return;
    }
    const int ms = (int)(dt * 1000.0f);
    for (Walker& w : _walkers) {
        w.x += w.speed * dt;
        w.ms += ms;
        if (w.x > kWidth + 32.0f * kScale) {
            const float y = w.y;
            spawn(w, false);
            // The same lane, so the drawing order stays right.
            w.y = y;
        }
    }
}

void HomeTopScreen::draw(Renderer*, const Mat4& transform, u32)
{
    for (const Walker& w : _walkers) {
        const AnimationPair* pair = w.type->getAnimation(w.zombie ? PersonType::ZombieWalk : PersonType::Run);
        const Animation& anim = pair->visible;
        const int frame = (w.ms / anim.getFrameMs()) % anim.getFrameCount();
        const AnimationFrame& f = anim.getFrames()[frame];
        // The frame's x axis is the way it faces: rightwards here.
        const float hw = anim.getFrameWidth() * kScale * 0.5f;
        const float hh = anim.getFrameHeight() * kScale * 0.5f;
        const Vec2 tl = transform.apply(Vec2(w.x - hw, w.y + hh)), tr = transform.apply(Vec2(w.x + hw, w.y + hh));
        const Vec2 bl = transform.apply(Vec2(w.x - hw, w.y - hh)), br = transform.apply(Vec2(w.x + hw, w.y - hh));
        gfx::bindTexture(w.type->getTexture());
        gfx::Vertex* v = gfx::triangles(6);
        if (!v)
            return;
        v[0] = {tl.x, tl.y, f.u0, f.v0, gfx::kWhite};
        v[1] = {tr.x, tr.y, f.u1, f.v0, gfx::kWhite};
        v[2] = {bl.x, bl.y, f.u0, f.v1, gfx::kWhite};
        v[3] = {tr.x, tr.y, f.u1, f.v0, gfx::kWhite};
        v[4] = {br.x, br.y, f.u1, f.v1, gfx::kWhite};
        v[5] = {bl.x, bl.y, f.u0, f.v1, gfx::kWhite};
    }
}
