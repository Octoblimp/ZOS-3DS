#include "game/zombiewave.h"

#include <stdexcept>
#include <string>

#include "game/person.h"
#include "game/utils.h"

ZombieWave::ZombieWave(int numZombies, int startTime, int endTime, const cocos2d::Rect& region, int minValidPoints)
    : _numZombies(numZombies), _startTime(startTime), _endTime(endTime), _region(region),
      _minValidPoints(minValidPoints)
{
    _zombies.reserve(numZombies);
}

ZombieWave::ZombieWave(FILE* f, std::deque<Person>& people)
{
    _numZombies = Utils::readInt(f);
    _startTime = Utils::readInt(f);
    _endTime = Utils::readInt(f);
    _region.origin.x = Utils::readFloat(f);
    _region.origin.y = Utils::readFloat(f);
    _region.size.width = Utils::readFloat(f);
    _region.size.height = Utils::readFloat(f);
    _minValidPoints = Utils::readInt(f);
    for (int i = 0; i < _numZombies; i++)
        addZombie(&people[Utils::readInt(f)], nullptr);
    _released = Utils::readInt(f);
}

void ZombieWave::addZombie(Person* p, SearchBucket* bucket)
{
    _zombies.push_back({p, bucket});
}

void ZombieWave::checkNumZombies() const
{
    if (_numZombies == (int)_zombies.size())
        return;
    throw std::runtime_error("Expected zombie wave to contain " + Utils::intToString(_numZombies) +
                             " zombies, but it contains " + Utils::intToString((int)_zombies.size()) +
                             " zombies");
}

int ZombieWave::update(int t)
{
    if (t < _startTime || _released >= _numZombies)
        return 0;
    int due = _numZombies;
    if (_startTime < _endTime) {
        due = _numZombies * (t - _startTime) / (_endTime - _startTime);
        if (due > _numZombies)
            due = _numZombies;
    }
    int added = due - _released;
    if (added <= 0)
        return 0;
    while (_released < due) {
        ZombieInfo& z = _zombies[_released];
        z.person->state = PersonStateZombieWander;
        z.bucket->people.push_back(z.person);
        _released++;
    }
    return added;
}

void ZombieWave::writeToSaveFile(FILE* f, std::unordered_map<const Person*, int>& indices) const
{
    Utils::writeInt(f, _numZombies);
    Utils::writeInt(f, _startTime);
    Utils::writeInt(f, _endTime);
    Utils::writeFloat(f, _region.origin.x);
    Utils::writeFloat(f, _region.origin.y);
    Utils::writeFloat(f, _region.size.width);
    Utils::writeFloat(f, _region.size.height);
    Utils::writeInt(f, _minValidPoints);
    for (const ZombieInfo& z : _zombies)
        Utils::writeInt(f, indices[z.person]);
    Utils::writeInt(f, _released);
}
