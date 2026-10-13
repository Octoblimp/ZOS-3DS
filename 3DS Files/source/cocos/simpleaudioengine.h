// CocosDenshion::SimpleAudioEngine, the calls the game makes, over ndsp.
//
// The game's engine was cocos2d-x 3.11's, modified (the .so's vtable has
// setEffectVolume(int, float) and playEffectById(int, bool, float, float,
// float, int)), over Android's Java side, read from classes.dex with
// Ghidra:
//
// - Cocos2dxSound: one SoundPool of 20 streams. Effects play at the engine's
//   effects volume, 0.5 from the start and never changed, times the gain.
//   playEffectById's priority group becomes SoundPool's priority: a group of
//   1 or more is 1,000,000,000 + group, and below 1 a counter that starts at
//   999,999,999 and counts down, so a newer low-priority sound ranks under an
//   older one. SoundPool's rule: with every stream busy, the lowest priority
//   stream stops (the oldest of equals) for the new one, unless the new one's
//   priority is lower still, when it does not play and play() returns 0.
//   The rate is the pitch, clamped to 0.5..2.
// - Cocos2dxMusic: one MediaPlayer, volume 0.5 until set; AppDelegate sets
//   1.0 and loops music01. willPlayBackgroundMusic was !isMusicActive() (no
//   other app playing music), always true here.
//
// The port: effects are PCM16 WAVs (tools/prepare_data.py) loaded whole into
// linear memory, one ndsp channel a stream (1..20). The music is streamed off
// the card on its own thread into channel 0. Without the DSP firmware
// (sdmc:/3ds/dspfirm.cdc) everything is silent and nothing fails.
#pragma once

namespace CocosDenshion {

class SimpleAudioEngine {
public:
    static SimpleAudioEngine* getInstance();
    // Stops everything and shuts ndsp down.
    static void end();

    void playBackgroundMusic(const char* filePath, bool loop = false);
    void stopBackgroundMusic(bool releaseData = false);
    void pauseBackgroundMusic();
    void resumeBackgroundMusic();
    void rewindBackgroundMusic();
    bool willPlayBackgroundMusic();
    bool isBackgroundMusicPlaying();
    float getBackgroundMusicVolume();
    void setBackgroundMusicVolume(float volume);

    float getEffectsVolume();
    void setEffectsVolume(float volume);
    // A playing stream's volume: the effects volume times volume (0..1).
    void setEffectVolume(int soundId, float volume);
    // A stream id (1 up), or 0 when it did not play (see above). pan is -1
    // (left) to 1 (right). Cocos2dxSound ignored it and the game passed 0;
    // the port's positional sound (SoundManager) uses it.
    int playEffectById(int sampleId, bool loop, float pitch, float pan, float gain, int priorityGroup);
    // A playing stream's pan (the port's).
    void setEffectPan(int soundId, float pan);
    void pauseAllEffects();
    void resumeAllEffects();
    void stopEffect(int soundId);
    void stopAllEffects();
    // A sample id, or -1 if the file is missing or bad. Loads once a path.
    int preloadEffect(const char* filePath);

    bool available() const { return _ready; }
    // The port's, for the log: effect streams playing, and about how many
    // samples of music the DSP has played.
    int effectsPlaying() const;
    unsigned musicSamplePosition() const;

private:
    SimpleAudioEngine();
    ~SimpleAudioEngine();
    void shutdown();

    bool _ready = false;
};

} // namespace CocosDenshion
