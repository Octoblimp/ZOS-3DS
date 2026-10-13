#include "game/audioenginewrapper.h"

#include "cocos/simpleaudioengine.h"

using CocosDenshion::SimpleAudioEngine;

namespace AudioEngineWrapper {

int preloadEffect(const std::string& path)
{
    return SimpleAudioEngine::getInstance()->preloadEffect(path.c_str());
}

int playEffect(int sampleId, float pitch, float volume, int priority, bool loop, float pan)
{
    if (sampleId == -1)
        return -1;
    if (priority > 100)
        priority = 100;
    return SimpleAudioEngine::getInstance()->playEffectById(sampleId, loop, pitch, pan, volume, priority);
}

void stopEffect(int soundId)
{
    if (soundId == -1)
        return;
    SimpleAudioEngine::getInstance()->stopEffect(soundId);
}

void setEffectVolume(int soundId, float volume)
{
    if (soundId == -1)
        return;
    SimpleAudioEngine::getInstance()->setEffectVolume(soundId, volume);
}

void setEffectPan(int soundId, float pan)
{
    if (soundId == -1)
        return;
    SimpleAudioEngine::getInstance()->setEffectPan(soundId, pan);
}

void stopAllEffects()
{
    SimpleAudioEngine::getInstance()->stopAllEffects();
}

} // namespace AudioEngineWrapper
