#include "game/simulation.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>

#include "game/persontype.h"
#include "game/soundmanager.h"
#include "game/utils.h"
#include "game/zombiewave.h"

namespace {

// Walking speed by health 0..10, as a fraction of the type's (0x513cfc,
// 0x513cd0): the wounded slow down, zombies do not.
const float kCivHealthSpeed[11] = {0.5f, 0.5f, 0.5f, 0.6f, 0.6f, 0.8f, 0.8f, 1.0f, 1.0f, 1.0f, 1.0f};
const float kZombieHealthSpeed[11] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};

// By GameOptions::ZombieSpeed (0x513d40).
const float kZombieSpeedMultipliers[5] = {0.4f, 0.75f, 1.0f, 1.5f, 2.0f};

int healthIndex(int health)
{
    return health < 0 ? 0 : health > 10 ? 10 : health;
}

bool isCorpse(int state)
{
    return state == PersonStateDead || state == PersonStateZombieDead;
}

// The game's way out of a bucket: the last one takes its place.
void removePerson(std::vector<Person*>& people, const Person* p)
{
    for (size_t i = 0; i < people.size(); i++) {
        if (people[i] == p) {
            people[i] = people.back();
            people.pop_back();
            return;
        }
    }
}

// The cells around (cx, cy) a ring at a time, from 1 cell out, as the game
// scans them: the ring's left column, its right, then its top and bottom rows
// between those. The first cell match accepts, or false once the ring is
// past every edge of the W x H layer.
template <typename Match>
bool findClosestCell(int cx, int cy, int w, int h, Match match, int& outX, int& outY)
{
    for (int r = 1;; r++) {
        const int left = cx - r, right = cx + r, top = cy - r, bottom = cy + r;
        if (left < 0 && top < 0 && right >= w && bottom >= h)
            return false;
        const int rowStart = std::max(top, 0), rowEnd = std::min(bottom, h - 1);
        const int colStart = std::max(left + 1, 0), colEnd = std::min(right, w);
        if (left >= 0) {
            for (int y = rowStart; y <= rowEnd; y++) {
                if (match(left, y)) {
                    outX = left;
                    outY = y;
                    return true;
                }
            }
        }
        if (right < w) {
            for (int y = rowStart; y <= rowEnd; y++) {
                if (match(right, y)) {
                    outX = right;
                    outY = y;
                    return true;
                }
            }
        }
        if (top >= 0) {
            for (int x = colStart; x < colEnd; x++) {
                if (match(x, top)) {
                    outX = x;
                    outY = top;
                    return true;
                }
            }
        }
        if (bottom < h) {
            for (int x = colStart; x < colEnd; x++) {
                if (match(x, bottom)) {
                    outX = x;
                    outY = bottom;
                    return true;
                }
            }
        }
    }
}

} // namespace

Simulation::Simulation(int width, int height, CollisionLayer* layer, CollisionOverlay* helicopterOverlay,
                       PersonTypeFactory* personTypes, SoundManager* sound, IObserver* observer)
    : _width(width), _height(height), _collisionLayer(*layer), _helicopterOverlay(helicopterOverlay),
      _personTypes(personTypes), _sound(sound), _observer(observer), _effects(width, height, sound)
{
    _collisionData = _collisionLayer.getData().data();
    _collisionWidth = _collisionLayer.getWidth();
    _collisionHeight = _collisionLayer.getHeight();
    setupSearchBuckets();
    _personMeasures.reserve(200);
}

Simulation::Simulation(int width, int height, CollisionLayer* layer, CollisionOverlay* helicopterOverlay,
                       PersonTypeFactory* personTypes, SoundManager* sound, IObserver* observer,
                       const GameOptions::Snapshot& options, int /*mapId*/)
    : Simulation(width, height, layer, helicopterOverlay, personTypes, sound, observer)
{
    _zombieSpeed = options.zombieSpeed;
    _civAccuracy = options.civAccuracy;
    _policeAccuracy = options.policeAccuracy;
    _soldierAccuracy = options.soldierAccuracy;
    const std::vector<PersonType*>& civTypes = _personTypes->getCivPersonTypes();
    PersonType* policeType = _personTypes->getPolicePersonType();
    const int numCivs = options.numCivs;
    const int numZombies = options.numZombies;
    _numPolice = options.numPolice;
    _numSoldiers = 0;
    _infectionTime = options.infectionTime * 1000;
    applyOptionsToPersonTypes();

    const cocos2d::Rect wholeMap(0.0f, 0.0f, (float)_width, (float)_height);
    createZombieWave(options);
    std::vector<cocos2d::Vec2> zombiePoints;
    getValidStartingPoints(_zombieWave->getRegion(), false, _zombieWave->getMinValidPoints(),
                           ((_height >> 2) * (_width >> 2)) / 20, zombiePoints);

    // A walkable point of region at random; for zombies, not in a building.
    auto randomPoint = [this](const cocos2d::Rect& region, bool zombie, float& x, float& y) {
        for (;;) {
            do {
                const float rx = Utils::getRandomDouble();
                const float ry = Utils::getRandomDouble();
                x = std::min(region.origin.x + rx * region.size.width, (float)(_width - 1));
                y = std::min(region.origin.y + ry * region.size.height, (float)(_height - 1));
            } while (!canWalk((int)x, (int)y));
            if (!zombie || !isBuilding((int)x, (int)y))
                return;
        }
    };

    const int total = numZombies + numCivs + _numPolice;
    _people.resize(total);
    const float armedCivs = (float)options.armedCivs;
    for (int i = 0; i < (int)_people.size(); i++) {
        Person& p = _people[i];
        bool police = false;
        bool zombie = false;
        float x, y;
        if (i < _numPolice) {
            // Police first, anywhere.
            p.type = policeType;
            _police.push_back(&p);
            police = true;
            randomPoint(wholeMap, false, x, y);
        } else {
            p.type = civTypes[Utils::getRandomUInt() % civTypes.size()];
            if (i >= numZombies + _numPolice) {
                randomPoint(wholeMap, false, x, y);
            } else if (zombiePoints.empty()) {
                zombie = true;
                randomPoint(_zombieWave->getRegion(), true, x, y);
            } else {
                zombie = true;
                const cocos2d::Vec2& pt = zombiePoints[Utils::getRandomUInt() % zombiePoints.size()];
                x = (pt.x + Utils::getRandomDouble()) * 4.0f;
                y = (pt.y + Utils::getRandomDouble()) * 4.0f;
                x = std::min(x, (float)(_width - 1));
                y = std::min(y, (float)(_height - 1));
            }
        }
        p.x = x;
        p.y = y;
        calculateNewTarget(p);
        setPersonDirection(p);
        setPersonCollisionValue(p);
        SearchBucket& bucket = getPersonSearchBucket(p);
        if (zombie) {
            // Out of the buckets until the wave lets it in.
            p.state = PersonStateNone;
            _zombieWave->addZombie(&p, &bucket);
        } else {
            const float r = Utils::getRandomDouble();
            if (r < 0.29411766f) {
                p.state = PersonStateIdle;
                p.timer = (int)(Utils::getRandomDouble() * 60000.0f);
            } else {
                p.state = PersonStateWalk;
                p.timer = (int)(Utils::getRandomDouble() * 160000.0f);
            }
            bucket.people.push_back(&p);
        }
        p.idleAnim = (Utils::getRandomUInt() & 1) + 1;
        p.health = 10;
        if (zombie)
            p.infectedTime = -_infectionTime;
        if (police)
            p.armed = true;
        else if (!zombie)
            p.armed = Utils::getRandomDouble() * 100.0f <= armedCivs;
        // Out of step with each other.
        p.animTime = -(int)(Utils::getRandomDouble() * 10000.0f);
    }

    _timer.reset();
    _zombieWave->checkNumZombies();
    _numHumans = total - numZombies;
    _numZombies = 0;
    _numDead = 0;
    _numZombiesKilled = 0;
    _numCivsKilled = 0;
    _numCivsRescued = 0;
    updateZombieWave();
}

