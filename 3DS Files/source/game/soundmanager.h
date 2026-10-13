// SoundManager (decomp/classes/SoundManager.c): the game's sound effects,
// each a set of samples one of which plays at random, with a volume, a pitch
// range, a priority and whether it loops. The table is the constructor's.
//
// The port's positional sound, an option (SELECT in play, kept in Prefs):
// a sound with a place on the map leans to the speaker on its side of the
// view. The game played every sound centred.
#pragma once

#include <string>
#include <vector>

class SoundManager {
public:
    enum Effect {
        Gunshot1,
        Gunshot2,
        Gunshot3,
        Gunshot4,
        Gunshot5,
        Explosion1,
        Explosion2,
        // The bomber's pass, and the soldiers' plane.
        Plane1,
        Plane2,
        // Loops while it flies.
        Helicopter,
        HelicopterCrash,
        ScreamMan,
        ScreamWoman,
        ZombieMoan,
        EffectCount
    };

    SoundManager();

    // Loads one sample a call, for the loading screen: done when all are,
    // percent of them so far. Throws if called again after.
    void loadStep(bool& done, int& percent);

    // scale is the map's zoom: below 0.35 nothing plays, and up to 0.75 it
    // fades in. A playing sound's id, or -1.
    int playEffect(Effect effect, float scale);
    // The same, from map x: panned to its side of the view when positional.
    int playEffect(Effect effect, float scale, float x);
    void stopEffect(int soundId);
    // volume times the effect's own.
    void setEffectVolume(Effect effect, int soundId, float volume);

    // Positional sound: on or off, the view's left edge and width in map
    // pixels (Simulation::step's), and a playing sound moved to map x.
    void setPositional(bool on) { _positional = on; }
    bool isPositional() const { return _positional; }
    void setView(float left, float width);
    void setEffectPosition(int soundId, float x);

private:
    struct SampleInfo {
        explicit SampleInfo(const std::string& p) : path(p) {}
        std::string path;   // 0x00
        int id = -1;        // 0x04
    };

    // 0x20 bytes.
    struct EffectInfo {
        std::vector<SampleInfo> samples;    // 0x00
        float volume = 1.0f;                // 0x0c
        float pitchMin = 1.0f;              // 0x10
        float pitchMax = 1.0f;              // 0x14
        int priority = 0;                   // 0x18
        bool loop = false;                  // 0x1c
    };

    std::vector<EffectInfo> _effects;   // 0x00
    int _sampleCount = 0;               // 0x0c
    int _loadEffect = 0;                // 0x10
    int _loadSample = 0;                // 0x14
    int _loaded = 0;                    // 0x18

    // The port's.
    int play(Effect effect, float scale, float pan);
    // -1 (the view's left edge or beyond) to 1 (its right), 0 when off.
    float panFor(float x) const;
    bool _positional = false;
    float _viewCentre = 0.0f;
    float _viewHalfWidth = 0.0f;
};
