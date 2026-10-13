#include "game/effects.h"

#include <cmath>
#include <stdexcept>

#include "game/soundmanager.h"
#include "game/utils.h"

namespace {

// DAT_005136e4: how long each bomb type's explosion lasts, in ms.
const int kExplosionDuration[2] = {4000, 3000};

const double kTwoPi = 6.283185307179586;
const double kPi = 3.141592653589793;

double degreesToRadians(float degrees)
{
    return ((double)degrees * kPi) / 180.0;
}

} // namespace

// ---- Bomb

void Effects::Bomb::getExplosionInfo(int t, bool& active, float& progress) const
{
    int start = time + 2000;
    if (start <= t && t <= kExplosionDuration[type] + start) {
        active = true;
        progress = (float)(t - start) / (float)kExplosionDuration[type];
        return;
    }
    active = false;
}

void Effects::Bomb::getBlastInfo(int t, bool& active, float& scale, float& alpha) const
{
    int start = time + 2000;
    int end = time + 2350;
    if (t < start || end < t) {
        active = false;
        return;
    }
    active = true;
    float growth = 1.0f;
    if (type != BombTypeSmall) {
        if (type != BombTypeLarge)
            throw std::runtime_error("Invalid bomb type in getBlastInfo()");
        growth = 2.0f;
    }
    float dt = (float)(t - start);
    scale = (dt * growth) / 350.0f + 1.0f;
    alpha = dt / -350.0f + 1.0f;
}

// ---- Mark

float Effects::Mark::getAlpha(int t) const
{
    if (t < time + 100)
        return (float)(t - time) / 100.0f;
    if (fadeStart < t)
        return (float)(t - fadeStart) / -1000.0f + 1.0f;
    return 1.0f;
}

// ---- SoldierDrop

void Effects::SoldierDrop::getParachuteInfo(int t, bool& active, float& outX, float& outY, float& progress) const
{
    if (!landed && time + 3000 <= t) {
        int dt = t - (time + 3000);
        active = true;
        progress = (float)dt / 2000.0f;
        int px = x;
        // Drifts in with the plane for its first 1.2 s.
        if (t < time + 4200)
            px = px + dt / 4 - 300;
        outX = (float)px;
        outY = (float)y;
        return;
    }
    active = false;
}

// ---- Helicopter

Effects::Helicopter::Helicopter(int x_, int y_, int time_) : x(x_), y(y_), time(time_)
{
    float r = Utils::getRandomDouble();
    angle = (r + r) * 3.1415927f;
    r = Utils::getRandomDouble();
    double a = (double)((r + r) * 3.1415927f);
    dirX = (float)cos(a);
    dirY = (float)sin(a);
}

Effects::Helicopter::Helicopter(FILE* f)
{
    x = Utils::readInt(f);
    y = Utils::readInt(f);
    time = Utils::readInt(f);
    angle = Utils::readFloat(f);
    landed = Utils::readBool(f);
    contaminated = Utils::readBool(f);
    takeOffTime = Utils::readInt(f);
    tookOff = Utils::readBool(f);
    infectedAboard = Utils::readBool(f);
    crashed = Utils::readBool(f);
    dirX = Utils::readFloat(f);
    dirY = Utils::readFloat(f);
    lastAttractTime = Utils::readInt(f);
    occupants = Utils::readInt(f);
    soundId = -1;
    lastVolumeTime = 0;
}