Simulation::Simulation(int width, int height, CollisionLayer* layer, CollisionOverlay* helicopterOverlay,
                       PersonTypeFactory* personTypes, SoundManager* sound, IObserver* observer, FILE* f,
                       int version)
    : Simulation(width, height, layer, helicopterOverlay, personTypes, sound, observer)
{
    _zombieSpeed = (GameOptions::ZombieSpeed)Utils::readInt(f);
    _civAccuracy = Utils::readInt(f);
    _policeAccuracy = Utils::readInt(f);
    _soldierAccuracy = Utils::readInt(f);
    _infectionTime = Utils::readInt(f);
    PersonType* policeType = _personTypes->getPolicePersonType();
    PersonType* soldierType = _personTypes->getSoldierPersonType();
    applyOptionsToPersonTypes();

    _numHumans = 0;
    _numPolice = 0;
    _numSoldiers = 0;
    _numZombies = 0;
    _numDead = 0;
    if (version < 4) {
        _numZombiesKilled = 0;
        _numCivsKilled = 0;
    } else {
        _numZombiesKilled = Utils::readInt(f);
        _numCivsKilled = Utils::readInt(f);
    }
    _numCivsRescued = Utils::readInt(f);

    _people.resize(Utils::readInt(f));
    for (size_t i = 0; i < _people.size(); i++) {
        Person& p = _people[i];
        p.type = _personTypes->getPersonTypeByName(Utils::readString(f));
        if (p.type == policeType) {
            _police.push_back(&p);
            _numPolice++;
        }
        if (p.type == soldierType) {
            _soldiers.push_back(&p);
            _numSoldiers++;
        }
        p.x = Utils::readFloat(f);
        p.y = Utils::readFloat(f);
        p.targetX = Utils::readFloat(f);
        p.targetY = Utils::readFloat(f);
        setPersonDirection(p);
        setPersonCollisionValue(p);
        p.animTime = Utils::readInt(f);
        const int target = Utils::readInt(f);
        if (target != -1)
            p.target = &_people[target];
        p.state = PersonStateUtils::saveFilePersonStateToPersonState((SaveFilePersonState)Utils::readInt(f));
        if (p.state != PersonStateNone) {
            SearchBucket& bucket = getPersonSearchBucket(p);
            if (isCorpse(p.state)) {
                bucket.corpses.push_back(&p);
                _numDead++;
            } else {
                bucket.people.push_back(&p);
                if (p.state == PersonStateZombieDying || p.state == PersonStateDying)
                    _numDead++;
                else if (p.state != PersonStateTurning && !(p.state & PersonStateFlag::Zombie))
                    _numHumans++;
                else
                    _numZombies++;
            }
        }
        p.lastActionTime = Utils::readInt(f);
        p.armed = Utils::readBool(f);
        p.timer = Utils::readInt(f);
        p.idleAnim = Utils::readInt(f);
        if (version >= 4)
            p.dieAnim = Utils::readInt(f);
        p.infectedTime = Utils::readInt(f);
        const int attacker = Utils::readInt(f);
        if (attacker != -1)
            p.attacker = &_people[attacker];
        p.attackedTime = Utils::readInt(f);
        p.health = Utils::readInt(f);
        if (version >= 4) {
            p.velX = Utils::readFloat(f);
            p.velY = Utils::readFloat(f);
        }
    }

    _stepCount = Utils::readInt(f);
    _gameTime = _stepCount * StepInterval;
    _zombieWave.reset(new ZombieWave(f, _people));
    for (int i = 0; i < _zombieWave->getNumZombies(); i++)
        _zombieWave->setZombieSearchBucket(i, &getPersonSearchBucket(*_zombieWave->getZombie(i)));
    if (version >= 4)
        _effects.readFromSaveFile(f, this);
    _timer.reset();
}

Simulation::~Simulation() = default;

void Simulation::applyOptionsToPersonTypes()
{
    for (PersonType* t : _personTypes->getCivPersonTypes())
        t->_accuracy = (float)_civAccuracy / 100.0f;
    _personTypes->getPolicePersonType()->_accuracy = (float)_policeAccuracy / 100.0f;
    _personTypes->getSoldierPersonType()->_accuracy = (float)_soldierAccuracy / 100.0f;
    const float multiplier = getZombieWalkSpeedMultiplier(_zombieSpeed);
    for (PersonType* t : _personTypes->getAllPersonTypes()) {
        t->_zombieSpeed = multiplier * t->_zombieSpeedBase;
        t->capSpeeds(StepInterval);
    }
}

float Simulation::getZombieWalkSpeedMultiplier(GameOptions::ZombieSpeed speed)
{
    if ((unsigned)speed < 5)
        return kZombieSpeedMultipliers[speed];
    throw std::runtime_error("Invalid zombie speed");
}

void Simulation::createZombieWave(const GameOptions::Snapshot& options)
{
    const int numZombies = options.numZombies;
    // A zombie every 50 ms, or all at once when map wide.
    int endTime = numZombies * 50;
    const float right = (float)(_width - 1);
    const float bottom = (float)(_height - 1);
    cocos2d::Rect region;
    int minValidPoints;
    switch (options.zombieOutbreak) {
    case GameOptions::ZombieOutbreakNorth:
        region = cocos2d::Rect(0.0f, 0.0f, right, 0.0f);
        minValidPoints = (_width >> 2) / 4;
        break;
    case GameOptions::ZombieOutbreakEast:
        region = cocos2d::Rect(right, 0.0f, 0.0f, bottom);
        minValidPoints = (_height >> 2) / 4;
        break;
    case GameOptions::ZombieOutbreakSouth:
        region = cocos2d::Rect(0.0f, bottom, right, 0.0f);
        minValidPoints = (_width >> 2) / 4;
        break;
    case GameOptions::ZombieOutbreakWest:
        region = cocos2d::Rect(0.0f, 0.0f, 0.0f, bottom);
        minValidPoints = (_height >> 2) / 4;
        break;
    case GameOptions::ZombieOutbreakMapWide:
        region = cocos2d::Rect(0.0f, 0.0f, right, bottom);
        minValidPoints = ((_height >> 2) * (_width >> 2)) / 100;
        endTime = 0;
        break;
    default:
        throw std::runtime_error("Invalid zombie outbreak");
    }
    _zombieWave.reset(new ZombieWave(numZombies, 0, endTime, region, minValidPoints));
}

