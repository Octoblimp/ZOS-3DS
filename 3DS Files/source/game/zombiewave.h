// ZombieWave (decomp/classes/ZombieWave.c): the zombies a game starts with,
// let in over time. They are placed at the start and held out of the search
// buckets until released; numZombies of them come in evenly from startTime
// to endTime (all at once if endTime is not after startTime).
#pragma once

#include <cstdio>
#include <deque>
#include <unordered_map>
#include <vector>

#include "cocos/base.h"

struct Person;
struct SearchBucket;

class ZombieWave {
public:
    // region is where they come in (an edge of the map, or all of it), and
    // minValidPoints how many starting cells it must have.
    ZombieWave(int numZombies, int startTime, int endTime, const cocos2d::Rect& region, int minValidPoints);
    // A saved game's wave over its people (Simulation then sets the buckets).
    ZombieWave(FILE* f, std::deque<Person>& people);

    void addZombie(Person* p, SearchBucket* bucket);
    const cocos2d::Rect& getRegion() const { return _region; }
    int getMinValidPoints() const { return _minValidPoints; }
    // Throws "Expected zombie wave to contain N zombies, but it contains M
    // zombies".
    void checkNumZombies() const;
    // Lets in the zombies due by t; how many came in.
    int update(int t);
    void writeToSaveFile(FILE* f, std::unordered_map<const Person*, int>& indices) const;

    int getNumZombies() const { return _numZombies; }
    Person* getZombie(int i) const { return _zombies[i].person; }
    void setZombieSearchBucket(int i, SearchBucket* bucket) { _zombies[i].bucket = bucket; }

private:
    struct ZombieInfo {
        Person* person;
        SearchBucket* bucket;
    };

    int _numZombies;                    // 0x00
    int _startTime;                     // 0x04
    int _endTime;                       // 0x08
    cocos2d::Rect _region;              // 0x0c
    int _minValidPoints;                // 0x1c
    std::vector<ZombieInfo> _zombies;   // 0x20
    int _released = 0;                  // 0x2c
};