void Effects::Helicopter::getHelicopterInfo(int t, float& outX, float& outY, float& height, float& rotation,
                                            bool& rotorOn, State& state, int& stateTime) const
{
    state = StateNormal;
    stateTime = time;
    if (!landed) {
        // Flies in from the right for 7 s, then comes down for 1.
        int arrive = time + 7000;
        if (t < arrive) {
            outX = (float)(((arrive - t) * 594) / 1000 + x);
            outY = (float)y;
            height = 1.0f;
        } else {
            outX = (float)x;
            outY = (float)y;
            height = (float)(t - arrive) / -1000.0f + 1.0f;
        }
        float dt = (float)((t - 8000) - time);
        rotation = (float)fmod((double)angle + degreesToRadians((dt * 720.0f) / 1000.0f), kTwoPi);
        rotorOn = true;
        return;
    }
    if (takeOffTime == Never) {
        // On the ground: the rotor winds down over 2 s.
        outX = (float)x;
        outY = (float)y;
        height = 0.0f;
        bool settled = time + 10000 <= t;
        if (settled) {
            rotation = (float)fmod((double)angle + kTwoPi, kTwoPi);
        } else {
            float f = (float)((t - 8000) - time) / 2000.0f;
            f = f + f * f * -0.5f;
            rotation = (float)fmod(degreesToRadians((f + f) * 360.0f) + (double)angle, kTwoPi);
        }
        rotorOn = !settled && t < time + 8400;
        if (contaminated)
            state = StateContaminated;
        return;
    }
    if (!tookOff) {
        // Winding up for 2 s.
        float dt = (float)(t - takeOffTime);
        outX = (float)x;
        outY = (float)y;
        height = 0.0f;
        rotation = (float)fmod(
            (double)angle + degreesToRadians(((dt / 2000.0f) * (dt / 2000.0f) + 1.0f) * 360.0f), kTwoPi);
        rotorOn = takeOffTime + 1600 < t;
        return;
    }
    int climbed = takeOffTime + 3000;
    if (t < climbed) {
        // Rising for 1 s.
        outX = (float)x;
        outY = (float)y;
        height = (float)(t - (takeOffTime + 2000)) / 1000.0f;
    } else if (!infectedAboard || t < takeOffTime + 4000) {
        // Away to the left.
        outX = (float)(x - ((t - climbed) * 594) / 1000);
        outY = (float)y;
        height = 1.0f;
    } else if (t < takeOffTime + 5000) {
        // Going down.
        int fall = takeOffTime + 4000;
        state = StateCrashing;
        stateTime = fall;
        outX = (float)(x - ((t - climbed) * 594) / 1000);
        outY = (float)y;
        height = (float)(t - fall) / -1000.0f + 1.0f;
    } else {
        // The game leaves the position and height as they were.
        state = StateCrashed;
    }
    float dt = (float)(t - (takeOffTime + 2000));
    rotation = (float)fmod(degreesToRadians(((dt + dt) / 1000.0f + 2.0f) * 360.0f) + (double)angle, kTwoPi);
    rotorOn = true;
}

void Effects::Helicopter::getCrashExplosionInfo(int t, bool& active, float& progress, float& outX,
                                                float& outY) const
{
    if (infectedAboard) {
        int start = takeOffTime + 5000;
        if (start <= t && t <= takeOffTime + 9000) {
            active = true;
            progress = (float)(t - start) / 4000.0f;
            outX = (float)(x - 1188);
            outY = (float)y;
            return;
        }
    }
    active = false;
}

void Effects::Helicopter::startSound(SoundManager* sound)
{
    soundId = sound->playEffect(SoundManager::Helicopter, 1.0f, (float)x);
}

void Effects::Helicopter::stopSound(SoundManager* sound)
{
    if (soundId == -1)
        return;
    sound->stopEffect(soundId);
    soundId = -1;
}

void Effects::Helicopter::setSoundVolume(SoundManager* sound, float volume, int t)
{
    if (soundId == -1)
        return;
    if (t < lastVolumeTime + 100)
        return;
    sound->setEffectVolume(SoundManager::Helicopter, soundId, volume);
    lastVolumeTime = t;
}

void Effects::Helicopter::writeToSaveFile(FILE* f) const
{
    Utils::writeInt(f, x);
    Utils::writeInt(f, y);
    Utils::writeInt(f, time);
    Utils::writeFloat(f, angle);
    Utils::writeBool(f, landed);
    Utils::writeBool(f, contaminated);
    Utils::writeInt(f, takeOffTime);
    Utils::writeBool(f, tookOff);
    Utils::writeBool(f, infectedAboard);
    Utils::writeBool(f, crashed);
    Utils::writeFloat(f, dirX);
    Utils::writeFloat(f, dirY);
    Utils::writeInt(f, lastAttractTime);
    Utils::writeInt(f, occupants);
}

// ---- Effects

Effects::Effects(int width, int height, SoundManager* sound) : _width(width), _height(height), _sound(sound)
{
}

void Effects::addBomb(BombType type, int x, int y, int t)
{
    float r = Utils::getRandomDouble();
    double a = (double)((r + r) * 3.1415927f);
    _bombs.emplace_back(type, x, y, (float)cos(a), (float)sin(a), t);
    _sound->playEffect(SoundManager::Plane1, 1.0f);
}

void Effects::addSoldierDrop(int x, int y, int t)
{
    _soldierDrops.emplace_back(x, y, t);
    _sound->playEffect(SoundManager::Plane2, 1.0f);
}