void Simulation::getValidStartingPoints(const cocos2d::Rect& region, bool allowBuildings, int minPoints,
                                        int maxPoints, std::vector<cocos2d::Vec2>& out)
{
    if (minPoints < 1)
        minPoints = 1;
    int x0 = (int)region.origin.x >> 2;
    int y0 = (int)region.origin.y >> 2;
    int x1 = (int)(region.origin.x + region.size.width) >> 2;
    int y1 = (int)(region.origin.y + region.size.height) >> 2;
    // The cells already looked at.
    int doneX0 = -1, doneY0 = -1, doneX1 = -1, doneY1 = -1;
    bool first = true;
    for (;;) {
        for (int y = y0; y <= y1; y++) {
            for (int x = x0; x <= x1; x++) {
                if (y >= doneY0 && y <= doneY1 && x >= doneX0 && x <= doneX1)
                    continue;
                const unsigned char v = _collisionData[y * _collisionWidth + x];
                if (!(v & 1) || (v == 0x1f && !allowBuildings))
                    continue;
                out.push_back(cocos2d::Vec2((float)x, (float)y));
                if (first && (int)out.size() > maxPoints) {
                    out.clear();
                    return;
                }
            }
        }
        if ((int)out.size() >= minPoints)
            return;
        if (x0 < 1 && y0 < 1 && x1 >= _collisionWidth - 1 && y1 >= _collisionHeight - 1)
            throw std::runtime_error("Unable to find sufficient valid starting points");
        doneX0 = x0;
        doneY0 = y0;
        doneX1 = x1;
        doneY1 = y1;
        if (x1 < _collisionWidth - 1)
            x1++;
        if (y1 < _collisionHeight - 1)
            y1++;
        if (y0 > 0)
            y0--;
        if (x0 > 0)
            x0--;
        first = false;
    }
}

void Simulation::calculateNewTarget(Person& p)
{
    float lo = p.x - 250.0f > 0.0f ? p.x - 250.0f : 0.0f;
    float hi = p.x + 250.0f < (float)_width - 1.0f ? p.x + 250.0f : (float)_width - 1.0f;
    p.targetX = lo + Utils::getRandomDouble() * (hi - lo);
    lo = p.y - 250.0f > 0.0f ? p.y - 250.0f : 0.0f;
    hi = p.y + 250.0f < (float)_height - 1.0f ? p.y + 250.0f : (float)_height - 1.0f;
    p.targetY = lo + Utils::getRandomDouble() * (hi - lo);
    p.animTime = _gameTime;
}

void Simulation::setPersonDirection(Person& p)
{
    const float dx = p.targetX - p.x;
    const float dy = p.targetY - p.y;
    const float d = sqrtf(dx * dx + dy * dy);
    // The game divides by zero here when the target is where it stands.
    if (d > 0.0f) {
        p.dirX = dx / d;
        p.dirY = dy / d;
    }
}

void Simulation::setPersonCollisionValue(Person& p)
{
    p.collisionValue = _collisionData[((int)p.y >> 2) * _collisionWidth + ((int)p.x >> 2)];
}

SearchBucket& Simulation::getPersonSearchBucket(const Person& p)
{
    return _searchBuckets[((int)p.y >> 7) * _searchBucketsWidth + ((int)p.x >> 7)];
}

void Simulation::updateZombieWave()
{
    _numZombies += _zombieWave->update(_gameTime);
}

void Simulation::setupSearchBuckets()
{
    _searchBucketsWidth = (int)ceilf((float)_width * 0.0078125f);
    _searchBucketsHeight = (int)ceilf((float)_height * 0.0078125f);
    _searchBuckets.resize(_searchBucketsWidth * _searchBucketsHeight);
}

void Simulation::update(const cocos2d::Rect& view, float scale)
{
    if (_paused)
        return;
    const double now = _timer.getTime();
    const double dt = now - _lastTime;
    if (dt > 0.0)
        _fps = (1.0 / dt) * (1.0 - 0.98) + _fps * 0.98;
    int steps = (int)((now * 1000.0) / StepInterval) - (int)((_lastTime * 1000.0) / StepInterval);
    // Behind by more than that, the game slows down instead.
    if (steps > 4)
        steps = 4;
    for (int i = 0; i < steps; i++) {
        _stepCount++;
        _gameTime = _stepCount * StepInterval;
        const double start = _timer.getTime();
        step(view, scale);
        _stepDuration = (_timer.getTime() - start) * (1.0 - 0.9) + _stepDuration * 0.9;
    }
    _lastTime = now;
}

void Simulation::unpause()
{
    _paused = false;
    resetTimer();
}

void Simulation::resetTimer()
{
    _timer.reset();
    _lastTime = 0.0;
}

void Simulation::startWalking(Person& p)
{
    p.state = PersonStateWalk;
    p.timer = (int)(Utils::getRandomDouble() * 80000.0f) + _gameTime + 80000;
    p.animTime = _gameTime;
}

void Simulation::searchForZombie(Person& p, int index)
{
    const int steps = p.type->_searchSteps;
    if (index % steps != _stepCount % steps)
        return;
    p.target = findClosestVisiblePerson(p, 220, true);
    if (p.target)
        p.state = PersonStateHunt;
}

void Simulation::scareTarget(Person& zombie, Person& target)
{
    if (target.armed)
        return;
    if (Utils::getRandomDouble() < 0.8f) {
        target.attacker = &zombie;
        target.attackedTime = _gameTime;
        target.state = PersonStateFlee;
    }
}

void Simulation::moveToCorpses(Person& p)
{
    SearchBucket& bucket = getPersonSearchBucket(p);
    removePerson(bucket.people, &p);
    bucket.corpses.push_back(&p);
}

void Simulation::stopKnockback(Person& p)
{
    p.velX = 0.0f;
    p.velY = 0.0f;
    if (p.state == PersonStateDying)
        p.animTime = _gameTime - p.type->getDieAnimationDuration(p.dieAnim);
    else if (p.state == PersonStateZombieDying)
        p.animTime = _gameTime - p.type->getZombieDieAnimationDuration(p.dieAnim);
}

bool Simulation::updateHuman(Person& p, int index)
{
    const PersonType* type = p.type;
    switch (p.state) {
    case PersonStateTurning:
        if (p.animTime + type->_turnDuration <= _gameTime) {
            p.state = PersonStateZombieWander;
            p.animTime = _gameTime;
            return true;
        }
        return false;
    case PersonStateDying:
        if (p.animTime + type->getDieAnimationDuration(p.dieAnim) <= _gameTime) {
            p.state = PersonStateDead;
            moveToCorpses(p);
        }
        return false;
    case PersonStateShoot:
        if (_gameTime < p.lastActionTime + type->_shootDuration)
            return false;
        p.state = PersonStateHunt;
        p.animTime = _gameTime;
        break;
    case PersonStateIdle:
        if (_gameTime >= p.timer) {
            startWalking(p);
            return true;
        }
        if (type->_armed && p.armed)
            searchForZombie(p, index);
        return false;
    case PersonStateWalk:
        if (_gameTime >= p.timer) {
            p.state = PersonStateIdle;
            p.animTime = _gameTime;
            p.timer = (int)(Utils::getRandomDouble() * 20000.0f) + _gameTime + 40000;
        } else if (p.armed) {
            searchForZombie(p, index);
        }
        return false;
    case PersonStateFlee:
        if (p.attackedTime + 10000 <= _gameTime) {
            startWalking(p);
            return true;
        }
        if (p.attacker) {
            // Directly away, to twice the distance, short of the edges.
            const float tx = (p.x + p.x) - p.attacker->x;
            const float ty = (p.y + p.y) - p.attacker->y;
            p.targetX = tx;
            p.targetY = ty;
            if (tx < 50.0f)
                p.targetX = 50.0f;
            else if (tx > (float)(_width - 50))
                p.targetX = (float)(_width - 50);
            if (ty < 50.0f)
                p.targetY = 50.0f;
            else if (ty > (float)(_height - 50))
                p.targetY = (float)(_height - 50);
        }
        return false;
    case PersonStateHunt:
        break;
    default:
        return false;
    }

    // Hunting: a fresh look every few steps, and whenever the zombie is gone.
    const int steps = type->_searchSteps;
    if (index % steps == _stepCount % steps || !p.target ||
        !(p.target->state & PersonStateFlag::LiveZombie)) {
        p.target = findClosestVisiblePerson(p, 220, true);
        if (!p.target) {
            startWalking(p);
            return true;
        }
    }
    return false;
}

