#include "game/simulationrenderer.h"

#include <algorithm>
#include <list>
#include <stdexcept>

#include "cocos/director.h"
#include "game/person.h"
#include "game/persontype.h"
#include "game/simulation.h"
#include "game/utils.h"

using cocos2d::Mat4;
using cocos2d::Vec2;

SimulationRenderer::SimulationRenderer(PersonTypeFactory* personTypes, float density)
    : _density(density), _personTypes(personTypes), _types(personTypes->getAllPersonTypes()), _effects(density)
{
    // The game's guesses at how many are on screen.
    allocatePeopleToRender(_people, 850);
    allocatePeopleToRender(_corpses, 200);
    _zombieMarkers.reserve(5000);
    _humanMarkers.reserve(5000);
}

SimulationRenderer::~SimulationRenderer()
{
    gfx::destroyTexture(&_shadow);
}

void SimulationRenderer::allocatePeopleToRender(PeopleToRender& people, int reserve)
{
    people.resize(_types.size());
    for (auto& v : people)
        v.reserve(reserve);
}

void SimulationRenderer::clearPeopleToRender(PeopleToRender& people)
{
    for (auto& v : people)
        v.clear();
}

void SimulationRenderer::onSurfaceCreated()
{
    const std::string path = cocos2d::FileUtils::getInstance()->fullPathForFilename("images/misc/person_shadow.png");
    if (!gfx::loadTexture(&_shadow, path.c_str()))
        throw std::runtime_error("Failed to load file: images/misc/person_shadow.png");
    _shadowWidth = _shadow.width;
    _shadowHeight = _shadow.height;
}

void SimulationRenderer::onSurfaceChanged(int width, int height)
{
    _screen = cocos2d::Size((float)width, (float)height);
    // (The game set the effects' screen in setSimulation only.)
    if (_simulation)
        _effects.setupMap(_width, _height, _screen);
}

void SimulationRenderer::setSimulation(Simulation* simulation)
{
    _simulation = simulation;
    if (simulation) {
        _width = simulation->getPlayingAreaWidth();
        _height = simulation->getPlayingAreaHeight();
        _effects.setupMap(_width, _height, _screen);
    }
}

void SimulationRenderer::prepareRender(float viewX, float viewY, float mapScale)
{
    if (!_simulation)
        return;
    _viewX = viewX;
    _viewY = viewY;
    _mapScale = mapScale;
    _scale = _density * mapScale;
    _gameTime = _simulation->getGameTime();
    prepareRenderPeopleMarkers(_zombieMarkers, PersonStateFlag::LiveZombie);
    prepareRenderPeopleMarkers(_humanMarkers, PersonStateFlag::LiveHuman);
    prepareRenderPoliceMarkers();
    prepareRenderSoldierMarkers();
    prepareRenderPeople();
    _effects.prepareRender(_simulation->getEffects(), _viewX, _viewY, _mapScale, _gameTime);
}

void SimulationRenderer::addMarker(std::vector<MarkerRenderInfo>& markers, float x, float y)
{
    const float sx = (x - _viewX) * _scale;
    const float sy = _scale * (y - _viewY);
    if (sx >= 0.0f && sy >= 0.0f && sx < _screen.width && sy < _screen.height)
        markers.push_back({sx, sy});
}

void SimulationRenderer::prepareRenderPeopleMarkers(std::vector<MarkerRenderInfo>& markers, int stateFlag)
{
    if (!(_mapScale < 0.4f))
        return;
    markers.clear();
    const int x0 = std::max((int)_viewX, 0);
    const int y0 = std::max((int)_viewY, 0);
    const int x1 = std::min((int)(_viewX + _screen.width / _scale), _width - 1);
    const int y1 = std::min((int)(_viewY + _screen.height / _scale), _height - 1);
    std::vector<SearchBucket>& buckets = _simulation->getSearchBuckets();
    const int bucketsWidth = _simulation->getSearchBucketsWidth();
    for (int by = y0 >> 7; by <= y1 >> 7; by++)
        for (int bx = x0 >> 7; bx <= x1 >> 7; bx++)
            for (const Person* p : buckets[bucketsWidth * by + bx].people)
                if (p->state & stateFlag)
                    addMarker(markers, p->x, p->y);
}

void SimulationRenderer::prepareRenderPoliceMarkers()
{
    if (!(_mapScale < 0.4f))
        return;
    _policeMarkers.clear();
    for (const Person* p : _simulation->getPolice())
        addMarker(_policeMarkers, p->x, p->y);
}

void SimulationRenderer::prepareRenderSoldierMarkers()
{
    if (!(_mapScale < 0.4f))
        return;
    _soldierMarkers.clear();
    for (const Person* p : _simulation->getSoldiers())
        addMarker(_soldierMarkers, p->x, p->y);
}