void Effects::addHelicopter(int x, int y, int t)
{
    _helicopters.emplace_back(x, y, t);
    _helicopters.back().soundId = _sound->playEffect(SoundManager::Helicopter, 1.0f, (float)x);
}

void Effects::update(int t, IEffectObserver* observer)
{
    updateBombs(t, observer);
    updateSoldiers(t, observer);
    updateHelicopters(t, observer);
}

void Effects::updateBombs(int t, IEffectObserver* observer)
{
    size_t i = 0;
    while (i < _bombs.size()) {
        Bomb& b = _bombs[i];
        if (!b.soundPlayed && b.time + 1000 <= t) {
            SoundManager::Effect e;
            if (b.type == BombTypeSmall)
                e = SoundManager::Explosion1;
            else if (b.type == BombTypeLarge)
                e = SoundManager::Explosion2;
            else
                throw std::runtime_error("Invalid bomb type when playing explosion sound");
            _sound->playEffect(e, 1.0f, (float)b.x);
            b.soundPlayed = true;
        }
        int fall = b.time + 2000;
        if (!b.exploded && fall <= t) {
            observer->bombExplosionStarted(b.type, b.x, b.y);
            b.exploded = true;
            // A helicopter on the ground within 60 px goes with it.
            for (auto h = _helicopters.begin(); h != _helicopters.end();) {
                int dx = h->x - b.x;
                int dy = h->y - b.y;
                if (h->landed && !h->tookOff && ((unsigned)(dy * dy + dx * dx) >> 4) < 225) {
                    observer->helicopterDestroyed(&*h);
                    h = _helicopters.erase(h);
                } else {
                    ++h;
                }
            }
        }
        if (fall <= t && !b.markDone) {
            if (observer->explosionShouldLeaveMark(b.x, b.y))
                _marks.emplace_back(b.type, (float)b.x, (float)b.y, b.dirX, b.dirY, t);
            b.markDone = true;
        }
        if (kExplosionDuration[b.type] + fall <= t) {
            _bombs.erase(_bombs.begin() + i);
            continue;
        }
        i++;
    }

    // At most 20 marks: the oldest beyond that fade out, then go.
    while (_marks.size() > 20) {
        const Mark& m = _marks.front();
        if (m.fadeStart == Mark::NeverFades || t < m.fadeStart + 1000)
            break;
        _marks.pop_front();
    }
    if (_marks.size() > 20) {
        int extra = (int)_marks.size() - 20;
        for (int j = 0; j < extra; j++)
            if (_marks[j].fadeStart == Mark::NeverFades)
                _marks[j].fadeStart = t;
    }
}

void Effects::updateSoldiers(int t, IEffectObserver* observer)
{
    size_t i = 0;
    while (i < _soldierDrops.size()) {
        SoldierDrop& d = _soldierDrops[i];
        if (!d.landed) {
            if (t < d.time + 5000) {
                i++;
                continue;
            }
            observer->soldierLanded(d.x, d.y);
            d.landed = true;
        }
        // Gone once the plane is off the map.
        float planeX = (float)((((t - 3000) - d.time) * 1415) / 1000 + d.x - 300);
        if (planeX < (float)(_width + 466)) {
            i++;
            continue;
        }
        _soldierDrops.erase(_soldierDrops.begin() + i);
    }
}