bool Simulation::updateZombie(Person& p, int index, bool inView, float scale)
{
    const PersonType* type = p.type;
    switch (p.state) {
    case PersonStateZombieRising:
        if (p.animTime + type->_zombieRiseDuration <= _gameTime) {
            p.state = PersonStateZombieWander;
            p.animTime = _gameTime;
            return true;
        }
        return false;
    case PersonStateZombieDying:
        if (p.animTime + type->getZombieDieAnimationDuration(p.dieAnim) <= _gameTime) {
            p.state = PersonStateZombieDead;
            moveToCorpses(p);
        }
        return false;
    case PersonStateZombieAttack:
        if (p.lastActionTime + type->_zombieAttackDuration <= _gameTime) {
            p.state = PersonStateZombieChase;
            p.animTime = _gameTime;
        }
        return false;
    case PersonStateZombieChase: {
        const int steps = type->_zombieSearchSteps;
        if (index % steps == _stepCount % steps) {
            Person* old = p.target;
            p.target = findClosestVisiblePerson(p, 800, false);
            if (p.target) {
                scareTarget(p, *p.target);
                if (p.target != old && inView)
                    _sound->playEffect(SoundManager::ZombieMoan, scale, p.x);
                return false;
            }
        } else if (p.target && (p.target->state & PersonStateFlag::LiveHuman)) {
            return false;
        }
        p.target = nullptr;
        p.state = PersonStateZombieWander;
        return true;
    }
    case PersonStateZombieWander: {
        const int steps = type->_zombieSearchSteps;
        if (index % steps != _stepCount % steps)
            return false;
        p.target = findClosestVisiblePerson(p, 800, false);
        if (p.target) {
            p.state = PersonStateZombieChase;
            if (inView)
                _sound->playEffect(SoundManager::ZombieMoan, scale, p.x);
            scareTarget(p, *p.target);
        }
        return false;
    }
    default:
        return false;
    }
}

void Simulation::step(const cocos2d::Rect& view, float scale)
{
    // On screen, with a 10 px margin.
    const float viewLeft = (float)(int)(view.origin.x - 10.0f);
    const float viewTop = (float)(int)(view.origin.y - 10.0f);
    const float viewRight = (float)(int)(view.origin.x + view.size.width + 10.0f);
    const float viewBottom = (float)(int)(view.origin.y + view.size.height + 10.0f);
    // Close enough to see people turn and hear them.
    const bool zoomedIn = scale > 0.35f;
    // Where the sounds are heard from (the port's positional sound).
    _sound->setView(view.origin.x, view.size.width);

    for (int i = 0; i < (int)_people.size(); i++) {
        Person& p = _people[i];
        if (p.state == PersonStateNone || p.state == PersonStateZombieDead)
            continue;

        // A new place to walk to, at the end of the step.
        bool newTarget = false;

        // The bitten turn, and infected dead rise.
        if ((p.state & PersonStateFlag::Infectable) && _gameTime >= _infectionTime + p.infectedTime) {
            if (p.state == PersonStateDead) {
                p.state = PersonStateZombieRising;
                _numDead--;
                SearchBucket& bucket = getPersonSearchBucket(p);
                bucket.corpses.remove(&p);
                bucket.people.push_back(&p);
            } else {
                newTarget = true;
                p.state = PersonStateTurning;
                _numHumans--;
            }
            p.animTime = _gameTime;
            _numZombies++;
            if (p.health < 10)
                p.health = 10;
        }

        const bool inView = p.x >= viewLeft && p.x < viewRight && p.y >= viewTop && p.y < viewBottom;
        // The speed below goes by this, before the state changes.
        const int stateBefore = p.state;
        if (!(stateBefore & PersonStateFlag::Zombie))
            newTarget |= updateHuman(p, i);
        else
            newTarget |= updateZombie(p, i, inView, scale);

        float nx = p.x;
        float ny = p.y;
        bool turned = false;
        bool moved = false;
        if (p.state & PersonStateFlag::Moving) {
            Person* t = p.target;
            if (t) {
                p.targetX = t->x;
                p.targetY = t->y;
            }
            const float dx = p.targetX - p.x;
            const float dy = p.targetY - p.y;
            const float dist = sqrtf(dx * dx + dy * dy);
            // How far it has to turn, when it turns gradually.
            float angleDiff = 0.0f;
            if (dist != 0.0f) {
                if (zoomedIn && inView) {
                    // In sight, people turn at their type's rate.
                    float angle = (float)atan2((double)p.dirY, (double)p.dirX);
                    float diff = (float)atan2((double)dy, (double)dx) - angle;
                    angleDiff = fabsf(diff);
                    if ((double)diff > M_PI)
                        diff = (float)((double)diff - 2.0 * M_PI);
                    else if ((double)diff < -M_PI)
                        diff = (float)((double)diff + 2.0 * M_PI);
                    const float turn = p.type->_turnPerStep;
                    if (diff > turn) {
                        angle += turn;
                        turned = true;
                    } else if (diff < -turn) {
                        angle -= turn;
                        turned = true;
                    }
                    if (turned) {
                        p.dirX = (float)cos((double)angle);
                        p.dirY = (float)sin((double)angle);
                    } else {
                        p.dirX = dx / dist;
                        p.dirY = dy / dist;
                    }
                } else {
                    p.dirX = dx / dist;
                    p.dirY = dy / dist;
                }
            }

            int sign = 1;
            bool walk = true;
            if (p.state == PersonStateHunt) {
                // Keep between 40 and 300 px of the zombie.
                if (dist < 40.0f) {
                    sign = -1;
                } else if (dist < 300.0f) {
                    walk = false;
                    p.animTime = _gameTime;
                }
                // Shoot once facing it.
                if (!turned && t && p.lastActionTime + p.type->_shootInterval <= _gameTime) {
                    p.state = PersonStateShoot;
                    p.lastActionTime = _gameTime;
                    p.animTime = _gameTime;
                    if (inView)
                        _sound->playEffect((SoundManager::Effect)p.type->_shootSound, scale, p.x);
                    if (Utils::getRandomDouble() < p.type->_accuracy) {
                        t->state = PersonStateZombieDying;
                        t->animTime = _gameTime;
                        t->health = 0;
                        _numZombies--;
                        _numDead++;
                    } else {
                        t->health = std::max(t->health - 2, 1);
                    }
                }
            } else if (p.state == PersonStateZombieChase) {
                // A bite, at most every 2 s.
                if (t && dist <= 13.0f && p.lastActionTime + 2000 <= _gameTime) {
                    p.state = PersonStateZombieAttack;
                    p.lastActionTime = _gameTime;
                    p.animTime = _gameTime;
                    if (_gameTime < t->infectedTime) {
                        t->infectedTime = _gameTime;
                        if (inView)
                            _sound->playEffect((SoundManager::Effect)t->type->_zombieAttackSound, scale, t->x);
                    }
                    t->health -= 2;
                    if (t->health < 1) {
                        t->state = PersonStateDying;
                        t->animTime = _gameTime;
                        t->health = 0;
                        _numHumans--;
                        _numDead++;
                    } else {
                        t->attacker = &p;
                        t->attackedTime = _gameTime;
                        t->state = PersonStateFlee;
                        t->target = nullptr;
                    }
                }
            } else if (dist < 5.0f) {
                // There.
                newTarget = true;
            }

            if (walk) {
                const PersonType* type = p.type;
                float speed;
                if (!(stateBefore & PersonStateFlag::Zombie))
                    speed = (p.state == PersonStateFlee ? type->_runSpeed : type->_walkSpeed) *
                            kCivHealthSpeed[healthIndex(p.health)];
                else
                    speed = type->_zombieSpeed * kZombieHealthSpeed[healthIndex(p.health)];
                const float stepDist = speed * (float)StepInterval / 1000.0f;
                float move = stepDist;
                // Turning near the target, slow down so as not to circle it.
                if (turned && dist < (stepDist + stepDist) * type->_stepsPerRadian) {
                    const float halfPi = 1.5707964f;
                    move = angleDiff <= halfPi ? ((halfPi - angleDiff) / halfPi) * stepDist : 0.0f;
                }
                ny += (float)sign * move * p.dirY;
                nx += p.dirX * (float)sign * move;
                moved = true;
            }
        }

        // Pushed by those nearby (pushNearbyPeople, last step).
        if (p.pushCount > 0 && (p.state & PersonStateFlag::Pushable)) {
            moved = true;
            ny += p.pushY / (float)p.pushCount;
            nx += p.pushX / (float)p.pushCount;
        }

        // A blast's knockback, dying away.
        bool apply = moved;
        if (p.velX != 0.0f || p.velY != 0.0f) {
            const float vx = p.velX;
            const float vy = p.velY;
            p.velX = vx * 0.985f;
            p.velY = vy * 0.985f;
            ny += vy * (float)StepInterval / 1000.0f;
            nx += vx * (float)StepInterval / 1000.0f;
            if (p.velX * p.velX + p.velY * p.velY < 5625.0f) {
                p.velX = 0.0f;
                p.velY = 0.0f;
            }
            apply = true;
        }

        if (apply) {
            nx = nx >= 0.0f ? std::min(nx, (float)(_width - 1)) : 0.0f;
            ny = ny >= 0.0f ? std::min(ny, (float)(_height - 1)) : 0.0f;
            const int cellX = (int)nx >> 2;
            const int cellY = (int)ny >> 2;
            if (cellX == ((int)p.x >> 2) && cellY == ((int)p.y >> 2)) {
                p.x = nx;
                p.y = ny;
            } else {
                const unsigned char v = _collisionData[_collisionWidth * cellY + cellX];
                if (v & 1) {
                    const int fromX = (int)p.x >> 7, fromY = (int)p.y >> 7;
                    const int toX = (int)nx >> 7, toY = (int)ny >> 7;
                    p.collisionValue = v;
                    p.x = nx;
                    p.y = ny;
                    if (toX != fromX || toY != fromY) {
                        SearchBucket& from = _searchBuckets[_searchBucketsWidth * fromY + fromX];
                        SearchBucket& to = _searchBuckets[_searchBucketsWidth * toY + toX];
                        if (isCorpse(p.state)) {
                            from.corpses.remove(&p);
                            to.corpses.push_back(&p);
                        } else {
                            removePerson(from.people, &p);
                            to.people.push_back(&p);
                        }
                    }
                } else if (!(p.state & PersonStateFlag::Moving) || turned) {
                    stopKnockback(p);
                } else if (p.state != PersonStateFlee) {
                    // Walked into a wall: somewhere else.
                    newTarget = true;
                    stopKnockback(p);
                } else {
                    // Cornered: give up on running from it.
                    p.attacker = nullptr;
                    p.velX = 0.0f;
                    p.velY = 0.0f;
                    newTarget = true;
                }
            }
        }
        if (newTarget)
            calculateNewTarget(p);

        p.pushX = 0.0f;
        p.pushY = 0.0f;
        p.pushCount = 0;
        if (zoomedIn && inView)
            pushNearbyPeople(p);
    }

    _effects.update(_gameTime, this);
    updatePolice();
    updateSoldiers();
    updateZombieWave();
}

