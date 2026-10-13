// Simulation (decomp/classes/Simulation.c): the outbreak itself. Everyone on
// the map, stepped every 58 ms: civilians wander and flee, police, soldiers
// and armed civilians hunt zombies, zombies chase and bite, the bitten turn
// and the dead rise. It is also the Effects observer, so the player's bombs,
// soldiers and helicopters act on the people here.
//
// People live in a deque (pointers to them stay valid) and are found through
// a grid of 128 x 128 px search buckets. Positions are map pixels; the
// collision layer's cells are 4 x 4 of them.
#pragma once

#include <cstdio>
#include <deque>
#include <list>
#include <memory>
#include <vector>

#include "cocos/base.h"
#include "game/collisionlayer.h"
#include "game/effects.h"
#include "game/gameoptions.h"
#include "game/highresolutiontimer.h"
#include "game/person.h"

class PersonTypeFactory;
class SoundManager;
class ZombieWave;

class Simulation : public Effects::IEffectObserver {
public:
    // GameNode.
    class IObserver {
    public:
        virtual void displayMessage(const char* message) = 0;

    protected:
        ~IObserver() = default;
    };

    // 0x30 bytes, for the HUD and the end of a game.
    struct Stats {
        int gameTime;           // 0x00
        int numCivs;            // 0x04
        int numZombies;         // 0x08
        int numDead;            // 0x0c
        int outbreakLevel;      // 0x10
        int numZombiesKilled;   // 0x14
        int numCivsKilled;      // 0x18
        int numCivsRescued;     // 0x1c
        double fps;             // 0x20
        double stepDuration;    // 0x28
    };

    static const int StepInterval = 58;

    // A new game. layer is copied (helicopters change the copy). mapId is
    // the map's, which the game passed and did not use.
    Simulation(int width, int height, CollisionLayer* layer, CollisionOverlay* helicopterOverlay,
               PersonTypeFactory* personTypes, SoundManager* sound, IObserver* observer,
               const GameOptions::Snapshot& options, int mapId);
    // A saved game, of save file version.
    Simulation(int width, int height, CollisionLayer* layer, CollisionOverlay* helicopterOverlay,
               PersonTypeFactory* personTypes, SoundManager* sound, IObserver* observer, FILE* f, int version);
    virtual ~Simulation();

    Simulation(const Simulation&) = delete;
    Simulation& operator=(const Simulation&) = delete;

    // Runs the steps due since the last call (at most 4). view is the part
    // of the map on screen, scale its zoom: people outside it, or all of
    // them when zoomed out past 0.35, skip the turning and sounds.
    void update(const cocos2d::Rect& view, float scale);
    void pause() { _paused = true; }
    void unpause();
    void resetTimer();
    bool getPaused() const { return _paused; }

    void dropBomb(BombType type, int x, int y);
    // On the closest open ground; not at all if there is none.
    void dropSoldier(int x, int y);
    // The closest place within 50 cells it fits; false, with a message, if
    // there is none.
    bool deployHelicopter(int x, int y);
    void evacuateHelicopter(const Effects::Helicopter* h);

    std::deque<Person>& getPeople() { return _people; }
    std::vector<SearchBucket>& getSearchBuckets() { return _searchBuckets; }
    int getSearchBucketsWidth() const { return _searchBucketsWidth; }
    int getSearchBucketsHeight() const { return _searchBucketsHeight; }
    std::list<Person*>& getPolice() { return _police; }
    std::list<Person*>& getSoldiers() { return _soldiers; }
    int getPlayingAreaWidth() const { return _width; }
    int getPlayingAreaHeight() const { return _height; }
    int getGameTime() const { return _gameTime; }
    double getFps() const { return _fps; }
    double getStepDuration() const { return _stepDuration; }
    static int getStepInterval() { return StepInterval; }
    // Civilians only (not police or soldiers).
    int getNumCivs() const { return _numHumans - _numPolice - _numSoldiers; }
    int getNumZombies() const { return _numZombies; }
    int getNumDead() const { return _numDead; }
    // 1 under 50 zombies, 2 under 300, else 3.
    int getOutbreakLevel() const;
    int getNumZombiesKilled() const { return _numZombiesKilled; }
    int getNumCivsKilled() const { return _numCivsKilled; }
    int getNumCivsRescued() const { return _numCivsRescued; }
    void getStats(Stats& stats) const;
    Effects& getEffects() { return _effects; }
    CollisionLayer& getCollisionLayer() { return _collisionLayer; }

    // Always the latest version (4).
    void writeToSaveFile(FILE* f) const;

    // Effects::IEffectObserver.
    void bombExplosionStarted(BombType type, int x, int y) override;
    void soldierLanded(int x, int y) override;
    void helicopterLanded(int x, int y) override;
    void helicopterOnGroundSafe(Effects::Helicopter* h) override;
    void helicopterTookOff(Effects::Helicopter* h) override;
    void helicopterCrashed(Effects::Helicopter* h) override;
    void helicopterDestroyed(Effects::Helicopter* h) override;
    void helicopterLoadedOnGround(int x, int y) override;
    bool explosionShouldLeaveMark(int x, int y) override;

private:
    // A person and a distance (squared), to sort by it.
    struct PersonMeasure {
        Person* person;
        unsigned measure;

        bool operator<(const PersonMeasure& other) const { return measure < other.measure; }
    };

