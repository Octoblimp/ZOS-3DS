// Effects, Effects::Bomb, Mark, SoldierDrop, Helicopter
// (decomp/classes/Effects.c, Effects__*.c): the player's weapons over time.
// A bomb is a plane's pass, a blast and a scorch mark; a soldier drop is a
// plane and a parachute; a helicopter flies in, lands, waits, takes off and
// may crash. Effects keeps their clocks and tells its observer (Simulation)
// when something happens to the people.
//
// Times are the simulation's game time in ms; positions are map pixels.
#pragma once

#include <cstdio>
#include <deque>
#include <vector>

class SoundManager;

enum BombType {
    // Kills within 70 px, blast to 150.
    BombTypeSmall,
    // Kills within 110 px, blast to 220.
    BombTypeLarge,
};

class Effects {
public:
    // 0x1c bytes.
    struct Bomb {
        Bomb(BombType type_, int x_, int y_, float dirX_, float dirY_, int time_)
            : type(type_), x(x_), y(y_), dirX(dirX_), dirY(dirY_), time(time_)
        {
        }

        // The target marker shows until the bomb falls (time + 2000).
        bool getMarkerActive(int t) const { return t < time + 2000; }
        // From the fall to the explosion's end; progress 0..1.
        void getExplosionInfo(int t, bool& active, float& progress) const;
        // The 350 ms flash as it lands.
        void getBlastInfo(int t, bool& active, float& scale, float& alpha) const;

        BombType type;          // 0x00
        int x, y;               // 0x04
        // The plane's heading (the mark's too).
        float dirX, dirY;       // 0x0c
        // When it was ordered.
        int time;               // 0x14
        bool soundPlayed = false;   // 0x18
        bool exploded = false;      // 0x19
        bool markDone = false;      // 0x1a
    };

    // A scorch mark, 0x1c bytes. At most 20 stay; older ones fade.
    struct Mark {
        static const int NeverFades = 0x40000000;

        Mark(BombType type_, float x_, float y_, float dirX_, float dirY_, int time_)
            : type(type_), x(x_), y(y_), dirX(dirX_), dirY(dirY_), time(time_)
        {
        }

        // Fades in over 100 ms, and out over 1 s from fadeStart.
        float getAlpha(int t) const;

        BombType type;          // 0x00
        float x, y;             // 0x04
        float dirX, dirY;       // 0x0c
        int time;               // 0x14
        int fadeStart = NeverFades;     // 0x18
    };

    // 0x10 bytes. The plane passes from time + 3000, the parachute opens
    // then and lands at time + 5000.
    struct SoldierDrop {
        SoldierDrop(int x_, int y_, int time_) : x(x_), y(y_), time(time_) {}

        bool getMarkerActive() const { return !landed; }
        // The falling soldier: progress 0..1 and where, while in the air.
        void getParachuteInfo(int t, bool& active, float& outX, float& outY, float& progress) const;

        int x, y;               // 0x00
        int time;               // 0x08
        bool landed = false;    // 0x0c
    };

    // 0x34 bytes. Flies in over 7 s, lands at time + 8000, then waits for
    // the player's evacuate; takes off 2 s after it and is gone. Lands on a
    // zombie (contaminated) and it waits for nobody; takes off with an
    // infected person aboard and it crashes 5 s after.
    struct Helicopter {
        static const int Never = 0x40000000;

        enum State {
            StateNormal,
            // On the ground with a zombie inside.
            StateContaminated,
            // Falling, from takeOffTime + 4000 (stateTime).
            StateCrashing,
            StateCrashed,
        };

        Helicopter(int x_, int y_, int time_);
        explicit Helicopter(FILE* f);

        bool getMarkerActive() const { return !landed; }
        // Where to draw it: height 0 (ground) .. 1 (flying), rotation in
        // radians, whether the rotor turns. stateTime is when state began.
        void getHelicopterInfo(int t, float& outX, float& outY, float& height, float& rotation, bool& rotorOn,
                               State& state, int& stateTime) const;
        bool isWaitingForPlayer() const { return landed && !contaminated && takeOffTime == Never; }
        bool isOnGround() const { return landed && !tookOff; }
        bool isOnGroundSafe() const { return landed && !contaminated && !tookOff; }
        // The crash's explosion, from takeOffTime + 5000 for 4 s.
        void getCrashExplosionInfo(int t, bool& active, float& progress, float& outX, float& outY) const;