void Simulation::dropBomb(BombType type, int x, int y)
{
    _effects.addBomb(type, x, y, _gameTime);
}

void Simulation::dropSoldier(int x, int y)
{
    const int cx = x >> 2;
    const int cy = y >> 2;
    // Open ground (walkable, not under a roof).
    auto open = [this](int cellX, int cellY) { return (_collisionData[_collisionWidth * cellY + cellX] & 5) == 1; };
    if (!open(cx, cy)) {
        int cellX, cellY;
        if (!findClosestCell(cx, cy, _collisionWidth, _collisionHeight, open, cellX, cellY)) {
            Utils::log("Unable to find a valid point to drop the soldier, starting at %d, %d", x, y);
            return;
        }
        x = cellX << 2 | 2;
        y = cellY << 2 | 2;
    }
    _effects.addSoldierDrop(x, y, _gameTime);
}

bool Simulation::deployHelicopter(int x, int y)
{
    const int cx = x >> 2;
    const int cy = y >> 2;
    // Rings out to 50 cells, starting at the cell itself.
    for (int r = 0; r <= 50; r++) {
        const int left = cx - r, right = cx + r, top = cy - r, bottom = cy + r;
        const int rowStart = std::max(top, 0), rowEnd = std::min(bottom, _collisionHeight - 1);
        const int colStart = std::max(left + 1, 0), colEnd = std::min(right, _collisionWidth);
        int foundX = -1, foundY = -1;
        if (left >= 0) {
            for (int cy2 = rowStart; cy2 <= rowEnd && foundX < 0; cy2++)
                if (canDeployHelicopter(left, cy2))
                    foundX = left, foundY = cy2;
        }
        if (foundX < 0 && r > 0 && right < _collisionWidth) {
            for (int cy2 = rowStart; cy2 <= rowEnd && foundX < 0; cy2++)
                if (canDeployHelicopter(right, cy2))
                    foundX = right, foundY = cy2;
        }
        if (foundX < 0 && top >= 0) {
            for (int cx2 = colStart; cx2 < colEnd && foundX < 0; cx2++)
                if (canDeployHelicopter(cx2, top))
                    foundX = cx2, foundY = top;
        }
        if (foundX < 0 && bottom < _collisionHeight) {
            for (int cx2 = colStart; cx2 < colEnd && foundX < 0; cx2++)
                if (canDeployHelicopter(cx2, bottom))
                    foundX = cx2, foundY = bottom;
        }
        if (foundX >= 0) {
            _effects.addHelicopter(foundX << 2, foundY << 2, _gameTime);
            return true;
        }
    }
    _observer->displayMessage("Can't deploy helicopter here");
    return false;
}

bool Simulation::canDeployHelicopter(int cellX, int cellY)
{
    return _collisionLayer.canApplyOverlay(_helicopterOverlay, cellX, cellY, 3) &&
           !_effects.isTooCloseToExistingOrIncomingHelicopter(cellX << 2, cellY << 2);
}

void Simulation::evacuateHelicopter(const Effects::Helicopter* h)
{
    _effects.evacuateHelicopter(h, _gameTime);
}