    // The constructors' common part.
    Simulation(int width, int height, CollisionLayer* layer, CollisionOverlay* helicopterOverlay,
               PersonTypeFactory* personTypes, SoundManager* sound, IObserver* observer);
    // Accuracies and the zombies' speed onto the person types.
    void applyOptionsToPersonTypes();
    static float getZombieWalkSpeedMultiplier(GameOptions::ZombieSpeed speed);
    void createZombieWave(const GameOptions::Snapshot& options);
    // Cells (x, y) of region that people may start on: at least minPoints of
    // them, growing the region a cell at a time until there are. When the
    // region itself has more than maxPoints, none (pick from it at random).
    void getValidStartingPoints(const cocos2d::Rect& region, bool allowBuildings, int minPoints, int maxPoints,
                                std::vector<cocos2d::Vec2>& out);
    bool canWalk(int x, int y) const { return _collisionLayer.getValue(x, y) & 1; }
    bool isBuilding(int x, int y) const { return (_collisionLayer.getValue(x, y) & 0x18) == 0x18; }
    // Somewhere within 250 px to walk to.
    void calculateNewTarget(Person& p);
    static void setPersonDirection(Person& p);
    void setPersonCollisionValue(Person& p);
    SearchBucket& getPersonSearchBucket(const Person& p);
    void updateZombieWave();
    void setupSearchBuckets();

    void step(const cocos2d::Rect& view, float scale);
    // A step's state changes for a human or a zombie; true when it should
    // pick a new target.
    bool updateHuman(Person& p, int index);
    bool updateZombie(Person& p, int index, bool inView, float scale);
    // An armed person's look for a zombie, every few steps.
    void searchForZombie(Person& p, int index);
    // Back to walking, for 80..160 s.
    void startWalking(Person& p);
    // An unarmed target of a zombie runs from it, mostly.
    void scareTarget(Person& zombie, Person& target);
    // Off the bucket's living and onto its dead.
    void moveToCorpses(Person& p);
    // Knocked into a wall: the knockback stops, and a death animation ends.
    void stopKnockback(Person& p);

    void playerKillCiv(Person& p);
    // The closest within radius (zombies, or humans) that p can see; nullptr
    // if none.
    Person* findClosestVisiblePerson(Person& p, int radius, bool zombies);
    // Pushes the pushable within 12 px of p away from it.
    void pushNearbyPeople(Person& p);
    void pushPeople(std::vector<Person*>& people, Person* p);
    // Drop those no longer alive.
    void updatePolice();
    void updateSoldiers();

    // The helicopter fits on open ground centred on the cell, clear of the
    // others.
    bool canDeployHelicopter(int cellX, int cellY);
    void movePeopleAwayFromLandedHelicopter(int x, int y);
    // Calls civilians within 400 px over, every 1.5 s.
    void helicopterAttractCivs(Effects::Helicopter& h);
    // Counts who is aboard and keeps a crowd from piling in; takingOff takes
    // them away.
    void helicopterUpdateOccupants(Effects::Helicopter& h, bool takingOff);
    // Those it called go back to wandering.
    void helicopterRedirectAttractedCivs(Effects::Helicopter& h);
    void updateCollisionValueOfPeopleInsideLandedHelicopter(int x, int y);
    // The centre of the closest cell of value 3 (open ground) to (x, y).
    void getClosestGroundPosition(int x, int y, int& outX, int& outY);

    int _width;                             // 0x04
    int _height;                            // 0x08
    std::deque<Person> _people;             // 0x0c
    HighResolutionTimer _timer;             // 0x34
    bool _paused = false;                   // 0x3c
    int _stepCount = 0;                     // 0x40
    // _stepCount * 58.
    int _gameTime = 0;                      // 0x44
    // The timer's at the last update, in s.
    double _lastTime = 0;                   // 0x48
    double _fps = 0;                        // 0x50
    double _stepDuration = 0;               // 0x58
    CollisionLayer _collisionLayer;         // 0x60
    unsigned char* _collisionData;          // 0x78
    int _collisionWidth;                    // 0x7c
    int _collisionHeight;                   // 0x80
    CollisionOverlay* _helicopterOverlay;   // 0x84
    PersonTypeFactory* _personTypes;        // 0x88
    int _searchBucketsWidth = 0;            // 0x8c
    int _searchBucketsHeight = 0;           // 0x90
    std::vector<SearchBucket> _searchBuckets;   // 0x94
    std::list<Person*> _police;             // 0xa0
    std::list<Person*> _soldiers;           // 0xa8
    // The living: civilians, police and soldiers.
    int _numHumans = 0;                     // 0xb0
    int _numPolice = 0;                     // 0xb4
    int _numSoldiers = 0;                   // 0xb8
    int _numZombies = 0;                    // 0xbc
    int _numDead = 0;                       // 0xc0
    // By the player: bombs, and infected civilians.
    int _numZombiesKilled = 0;              // 0xc4
    int _numCivsKilled = 0;                 // 0xc8
    int _numCivsRescued = 0;                // 0xcc
    // ms from a bite to turning.
    int _infectionTime = 0;                 // 0xd0
    GameOptions::ZombieSpeed _zombieSpeed = GameOptions::ZombieSpeedNormal;    // 0xd4
    int _civAccuracy = 0;                   // 0xd8
    int _policeAccuracy = 0;                // 0xdc
    int _soldierAccuracy = 0;               // 0xe0
    std::unique_ptr<ZombieWave> _zombieWave;    // 0xe4
    SoundManager* _sound;                   // 0xe8
    IObserver* _observer;                   // 0xec
    // Scratch.
    std::vector<PersonMeasure> _personMeasures; // 0xf0
    std::vector<Person*> _occupants;        // 0xfc
    Effects _effects;                       // 0x108
};
