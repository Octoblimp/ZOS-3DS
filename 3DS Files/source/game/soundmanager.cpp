#include "game/soundmanager.h"

#include <cstdio>
#include <stdexcept>

#include "game/audioenginewrapper.h"
#include "game/utils.h"

SoundManager::SoundManager()
{
    _effects.resize(EffectCount);

    auto add = [this](Effect e, const char* path) { _effects[e].samples.emplace_back(path); };
    auto addNumbered = [this](Effect e, const char* name, int count) {
        char path[64];
        for (int i = 1; i <= count; i++) {
            snprintf(path, sizeof(path), "audio/sound/%s_%02d.mp3", name, i);
            _effects[e].samples.emplace_back(path);
        }
    };

    for (int i = 0; i < 5; i++) {
        char path[64];
        snprintf(path, sizeof(path), "audio/sound/gunshot_%02d.mp3", i + 1);
        add((Effect)(Gunshot1 + i), path);
        _effects[Gunshot1 + i].volume = 0.5f;
        _effects[Gunshot1 + i].priority = 1;
    }
    add(Explosion1, "audio/sound/explosion_01.mp3");
    _effects[Explosion1].volume = 0.8f;
    _effects[Explosion1].priority = 2;
    add(Explosion2, "audio/sound/explosion_02.mp3");
    _effects[Explosion2].volume = 0.8f;
    _effects[Explosion2].priority = 2;
    add(Plane1, "audio/sound/plane_01.mp3");
    _effects[Plane1].volume = 1.0f;
    _effects[Plane1].pitchMin = 0.5f;
    _effects[Plane1].pitchMax = 1.5f;
    _effects[Plane1].priority = 3;
    add(Plane2, "audio/sound/plane_02.mp3");
    _effects[Plane2].volume = 1.0f;
    _effects[Plane2].priority = 3;
    add(Helicopter, "audio/sound/helicopter.wav");
    _effects[Helicopter].volume = 0.5f;
    _effects[Helicopter].priority = 3;
    _effects[Helicopter].loop = true;
    add(HelicopterCrash, "audio/sound/crash_01.mp3");
    _effects[HelicopterCrash].volume = 0.8f;
    _effects[HelicopterCrash].priority = 2;
    addNumbered(ScreamMan, "scream_man", 15);
    _effects[ScreamMan].volume = 0.8f;
    addNumbered(ScreamWoman, "scream_woman", 9);
    _effects[ScreamWoman].volume = 0.8f;
    addNumbered(ZombieMoan, "zombie_moan", 10);
    _effects[ZombieMoan].volume = 0.8f;

    for (const EffectInfo& e : _effects)
        _sampleCount += (int)e.samples.size();
}

void SoundManager::loadStep(bool& done, int& percent)
{
    if (_loaded >= _sampleCount)
        throw std::runtime_error("SoundManager::loadStep() has been called too many times");
    EffectInfo& e = _effects[_loadEffect];
    SampleInfo& s = e.samples[_loadSample];
    s.id = AudioEngineWrapper::preloadEffect(s.path);
    _loadSample++;
    if ((int)e.samples.size() <= _loadSample) {
        _loadEffect++;
        _loadSample = 0;
    }
    _loaded++;
    done = _sampleCount <= _loaded;
    percent = _loaded * 100 / _sampleCount;
}

int SoundManager::playEffect(Effect effect, float scale)
{
    return play(effect, scale, 0.0f);
}

int SoundManager::playEffect(Effect effect, float scale, float x)
{
    return play(effect, scale, panFor(x));
}

void SoundManager::setView(float left, float width)
{
    _viewHalfWidth = width * 0.5f;
    _viewCentre = left + _viewHalfWidth;
}

float SoundManager::panFor(float x) const
{
    if (!_positional || _viewHalfWidth <= 0.0f)
        return 0.0f;
    const float pan = (x - _viewCentre) / _viewHalfWidth;
    return pan < -1.0f ? -1.0f : (pan > 1.0f ? 1.0f : pan);
}

void SoundManager::setEffectPosition(int soundId, float x)
{
    AudioEngineWrapper::setEffectPan(soundId, panFor(x));
}

int SoundManager::play(Effect effect, float scale, float pan)
{
    if (scale < 0.35f)
        return -1;
    float fade = scale <= 0.75f ? (scale - 0.75f) / 0.4f + 1.0f : 1.0f;
    const EffectInfo& e = _effects[effect];
    int sample = 0;
    if (e.samples.size() >= 2)
        sample = (int)(Utils::getRandomUInt() % e.samples.size());
    float pitch = e.pitchMin;
    if (e.pitchMin != e.pitchMax)
        pitch = e.pitchMin + Utils::getRandomDouble() * (e.pitchMax - e.pitchMin);
    return AudioEngineWrapper::playEffect(e.samples[sample].id, pitch, fade * e.volume, e.priority, e.loop, pan);
}

void SoundManager::stopEffect(int soundId)
{
    AudioEngineWrapper::stopEffect(soundId);
}

void SoundManager::setEffectVolume(Effect effect, int soundId, float volume)
{
    AudioEngineWrapper::setEffectVolume(soundId, _effects[effect].volume * volume);
}