int Simulation::getOutbreakLevel() const
{
    if (_numZombies < 50)
        return 1;
    if (_numZombies < 300)
        return 2;
    return 3;
}

void Simulation::getStats(Stats& stats) const
{
    stats.gameTime = _gameTime;
    stats.numCivs = getNumCivs();
    stats.numZombies = _numZombies;
    stats.numDead = _numDead;
    stats.outbreakLevel = getOutbreakLevel();
    stats.numZombiesKilled = _numZombiesKilled;
    stats.numCivsKilled = _numCivsKilled;
    stats.numCivsRescued = _numCivsRescued;
    stats.fps = _fps;
    stats.stepDuration = _stepDuration;
}

void Simulation::writeToSaveFile(FILE* f) const
{
    Utils::writeInt(f, _zombieSpeed);
    Utils::writeInt(f, _civAccuracy);
    Utils::writeInt(f, _policeAccuracy);
    Utils::writeInt(f, _soldierAccuracy);
    Utils::writeInt(f, _infectionTime);
    Utils::writeInt(f, _numZombiesKilled);
    Utils::writeInt(f, _numCivsKilled);
    Utils::writeInt(f, _numCivsRescued);
    Utils::writeInt(f, (int)_people.size());
    // People refer to each other by index.
    std::unordered_map<const Person*, int> indices(10);
    for (size_t i = 0; i < _people.size(); i++)
        indices[&_people[i]] = (int)i;
    for (const Person& p : _people) {
        Utils::writeString(f, p.type->_name);
        Utils::writeFloat(f, p.x);
        Utils::writeFloat(f, p.y);
        Utils::writeFloat(f, p.targetX);
        Utils::writeFloat(f, p.targetY);
        Utils::writeInt(f, p.animTime);
        Utils::writeInt(f, p.target ? indices[p.target] : -1);
        Utils::writeInt(f, PersonStateUtils::personStateToSaveFilePersonState(p.state));
        Utils::writeInt(f, p.lastActionTime);
        Utils::writeBool(f, p.armed);
        Utils::writeInt(f, p.timer);
        Utils::writeInt(f, p.idleAnim);
        Utils::writeInt(f, p.dieAnim);
        Utils::writeInt(f, p.infectedTime);
        Utils::writeInt(f, p.attacker ? indices[p.attacker] : -1);
        Utils::writeInt(f, p.attackedTime);
        Utils::writeInt(f, p.health);
        Utils::writeFloat(f, p.velX);
        Utils::writeFloat(f, p.velY);
    }
    Utils::writeInt(f, _stepCount);
    _zombieWave->writeToSaveFile(f, indices);
    _effects.writeToSaveFile(f);
}

void Simulation::playerKillCiv(Person& p)
{
    p.state = PersonStateDying;
    p.animTime = _gameTime;
    p.health = 0;
    _numHumans--;
    _numDead++;
    p.dieAnim = 2;
    // The bitten count as zombies, and will not rise.
    if (p.infectedTime != Person::NeverInfected) {
        _numZombiesKilled++;
        p.infectedTime = Person::NeverInfected;
    } else {
        _numCivsKilled++;
    }
}

Person* Simulation::findClosestVisiblePerson(Person& p, int radius, bool zombies)
{
    const int x0 = std::max((int)p.x - radius, 0);
    const int y0 = std::max((int)p.y - radius, 0);
    const int x1 = std::min((int)p.x + radius, _width - 1);
    const int y1 = std::min((int)p.y + radius, _height - 1);
    const int flag = zombies ? PersonStateFlag::LiveZombie : PersonStateFlag::LiveHuman;
    const unsigned radius2 = (unsigned)(radius * radius);
    _personMeasures.clear();
    for (int by = y0 >> 7; by <= y1 >> 7; by++) {
        for (int bx = x0 >> 7; bx <= x1 >> 7; bx++) {
            for (Person* q : _searchBuckets[_searchBucketsWidth * by + bx].people) {
                if (q == &p || !(q->state & flag))
                    continue;
                const int dx = (int)(q->x - p.x);
                const int dy = (int)(q->y - p.y);
                const unsigned d2 = (unsigned)(dy * dy + dx * dx);
                if (d2 <= radius2)
                    _personMeasures.push_back({q, d2});
            }
        }
    }
    std::sort(_personMeasures.begin(), _personMeasures.end());
    const int cellX = (int)p.x >> 2;
    const int cellY = (int)p.y >> 2;
    for (const PersonMeasure& m : _personMeasures)
        if (_collisionLayer.isVisibleFrom(cellX, cellY, (int)m.person->x, (int)m.person->y))
            return m.person;
    return nullptr;
}

void Simulation::pushNearbyPeople(Person& p)
{
    const int x0 = std::max((int)p.x - 12, 0);
    const int y0 = std::max((int)p.y - 12, 0);
    const int x1 = std::min((int)p.x + 12, _width - 1);
    const int y1 = std::min((int)p.y + 12, _height - 1);
    for (int by = y0 >> 7; by <= y1 >> 7; by++)
        for (int bx = x0 >> 7; bx <= x1 >> 7; bx++)
            pushPeople(_searchBuckets[_searchBucketsWidth * by + bx].people, &p);
}

void Simulation::pushPeople(std::vector<Person*>& people, Person* p)
{
    for (Person* q : people) {
        if (q == p || !(q->state & PersonStateFlag::Pushable))
            continue;
        const float dx = q->x - p->x;
        const float dy = q->y - p->y;
        const float d2 = dx * dx + dy * dy;
        if (!(d2 < 144.0f))
            continue;
        // Out to 12 px apart.
        if (d2 <= 0.0f) {
            q->pushX += 12.0f;
        } else {
            const float d = sqrtf(d2);
            const float f = (12.0f - d) / d;
            q->pushX += dx * f;
            q->pushY += dy * f;
        }
        q->pushCount++;
    }
}

void Simulation::updatePolice()
{
    for (auto it = _police.begin(); it != _police.end();) {
        if ((*it)->state & PersonStateFlag::LiveHuman)
            ++it;
        else
            it = _police.erase(it);
    }
    _numPolice = (int)_police.size();
}

void Simulation::updateSoldiers()
{
    for (auto it = _soldiers.begin(); it != _soldiers.end();) {
        if ((*it)->state & PersonStateFlag::LiveHuman)
            ++it;
        else
            it = _soldiers.erase(it);
    }
    _numSoldiers = (int)_soldiers.size();
}

