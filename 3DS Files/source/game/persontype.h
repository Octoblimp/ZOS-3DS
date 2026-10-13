// Animation, AnimationPair, PersonType, PersonTypeFactory
// (decomp/classes/Animation.c, AnimationPair.c, PersonType.c,
// PersonTypeFactory.c). The seven types and their frame tables are
// generated from the game (persontypes_data.cpp).
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "gfx/gfx.h"

// One frame's rectangle on a character sheet, in pixels. The last two are
// the cell the frame sits in; the game does not read them.
struct AnimationIndexImage {
    int x, y, w, h, cellW, cellH;
};

struct AnimationFrame {
    int x, y;
    // Image space (gfx.h): u0..u1 left to right, v0..v1 top to bottom. The
    // game's are GL's, v flipped (1 - y/H); the port keeps the image's.
    float u0, u1, v0, v1;
};

class Animation {
public:
    // frames entries of table, which must end (x == -1) right after them;
    // every frame the first one's size.
    Animation(const std::string& name, int frames, int frameMs, bool loop, const AnimationIndexImage* table);

    void setTextureSize(int width, int height);
    int getDuration() const { return _frameCount * _frameMs; }

    int getFrameCount() const { return _frameCount; }
    int getFrameMs() const { return _frameMs; }
    bool isLoop() const { return _loop; }
    const std::vector<AnimationFrame>& getFrames() const { return _frames; }
    int getFrameWidth() const { return _frameWidth; }
    int getFrameHeight() const { return _frameHeight; }

private:
    int _frameCount;    // 0x00
    int _frameMs;       // 0x04
    bool _loop;         // 0x08
    std::vector<AnimationFrame> _frames;    // 0x0c
    int _frameWidth = 0;    // 0x18
    int _frameHeight = 0;   // 0x1c
};

// A pose twice over: as drawn, and "hidden", the same frames as a green
// outline (on the sheet beside them). SimulationRenderer picks which.
class AnimationPair {
public:
    AnimationPair(const std::string& name, int frames, int frameMs, bool loop, const AnimationIndexImage* visible,
                  const AnimationIndexImage* hidden);

    void setTextureSize(int width, int height);

    Animation visible;  // 0x00
    Animation hidden;   // 0x20
};

class PersonType {
public:
    enum Slot {
        Walk,
        Idle1,
        Idle2,
        Run,
        ZombieWalk,
        Die,
        Turn,
        ZombieRise,
        ZombieAttack,
        Shoot,
        ZombieDie1,
        ZombieDie2,
        SlotCount
    };

    explicit PersonType(const std::string& texturePath);
    ~PersonType();
    PersonType(const PersonType&) = delete;
    PersonType& operator=(const PersonType&) = delete;

    void setAnimation(Slot slot, AnimationPair* pair) { _animations[slot].reset(pair); }
    AnimationPair* getAnimation(Slot slot) const { return _animations[slot].get(); }

    // stepMs is the simulation's step (Simulation::getStepInterval). Checks
    // the speeds against it and works out the per-step values; throws if a
    // speed is above getMaxSpeed.
    void setup(int stepMs);
    // Pixels a second at which a person moves 3.2 px a step, under a 4 px
    // collision cell.
    static int getMaxSpeed(int stepMs) { return (int)(3200.0 / (double)stepMs); }
    void capSpeeds(int stepMs);
    void loadTexture();

    // dieAnimation 1 or 2; throws otherwise.
    int getDieAnimationDuration(int dieAnimation) const;
    int getZombieDieAnimationDuration(int dieAnimation) const;

    const gfx::Texture* getTexture() const { return _texture; }

    std::string _name;          // 0x00
    std::string _texturePath;   // 0x04
    gfx::Texture* _texture = nullptr;   // 0x08

    // Animation durations in ms, from setup().
    int _dieDuration = 0;           // 0x24
    int _turnDuration = 0;          // 0x2c
    int _zombieRiseDuration = 0;    // 0x34
    int _zombieAttackDuration = 0;  // 0x3c
    int _shootDuration = 0;         // 0x44
    int _zombieDie1Duration = 0;    // 0x50
    int _zombieDie2Duration = 0;    // 0x54

    // Pixels a second.
    float _walkSpeed = 0;           // 0x58
    float _runSpeed = 0;            // 0x5c
    float _zombieSpeedBase = 0;     // 0x60
    // _zombieSpeedBase, capped (capSpeeds) and set by the options.
    float _zombieSpeed = 0;         // 0x64
    // Chance a shot kills (GameOptions' accuracy / 100).
    float _accuracy = 0;            // 0x68
    // Police and soldiers carry guns; civilians only when armed.
    bool _armed = false;            // 0x6c
    // The helicopter takes them (not soldiers).
    bool _rescuable = true;         // 0x6d
    // ms between shots.
    int _shootInterval = 0;         // 0x70
    // ms between looking for a target, as a person and as a zombie, and in
    // steps (setup).
    int _searchInterval = 0;        // 0x74
    int _zombieSearchInterval = 0;  // 0x78
    int _searchSteps = 0;           // 0x7c
    int _zombieSearchSteps = 0;     // 0x80
    // ms for a half turn, and radians a step (setup) and its inverse.
    int _halfTurnMs = 0;            // 0x84
    float _turnPerStep = 0;         // 0x88
    float _stepsPerRadian = 0;      // 0x8c
    // SoundManager effects: the gunshot, and the zombie's attack.
    int _shootSound = 0;            // 0x90
    int _zombieAttackSound = 0;     // 0x94
    // In PersonTypeFactory's list (the renderer's per-type buffers).
    int _index = 0;                 // 0x98

private:
    std::unique_ptr<AnimationPair> _animations[SlotCount];
};

class PersonTypeFactory {
public:
    PersonTypeFactory() = default;
    ~PersonTypeFactory();
    PersonTypeFactory(const PersonTypeFactory&) = delete;
    PersonTypeFactory& operator=(const PersonTypeFactory&) = delete;

    // Makes the seven types and runs setup(stepMs) on each.
    void setupPersonTypes(int stepMs);
    // Loads one type's sheet a call, for the loading screen: done when all
    // are, percent of them so far.
    void loadStep(bool& done, int& percent);

    const std::vector<PersonType*>& getAllPersonTypes() const { return _allPersonTypes; }
    const std::vector<PersonType*>& getCivPersonTypes() const { return _civPersonTypes; }
    PersonType* getPolicePersonType() const { return _policePersonType; }
    PersonType* getSoldierPersonType() const { return _soldierPersonType; }
    // Throws "Unable to find person type with name ...".
    PersonType* getPersonTypeByName(const std::string& name) const;

private:
    // persontypes_data.cpp.
    void createPersonTypes();

    std::vector<PersonType*> _allPersonTypes;           // 0x00
    std::map<std::string, PersonType*> _personTypesByName;  // 0x0c
    std::vector<PersonType*> _civPersonTypes;           // 0x24
    PersonType* _policePersonType = nullptr;            // 0x30
    PersonType* _soldierPersonType = nullptr;           // 0x34
    int _loaded = 0;                                    // 0x38
};