void Effects::updateHelicopters(int t, IEffectObserver* observer)
{
    size_t i = 0;
    while (i < _helicopters.size()) {
        Helicopter& h = _helicopters[i];
        // The port's positional sound: the loop follows the helicopter.
        if (h.soundId != -1) {
            float hx, hy, height, rotation;
            bool rotorOn;
            Helicopter::State state;
            int stateTime;
            h.getHelicopterInfo(t, hx, hy, height, rotation, rotorOn, state, stateTime);
            _sound->setEffectPosition(h.soundId, hx);
        }
        if (!h.landed) {
            if (h.soundId == -1)
                h.soundId = _sound->playEffect(SoundManager::Helicopter, 1.0f, (float)h.x);
            if (h.time + 8000 <= t) {
                observer->helicopterLanded(h.x, h.y);
                h.landed = true;
            }
            i++;
            continue;
        }

        bool remove = false;
        if (h.takeOffTime == Helicopter::Never) {
            // Landed: the engine winds down over 800 ms.
            if (h.soundId != -1) {
                if (h.time + 8800 < t) {
                    _sound->stopEffect(h.soundId);
                    h.soundId = -1;
                } else if (h.lastVolumeTime + 100 <= t) {
                    float v = (float)((t - 8000) - h.time) / -800.0f + 1.0f;
                    _sound->setEffectVolume(SoundManager::Helicopter, h.soundId, v);
                    h.lastVolumeTime = t;
                }
            }
        } else {
            int takeOff = h.takeOffTime;
            if (!h.tookOff) {
                if (takeOff + 2000 < t) {
                    h.tookOff = true;
                    observer->helicopterTookOff(&h);
                    if (h.soundId != -1 && h.lastVolumeTime + 100 <= t) {
                        _sound->setEffectVolume(SoundManager::Helicopter, h.soundId, 1.0f);
                        h.lastVolumeTime = t;
                    }
                } else if (takeOff + 1200 < t) {
                    // The engine winds up over 800 ms.
                    if (h.soundId == -1)
                        h.soundId = _sound->playEffect(SoundManager::Helicopter, 1.0f, (float)h.x);
                    if (h.soundId != -1 && h.lastVolumeTime + 100 <= t) {
                        float v = (float)(t - (takeOff + 1200)) / 800.0f;
                        _sound->setEffectVolume(SoundManager::Helicopter, h.soundId, v);
                        h.lastVolumeTime = t;
                    }
                }
            }
            if (takeOff + 3000 < t) {
                if (!h.infectedAboard) {
                    remove = h.x - ((t - (takeOff + 3000)) * 594) / 1000 + 53 < 0;
                } else if (h.crashed) {
                    remove = takeOff + 9000 - t < 0;
                } else {
                    int crashTime = takeOff + 5000;
                    if (crashTime < t) {
                        h.crashed = true;
                        int cx = h.x - 1188;
                        int cy = h.y;
                        if (observer->explosionShouldLeaveMark(cx, cy))
                            _marks.emplace_back(BombTypeSmall, (float)cx, (float)cy, h.dirX, h.dirY, crashTime);
                        h.stopSound(_sound);
                        _sound->playEffect(SoundManager::HelicopterCrash, 1.0f, (float)cx);
                        observer->bombExplosionStarted(BombTypeSmall, cx, cy);
                        observer->helicopterCrashed(&h);
                    }
                }
            }
        }

        if (!h.tookOff && !h.contaminated) {
            observer->helicopterOnGroundSafe(&h);
            // A zombie got in before it left: it stays, and waits as if it
            // had just landed.
            if (h.contaminated && h.takeOffTime != Helicopter::Never) {
                h.time = (t * 2 - 10000) - h.takeOffTime;
                h.takeOffTime = Helicopter::Never;
                h.stopSound(_sound);
            }
        }
        if (remove) {
            h.stopSound(_sound);
            _helicopters.erase(_helicopters.begin() + i);
            continue;
        }
        i++;
    }
}

void Effects::getBombPlaneInfo(const Bomb& bomb, int t, bool& active, float& outX, float& outY) const
{
    int px = bomb.x + (((t - 2000) - bomb.time) * 2059) / 1000;
    outX = (float)px;
    if (-119 < px && outX < (float)(_width + 338)) {
        active = true;
        outY = (float)bomb.y;
        return;
    }
    active = false;
}

void Effects::getSoldierDropPlaneInfo(const SoldierDrop& drop, int t, bool& active, float& outX, float& outY) const
{
    int px = (((t - 3000) - drop.time) * 1415) / 1000 + drop.x - 300;
    outX = (float)px;
    if (-247 < px && outX < (float)(_width + 466)) {
        active = true;
        outY = (float)drop.y;
        return;
    }
    active = false;
}

Effects::Helicopter* Effects::getClosestWaitingHelicopter(int x, int y)
{
    Helicopter* best = nullptr;
    int bestDist = 0x7fffffff;
    for (Helicopter& h : _helicopters) {
        if (!h.isWaitingForPlayer())
            continue;
        int d = (h.y - y) * (h.y - y) + (h.x - x) * (h.x - x);
        if (d < bestDist) {
            best = &h;
            bestDist = d;
        }
    }
    return best;
}

Effects::Helicopter* Effects::getClosestOnGroundSafeHelicopter(int x, int y)
{
    Helicopter* best = nullptr;
    int bestDist = 0x7fffffff;
    for (Helicopter& h : _helicopters) {
        if (!h.isOnGroundSafe())
            continue;
        int d = (h.y - y) * (h.y - y) + (h.x - x) * (h.x - x);
        if (d < bestDist) {
            best = &h;
            bestDist = d;
        }
    }
    return best;
}