void Simulation::bombExplosionStarted(BombType type, int x, int y)
{
    // Kills within, knocks back to, and the knockback at the edge (px/s).
    int killRadius, blastRadius, edgeSpeed;
    if (type == BombTypeSmall) {
        killRadius = 70;
        blastRadius = 150;
        edgeSpeed = 30;
    } else if (type == BombTypeLarge) {
        killRadius = 110;
        blastRadius = 220;
        edgeSpeed = 50;
    } else {
        throw std::runtime_error("Invalid bomb type in bombExplosionStarted()");
    }
    const int x0 = std::max(x - blastRadius, 0);
    const int y0 = std::max(y - blastRadius, 0);
    const int x1 = std::min(x + blastRadius, _width - 1);
    const int y1 = std::min(y + blastRadius, _height - 1);
    const float blast = (float)blastRadius;
    const float falloff = (float)(edgeSpeed - blastRadius);
    for (int by = y0 >> 7; by <= y1 >> 7; by++) {
        for (int bx = x0 >> 7; bx <= x1 >> 7; bx++) {
            for (Person* p : _searchBuckets[_searchBucketsWidth * by + bx].people) {
                const int dx = (int)p->x - x;
                const int dy = (int)p->y - y;
                const unsigned d2 = (unsigned)(dy * dy + dx * dx);
                if (d2 >= (unsigned)(blastRadius * blastRadius))
                    continue;
                const int state = p->state;
                if (d2 < (unsigned)(killRadius * killRadius)) {
                    if (state == PersonStateTurning || (state & PersonStateFlag::LiveZombie)) {
                        p->state = PersonStateZombieDying;
                        p->animTime = _gameTime;
                        p->health = 0;
                        _numZombies--;
                        _numDead++;
                        p->dieAnim = 2;
                        _numZombiesKilled++;
                    } else if (state & PersonStateFlag::LiveHuman) {
                        playerKillCiv(*p);
                    } else if (state == PersonStateDying) {
                        // Bitten to death: the bomb's now, and it stays dead.
                        p->dieAnim = 2;
                        p->animTime = _gameTime;
                        p->infectedTime = Person::NeverInfected;
                    }
                } else if (state & PersonStateFlag::LiveZombie) {
                    p->health = std::max(p->health - 2, 1);
                } else if (state & PersonStateFlag::LiveHuman) {
                    p->health -= 2;
                    if (p->health < 1)
                        playerKillCiv(*p);
                }
                // Thrown outwards, harder nearer the middle.
                const float d = (float)sqrt((double)(int)d2);
                if (d != 0.0f) {
                    const float speed = blast + (falloff * d) / blast;
                    p->velX += ((float)dx / d) * speed;
                    p->velY += ((float)dy / d) * speed;
                }
            }
        }
    }
}

void Simulation::soldierLanded(int x, int y)
{
    _people.resize(_people.size() + 1);
    Person& p = _people.back();
    p.type = _personTypes->getSoldierPersonType();
    p.x = (float)x;
    p.y = (float)y;
    calculateNewTarget(p);
    setPersonDirection(p);
    setPersonCollisionValue(p);
    p.state = PersonStateWalk;
    p.timer = (int)(Utils::getRandomDouble() * 80000.0f) + _gameTime + 80000;
    getPersonSearchBucket(p).people.push_back(&p);
    p.idleAnim = (Utils::getRandomUInt() & 1) + 1;
    p.health = 10;
    p.armed = true;
    p.animTime = _gameTime;
    _soldiers.push_back(&p);
    _numHumans++;
    _numSoldiers++;
}

void Simulation::helicopterLanded(int x, int y)
{
    _collisionLayer.applyOverlay(_helicopterOverlay, x >> 2, y >> 2);
    movePeopleAwayFromLandedHelicopter(x, y);
}

void Simulation::movePeopleAwayFromLandedHelicopter(int x, int y)
{
    const int halfW = (_helicopterOverlay->getWidth() << 2) >> 1;
    const int halfH = (_helicopterOverlay->getHeight() << 2) >> 1;
    const int x0 = std::max(x - halfW, 0);
    const int y0 = std::max(y - halfH, 0);
    const int x1 = std::min(x + halfW, _width - 1);
    const int y1 = std::min(y + halfH, _height - 1);
    _personMeasures.clear();
    for (int by = y0 >> 7; by <= y1 >> 7; by++) {
        for (int bx = x0 >> 7; bx <= x1 >> 7; bx++) {
            SearchBucket& bucket = _searchBuckets[_searchBucketsWidth * by + bx];
            for (Person* p : bucket.people)
                if (_helicopterOverlay->getValue((int)p->x - x, (int)p->y - y) != 0)
                    _personMeasures.push_back({p, 0});
            // The dead under it are left there, and will not rise.
            for (Person* c : bucket.corpses) {
                const int v = _helicopterOverlay->getValue((int)c->x - x, (int)c->y - y);
                if (v != 0) {
                    if (c->state == PersonStateDead)
                        c->state = PersonStateZombieDead;
                    c->collisionValue = v;
                }
            }
        }
    }
    for (const PersonMeasure& m : _personMeasures) {
        Person* p = m.person;
        int gx, gy;
        getClosestGroundPosition((int)p->x, (int)p->y, gx, gy);
        if ((gx >> 7) != ((int)p->x >> 7) || (gy >> 7) != ((int)p->y >> 7)) {
            removePerson(getPersonSearchBucket(*p).people, p);
            _searchBuckets[_searchBucketsWidth * (gy >> 7) + (gx >> 7)].people.push_back(p);
        }
        p->x = (float)gx;
        p->y = (float)gy;
    }
}

void Simulation::helicopterOnGroundSafe(Effects::Helicopter* h)
{
    helicopterAttractCivs(*h);
    helicopterUpdateOccupants(*h, false);
    if (h->contaminated)
        helicopterRedirectAttractedCivs(*h);
}

void Simulation::helicopterAttractCivs(Effects::Helicopter& h)
{
    if (_gameTime < h.lastAttractTime + 1500)
        return;
    h.lastAttractTime = _gameTime;
    const int x0 = std::max(h.x - 400, 0);
    const int y0 = std::max(h.y - 400, 0);
    const int x1 = std::min(h.x + 400, _width - 1);
    const int y1 = std::min(h.y + 400, _height - 1);
    for (int by = y0 >> 7; by <= y1 >> 7; by++) {
        for (int bx = x0 >> 7; bx <= x1 >> 7; bx++) {
            for (Person* p : _searchBuckets[_searchBucketsWidth * by + bx].people) {
                if (!(p->state & PersonStateFlag::LiveHuman) || !p->type->_rescuable)
                    continue;
                const int dx = (int)(p->x - (float)h.x);
                const int dy = (int)(p->y - (float)h.y);
                const unsigned d2 = (unsigned)(dy * dy + dx * dx);
                // Within 400 px, in sight, and this the closest.
                if (d2 >= 160001 || !_collisionLayer.isVisibleFrom(h.x >> 2, h.y >> 2, (int)p->x, (int)p->y) ||
                    _effects.getClosestOnGroundSafeHelicopter((int)p->x, (int)p->y) != &h)
                    continue;
                if (_helicopterOverlay->getValue((int)p->x - h.x, (int)p->y - h.y) == 0) {
                    // Run to the door, on whichever side.
                    p->targetX = (float)h.x;
                    int ty = h.y;
                    if (d2 >= 2501)
                        ty = p->y < (float)h.y ? h.y - 20 : h.y + 20;
                    p->targetY = (float)ty;
                    p->state = PersonStateFlee;
                    p->attacker = nullptr;
                    p->attackedTime = _gameTime;
                    p->target = nullptr;
                } else {
                    // Aboard: wait.
                    if (p->state != PersonStateIdle) {
                        p->state = PersonStateIdle;
                        p->animTime = _gameTime;
                    }
                    p->timer = _gameTime + 1600;
                }
            }
        }
    }
}

