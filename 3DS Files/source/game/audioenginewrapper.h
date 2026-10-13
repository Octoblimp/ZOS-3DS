// AudioEngineWrapper (decomp/classes/AudioEngineWrapper.c): the four calls
// SoundManager makes into the audio engine. The game's went to a modified
// CocosDenshion::SimpleAudioEngine, and so do the port's
// (cocos/simpleaudioengine.*, over ndsp).
#pragma once

#include <string>

namespace AudioEngineWrapper {

// A sample id, or -1.
int preloadEffect(const std::string& path);
// A playing sound's id, or -1. priority is capped at 100: when the voices
// run out, a sound of lower priority gives way. pan (the port's, -1 left to
// 1 right) was 0 in the game's call.
int playEffect(int sampleId, float pitch, float volume, int priority, bool loop, float pan = 0.0f);
void stopEffect(int soundId);
void setEffectVolume(int soundId, float volume);
// The port's: a playing sound's pan.
void setEffectPan(int soundId, float pan);
// SimpleAudioEngine's, which GameNode::stopMap calls so a helicopter's loop
// does not outlive its map.
void stopAllEffects();

} // namespace AudioEngineWrapper