bool Effects::isTooCloseToExistingOrIncomingHelicopter(int x, int y) const
{
    for (const Helicopter& h : _helicopters)
        if (!h.tookOff && (unsigned)((h.y - y) * (h.y - y) + (h.x - x) * (h.x - x)) < 16900)
            return true;
    return false;
}

void Effects::evacuateHelicopter(const Helicopter* h, int t)
{
    for (Helicopter& mine : _helicopters) {
        if (&mine != h)
            continue;
        // Not settled yet: take off as late as if it had been asked at
        // time + 10000, mirrored.
        if (t < mine.time + 10000)
            mine.takeOffTime = (-10000 - mine.time) + t * 2;
        else
            mine.takeOffTime = t;
        return;
    }
}

void Effects::writeToSaveFile(FILE* f) const
{
    Utils::writeInt(f, (int)_bombs.size());
    for (const Bomb& b : _bombs) {
        Utils::writeInt(f, b.type);
        Utils::writeInt(f, b.x);
        Utils::writeInt(f, b.y);
        Utils::writeFloat(f, b.dirX);
        Utils::writeFloat(f, b.dirY);
        Utils::writeInt(f, b.time);
        Utils::writeBool(f, b.soundPlayed);
        Utils::writeBool(f, b.exploded);
        Utils::writeBool(f, b.markDone);
    }
    Utils::writeInt(f, (int)_marks.size());
    for (const Mark& m : _marks) {
        Utils::writeInt(f, m.type);
        Utils::writeInt(f, (int)m.x);
        Utils::writeInt(f, (int)m.y);
        Utils::writeFloat(f, m.dirX);
        Utils::writeFloat(f, m.dirY);
        Utils::writeInt(f, m.time);
        Utils::writeInt(f, m.fadeStart);
    }
    Utils::writeInt(f, (int)_soldierDrops.size());
    for (const SoldierDrop& d : _soldierDrops) {
        Utils::writeInt(f, d.x);
        Utils::writeInt(f, d.y);
        Utils::writeInt(f, d.time);
        Utils::writeBool(f, d.landed);
    }
    Utils::writeInt(f, (int)_helicopters.size());
    for (const Helicopter& h : _helicopters)
        h.writeToSaveFile(f);
}

void Effects::readFromSaveFile(FILE* f, IEffectObserver* observer)
{
    int count = Utils::readInt(f);
    for (int i = 0; i < count; i++) {
        BombType type = (BombType)Utils::readInt(f);
        int x = Utils::readInt(f);
        int y = Utils::readInt(f);
        float dirX = Utils::readFloat(f);
        float dirY = Utils::readFloat(f);
        int time = Utils::readInt(f);
        bool soundPlayed = Utils::readBool(f);
        bool exploded = Utils::readBool(f);
        bool markDone = Utils::readBool(f);
        _bombs.emplace_back(type, x, y, dirX, dirY, time);
        _bombs.back().soundPlayed = soundPlayed;
        _bombs.back().exploded = exploded;
        _bombs.back().markDone = markDone;
    }
    count = Utils::readInt(f);
    for (int i = 0; i < count; i++) {
        BombType type = (BombType)Utils::readInt(f);
        int x = Utils::readInt(f);
        int y = Utils::readInt(f);
        float dirX = Utils::readFloat(f);
        float dirY = Utils::readFloat(f);
        int time = Utils::readInt(f);
        int fadeStart = Utils::readInt(f);
        _marks.emplace_back(type, (float)x, (float)y, dirX, dirY, time);
        _marks.back().fadeStart = fadeStart;
    }
    count = Utils::readInt(f);
    for (int i = 0; i < count; i++) {
        int x = Utils::readInt(f);
        int y = Utils::readInt(f);
        int time = Utils::readInt(f);
        bool landed = Utils::readBool(f);
        _soldierDrops.emplace_back(x, y, time);
        _soldierDrops.back().landed = landed;
    }
    count = Utils::readInt(f);
    for (int i = 0; i < count; i++) {
        _helicopters.emplace_back(f);
        const Helicopter& h = _helicopters.back();
        if (h.landed && !h.tookOff)
            observer->helicopterLoadedOnGround(h.x, h.y);
    }
}