void Simulation::helicopterUpdateOccupants(Effects::Helicopter& h, bool takingOff)
{
    const int previous = h.occupants;
    _occupants.clear();
    // Past 10 aboard, the crowd is pushed back, the harder the more.
    const bool crowded = previous > 10 && !takingOff;
    float pushStrength = 0.0f;
    if (crowded)
        pushStrength = previous < 30 ? (float)(previous - 10) / 30.0f : 1.0f;
    h.occupants = 0;
    const int halfW = std::max((_helicopterOverlay->getWidth() << 2) >> 1, 80);
    const int halfH = std::max((_helicopterOverlay->getHeight() << 2) >> 1, 80);
    const int x0 = std::max(h.x - halfW, 0);
    const int y0 = std::max(h.y - halfH, 0);
    const int x1 = std::min(h.x + halfW, _width - 1);
    const int y1 = std::min(h.y + halfH, _height - 1);
    for (int by = y0 >> 7; by <= y1 >> 7; by++) {
        for (int bx = x0 >> 7; bx <= x1 >> 7; bx++) {
            SearchBucket& bucket = _searchBuckets[_searchBucketsWidth * by + bx];
            std::vector<Person*>& people = bucket.people;
            for (size_t k = 0; k < people.size();) {
                Person* p = people[k];
                bool inside = false;
                bool taken = false;
                if (_helicopterOverlay->getValue((int)p->x - h.x, (int)p->y - h.y) != 0) {
                    inside = true;
                    const int state = p->state;
                    if (!(state & PersonStateFlag::LiveHuman)) {
                        if (state & PersonStateFlag::LiveZombie)
                            h.contaminated = true;
                    } else {
                        h.occupants++;
                        if (takingOff) {
                            p->state = PersonStateNone;
                            _numHumans--;
                            if (p->infectedTime != Person::NeverInfected)
                                h.infectedAboard = true;
                            taken = true;
                        } else if (state == PersonStateFlee) {
                            // Arrived.
                            p->state = PersonStateWalk;
                            p->timer = Person::NeverInfected;
                        }
                    }
                }
                if (crowded) {
                    p->pushCount++;
                    if (inside) {
                        pushPeople(_occupants, p);
                        _occupants.push_back(p);
                        // Back out the door: towards the middle across it,
                        // and out the nearer side.
                        const float dx = (float)h.x - p->x;
                        float dy = 0.0f;
                        if (dx > -16.0f && dx < 16.0f)
                            dy = (float)(h.y + (p->y < (float)h.y ? -16 : 16)) - p->y;
                        p->pushX += pushStrength * dx * (float)p->pushCount;
                        p->pushY += pushStrength * dy * (float)p->pushCount;
                    } else {
                        const float dy = p->y - (float)h.y;
                        const float dx = p->x - (float)h.x;
                        const float d2 = dx * dx + dy * dy;
                        if (d2 < 6400.0f) {
                            pushPeople(_occupants, p);
                            _occupants.push_back(p);
                            const float d = sqrtf(d2);
                            if (d > 0.0f) {
                                const float f = d / -80.0f + 1.0f;
                                const float k2 = (float)p->pushCount * pushStrength * (f + f);
                                p->pushX += (dx / d) * k2;
                                p->pushY += (dy / d) * k2;
                            }
                        }
                    }
                }
                if (taken)
                    people.erase(people.begin() + k);
                else
                    k++;
            }
            if (takingOff) {
                for (Person* c : bucket.corpses)
                    if (_helicopterOverlay->getValue((int)c->x - h.x, (int)c->y - h.y) != 0)
                        c->collisionValue = 3;
            }
        }
    }
}

void Simulation::helicopterRedirectAttractedCivs(Effects::Helicopter& h)
{
    const int x0 = std::max(h.x - 400, 0);
    const int y0 = std::max(h.y - 400, 0);
    const int x1 = std::min(h.x + 400, _width - 1);
    const int y1 = std::min(h.y + 400, _height - 1);
    const float hx = (float)h.x;
    const float hy = (float)h.y;
    for (int by = y0 >> 7; by <= y1 >> 7; by++) {
        for (int bx = x0 >> 7; bx <= x1 >> 7; bx++) {
            for (Person* p : _searchBuckets[_searchBucketsWidth * by + bx].people) {
                if (!(p->state & PersonStateFlag::LiveHuman) || !p->type->_rescuable)
                    continue;
                const int dx = (int)(p->x - hx), dy = (int)(p->y - hy);
                if ((unsigned)(dy * dy + dx * dx) >= 160001)
                    continue;
                // Making for it (within 30 px).
                const int tx = (int)(p->targetX - hx), ty = (int)(p->targetY - hy);
                if (((unsigned)(ty * ty + tx * tx) >> 2) >= 225)
                    continue;
                calculateNewTarget(*p);
                if (p->state == PersonStateFlee) {
                    p->state = PersonStateWalk;
                    p->timer = (int)(Utils::getRandomDouble() * 80000.0f) + _gameTime + 80000;
                }
            }
        }
    }
}

void Simulation::helicopterTookOff(Effects::Helicopter* h)
{
    helicopterUpdateOccupants(*h, true);
    helicopterRedirectAttractedCivs(*h);
    _collisionLayer.removeOverlay(_helicopterOverlay, h->x >> 2, h->y >> 2, 3);
    if (h->infectedAboard)
        _observer->displayMessage("Someone on board is infected!");
    else
        _numCivsRescued += h->occupants;
}

void Simulation::helicopterCrashed(Effects::Helicopter* h)
{
    _numDead += h->occupants;
}

void Simulation::helicopterDestroyed(Effects::Helicopter* h)
{
    helicopterRedirectAttractedCivs(*h);
}

void Simulation::helicopterLoadedOnGround(int x, int y)
{
    _collisionLayer.applyOverlay(_helicopterOverlay, x >> 2, y >> 2);
    updateCollisionValueOfPeopleInsideLandedHelicopter(x, y);
}

void Simulation::updateCollisionValueOfPeopleInsideLandedHelicopter(int x, int y)
{
    const int halfW = (_helicopterOverlay->getWidth() << 2) >> 1;
    const int halfH = (_helicopterOverlay->getHeight() << 2) >> 1;
    const int x0 = std::max(x - halfW, 0);
    const int y0 = std::max(y - halfH, 0);
    const int x1 = std::min(x + halfW, _width - 1);
    const int y1 = std::min(y + halfH, _height - 1);
    for (int by = y0 >> 7; by <= y1 >> 7; by++) {
        for (int bx = x0 >> 7; bx <= x1 >> 7; bx++) {
            SearchBucket& bucket = _searchBuckets[_searchBucketsWidth * by + bx];
            for (Person* p : bucket.people) {
                const int v = _helicopterOverlay->getValue((int)p->x - x, (int)p->y - y);
                if (v != 0)
                    p->collisionValue = v;
            }
            for (Person* c : bucket.corpses) {
                const int v = _helicopterOverlay->getValue((int)c->x - x, (int)c->y - y);
                if (v != 0)
                    c->collisionValue = v;
            }
        }
    }
}

void Simulation::getClosestGroundPosition(int x, int y, int& outX, int& outY)
{
    auto ground = [this](int cellX, int cellY) { return _collisionData[_collisionWidth * cellY + cellX] == 3; };
    int cellX, cellY;
    if (!findClosestCell(x >> 2, y >> 2, _collisionWidth, _collisionHeight, ground, cellX, cellY)) {
        Utils::log("Couldn't find a close ground position");
        outX = x;
        outY = y;
        return;
    }
    outX = cellX << 2 | 2;
    outY = cellY << 2 | 2;
}

bool Simulation::explosionShouldLeaveMark(int x, int y)
{
    const int cellX = x < 0 ? 0 : std::min(x, _width - 1) >> 2;
    const int cellY = y < 0 ? 0 : std::min(y, _height - 1) >> 2;
    // Not on water. (The game strode rows by the layer's height here, which
    // is the wrong cell on all but square maps.)
    return _collisionData[_collisionWidth * cellY + cellX] != 0x82;
}