        void startSound(SoundManager* sound);
        void stopSound(SoundManager* sound);
        // At most every 100 ms.
        void setSoundVolume(SoundManager* sound, float volume, int t);
        void writeToSaveFile(FILE* f) const;

        int x, y;                       // 0x00
        int time;                       // 0x08
        float angle;                    // 0x0c
        bool landed = false;            // 0x10
        // A zombie got in.
        bool contaminated = false;      // 0x11
        int takeOffTime = Never;        // 0x14
        bool tookOff = false;           // 0x18
        bool infectedAboard = false;    // 0x19
        bool crashed = false;           // 0x1a
        float dirX, dirY;               // 0x1c
        // When it last called civilians over.
        int lastAttractTime = 0;        // 0x24
        // People inside, as last counted.
        int occupants = 0;              // 0x28
        int soundId = -1;               // 0x2c
        int lastVolumeTime = 0;         // 0x30
    };

    // Simulation, in this vtable order.
    class IEffectObserver {
    public:
        virtual void bombExplosionStarted(BombType type, int x, int y) = 0;
        virtual void soldierLanded(int x, int y) = 0;
        virtual void helicopterLanded(int x, int y) = 0;
        virtual void helicopterOnGroundSafe(Helicopter* h) = 0;
        virtual void helicopterTookOff(Helicopter* h) = 0;
        virtual void helicopterCrashed(Helicopter* h) = 0;
        virtual void helicopterDestroyed(Helicopter* h) = 0;
        // A saved game's helicopter that was on the ground.
        virtual void helicopterLoadedOnGround(int x, int y) = 0;
        // Not on water.
        virtual bool explosionShouldLeaveMark(int x, int y) = 0;

    protected:
        ~IEffectObserver() = default;
    };

    // The playing area, in map pixels.
    Effects(int width, int height, SoundManager* sound);

    void addBomb(BombType type, int x, int y, int t);
    void addSoldierDrop(int x, int y, int t);
    void addHelicopter(int x, int y, int t);
    void update(int t, IEffectObserver* observer);

    const std::vector<Bomb>& getActiveBombs() const { return _bombs; }
    const std::vector<SoldierDrop>& getActiveSoldierDrops() const { return _soldierDrops; }
    std::vector<Helicopter>& getActiveHelicopters() { return _helicopters; }
    const std::vector<Helicopter>& getActiveHelicopters() const { return _helicopters; }
    const std::deque<Mark>& getMarks() const { return _marks; }

    // The planes, flying left to right across the map.
    void getBombPlaneInfo(const Bomb& bomb, int t, bool& active, float& outX, float& outY) const;
    void getSoldierDropPlaneInfo(const SoldierDrop& drop, int t, bool& active, float& outX, float& outY) const;

    // nullptr if there is none.
    Helicopter* getClosestWaitingHelicopter(int x, int y);
    Helicopter* getClosestOnGroundSafeHelicopter(int x, int y);
    // Within 130 px of one that has not taken off.
    bool isTooCloseToExistingOrIncomingHelicopter(int x, int y) const;
    // Takes off now, or as soon as it has landed and settled (time + 10000).
    void evacuateHelicopter(const Helicopter* h, int t);

    void writeToSaveFile(FILE* f) const;
    void readFromSaveFile(FILE* f, IEffectObserver* observer);

private:
    void updateBombs(int t, IEffectObserver* observer);
    void updateSoldiers(int t, IEffectObserver* observer);
    void updateHelicopters(int t, IEffectObserver* observer);

    int _width;                             // 0x00
    int _height;                            // 0x04
    SoundManager* _sound;                   // 0x08
    std::vector<Bomb> _bombs;               // 0x0c
    std::deque<Mark> _marks;                // 0x18
    std::vector<SoldierDrop> _soldierDrops; // 0x40
    std::vector<Helicopter> _helicopters;   // 0x4c
};