void SimulationRenderer::prepareRenderPeople()
{
    if (!(_mapScale > 0.3f))
        return;
    // 40 px around the screen, for the people half on it.
    const int x0 = std::max((int)_viewX - 40, 0);
    const int y0 = std::max((int)_viewY - 40, 0);
    const int x1 = std::min((int)(_viewX + _screen.width / _scale + 40.0f), _width - 1);
    const int y1 = std::min((int)(_viewY + _screen.height / _scale + 40.0f), _height - 1);
    clearPeopleToRender(_people);
    clearPeopleToRender(_corpses);
    std::vector<SearchBucket>& buckets = _simulation->getSearchBuckets();
    const int bucketsWidth = _simulation->getSearchBucketsWidth();
    auto add = [this](PeopleToRender& to, const Person* p) {
        const float sx = (p->x - _viewX) * _scale;
        const float sy = _scale * (p->y - _viewY);
        if (sx < -40.0f || sy < -40.0f || sx >= _screen.width + 40.0f || sy >= _screen.height + 40.0f)
            return;
        to[p->type->_index].push_back({p->x, p->y, p->dirX, p->dirY, (int)p->state, p->animTime,
                                       p->collisionValue, p->idleAnim, p->dieAnim, sx, sy});
    };
    for (int by = y0 >> 7; by <= y1 >> 7; by++) {
        for (int bx = x0 >> 7; bx <= x1 >> 7; bx++) {
            const SearchBucket& bucket = buckets[bucketsWidth * by + bx];
            for (const Person* p : bucket.people)
                add(_people, p);
            for (const Person* p : bucket.corpses)
                add(_corpses, p);
        }
    }
}

void SimulationRenderer::doRender(const Mat4& transform)
{
    _effects.doRenderUnderPeople(transform);
    doRenderPeople(transform);
    doRenderPeopleMarkers(transform, _zombieMarkers, 0xe30000, 3.0f);
    doRenderPeopleMarkers(transform, _humanMarkers, 0x00e300, 3.0f);
    doRenderPeopleMarkers(transform, _policeMarkers, 0x1212ff, 6.0f);
    doRenderPeopleMarkers(transform, _soldierMarkers, 0x1f3933, 6.0f);
    _effects.doRenderOverPeople(transform);
}

void SimulationRenderer::doRenderPeople(const Mat4& transform)
{
    if (!(_mapScale > 0.3f))
        return;
    // Fading in over 0.3..0.35.
    const float alpha = _mapScale <= 0.35f ? (_mapScale - 0.35f) / 0.049999982f + 1.0f : 1.0f;
    renderPeopleToRender(transform, _corpses, alpha);
    renderPeopleShadows(transform, alpha);
    renderPeopleToRender(transform, _people, alpha);
}

Vec2 SimulationRenderer::toNode(const Mat4& transform, float x, float y) const
{
    return transform.apply(Vec2(x, _screen.height - y));
}

void SimulationRenderer::doRenderPeopleMarkers(const Mat4& transform, const std::vector<MarkerRenderInfo>& markers,
                                               int colour, float size)
{
    if (!(_mapScale < 0.4f) || markers.empty())
        return;
    // Fading out over 0.35..0.4.
    const float alpha = _mapScale >= 0.35f ? (_mapScale - 0.4f) / -0.050000012f : 1.0f;
    const u32 c = gfx::rgba((u8)(((colour >> 16) & 0xff) * alpha), (u8)(((colour >> 8) & 0xff) * alpha),
                            (u8)((colour & 0xff) * alpha), (u8)(alpha * 255.0f));
    const float half = _density * size * 0.5f;
    gfx::bindTexture(nullptr);
    for (const MarkerRenderInfo& m : markers) {
        const Vec2 a = toNode(transform, m.x - half, m.y + half);
        const Vec2 b = toNode(transform, m.x + half, m.y - half);
        gfx::quad(a.x, a.y, b.x, b.y, 0.0f, 0.0f, 0.0f, 0.0f, c);
    }
}

void SimulationRenderer::renderPeopleToRender(const Mat4& transform, const PeopleToRender& people, float alpha)
{
    const u32 colour = gfx::alphaColor(alpha);
    for (size_t i = 0; i < people.size(); i++) {
        if (people[i].empty())
            continue;
        const PersonType* type = _types[i];
        if (!type->getTexture() || !type->getTexture()->valid)
            continue;
        gfx::bindTexture(type->getTexture());
        for (const PersonRenderInfo& p : people[i])
            populatePersonVerticesAndTexCoords(transform, p, type, colour);
    }
}

void SimulationRenderer::renderPeopleShadows(const Mat4& transform, float alpha)
{
    if (!_shadow.valid)
        return;
    gfx::bindTexture(&_shadow);
    const float halfW = (float)_shadowWidth * _scale * 0.5f;
    const float halfH = _scale * (float)_shadowHeight * 0.5f;
    for (size_t i = 0; i < _people.size(); i++) {
        const PersonType* type = _types[i];
        for (const PersonRenderInfo& p : _people[i]) {
            // None under a roof.
            if (p.collisionValue & 4)
                continue;
            // Fading as they die, and in as they rise.
            const int elapsed = _gameTime - p.animTime;
            float shadowAlpha = 1.0f;
            if (p.state == PersonStateZombieDying || p.state == PersonStateDying) {
                const int duration = p.state == PersonStateZombieDying
                                         ? type->getZombieDieAnimationDuration(p.dieAnim)
                                         : type->getDieAnimationDuration(p.dieAnim);
                shadowAlpha = std::max(1.0f - (float)elapsed / (float)duration, 0.0f);
            } else if (p.state == PersonStateZombieRising) {
                shadowAlpha = (float)elapsed / (float)type->_zombieRiseDuration;
                if (!(shadowAlpha < 1.0f))
                    shadowAlpha = 1.0f;
            }
            // Below and left of the feet.
            const float cx = p.screenX - _scale * 8.0f;
            const float cy = p.screenY - (_scale + _scale);
            const Vec2 a = toNode(transform, cx - halfW, cy - halfH);
            const Vec2 b = toNode(transform, cx + halfW, cy + halfH);
            gfx::quad(a.x, a.y, b.x, b.y, 0.0f, 0.0f, 1.0f, 1.0f, gfx::alphaColor(shadowAlpha * alpha));
        }
    }
}

const AnimationPair* SimulationRenderer::getAnimationPair(const PersonRenderInfo& p, const PersonType* type)
{
    switch (p.state) {
    case PersonStateIdle:
        return type->getAnimation(p.idleAnim == 1 ? PersonType::Idle1 : PersonType::Idle2);
    case PersonStateWalk:
    case PersonStateHunt:
        return type->getAnimation(PersonType::Walk);
    case PersonStateFlee:
        return type->getAnimation(PersonType::Run);
    case PersonStateZombieWander:
    case PersonStateZombieChase:
        return type->getAnimation(PersonType::ZombieWalk);
    case PersonStateZombieAttack:
        return type->getAnimation(PersonType::ZombieAttack);
    case PersonStateShoot:
        return type->getAnimation(PersonType::Shoot);
    case PersonStateTurning:
        return type->getAnimation(PersonType::Turn);
    case PersonStateZombieRising:
        return type->getAnimation(PersonType::ZombieRise);
    case PersonStateDying:
    case PersonStateDead:
        // The player's kills (2) go the way of a zombie's.
        if (p.dieAnim == 2)
            return type->getAnimation(PersonType::ZombieDie2);
        if (p.dieAnim == 1)
            return type->getAnimation(PersonType::Die);
        throw std::runtime_error("Invalid zombie die animation " + Utils::intToString(p.dieAnim));
    case PersonStateZombieDying:
    case PersonStateZombieDead:
        if (p.dieAnim == 2)
            return type->getAnimation(PersonType::ZombieDie2);
        if (p.dieAnim == 1)
            return type->getAnimation(PersonType::ZombieDie1);
        throw std::runtime_error("Invalid zombie die animation " + Utils::intToString(p.dieAnim));
    default:
        throw std::runtime_error("Invalid person state " + Utils::intToString(p.state));
    }
}

void SimulationRenderer::populatePersonVerticesAndTexCoords(const Mat4& transform, const PersonRenderInfo& p,
                                                            const PersonType* type, u32 colour)
{
    const AnimationPair* pair = getAnimationPair(p, type);
    // Under a roof, the outline.
    const Animation& anim = (p.collisionValue & 4) ? pair->hidden : pair->visible;
    const int count = anim.getFrameCount();
    int frame = (_gameTime - p.animTime) / anim.getFrameMs();
    if (!anim.isLoop()) {
        if (frame >= count)
            frame = count - 1;
    } else {
        frame %= count;
    }
    // (The game indexed before the first frame for a time before animTime.)
    if (frame < 0)
        frame = 0;
    const AnimationFrame& f = anim.getFrames()[frame];

    // The frame's x axis along the direction, about the person.
    const float hw = (float)anim.getFrameWidth() * _scale * 0.5f;
    const float hh = _scale * (float)anim.getFrameHeight() * 0.5f;
    const float dx = p.dirX, dy = p.dirY;
    auto corner = [&](float lx, float ly) { return toNode(transform, p.screenX + (lx * dx - ly * dy), p.screenY + (lx * dy + ly * dx)); };
    const Vec2 tl = corner(-hw, -hh), tr = corner(hw, -hh), bl = corner(-hw, hh), br = corner(hw, hh);
    gfx::Vertex* v = gfx::triangles(6);
    if (!v)
        return;
    v[0] = {tl.x, tl.y, f.u0, f.v0, colour};
    v[1] = {tr.x, tr.y, f.u1, f.v0, colour};
    v[2] = {bl.x, bl.y, f.u0, f.v1, colour};
    v[3] = {tr.x, tr.y, f.u1, f.v0, colour};
    v[4] = {br.x, br.y, f.u1, f.v1, colour};
    v[5] = {bl.x, bl.y, f.u0, f.v1, colour};
}
