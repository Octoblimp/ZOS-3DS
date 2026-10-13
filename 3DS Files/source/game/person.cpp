#include "game/person.h"

#include <stdexcept>
#include <string>

#include "game/utils.h"

namespace {

// DAT_00513c80.
const PersonState kSaveFileStates[16] = {
    PersonStateNone,         PersonStateWalk,         PersonStateHunt,         PersonStateShoot,
    PersonStateUnused,       PersonStateIdle,         PersonStateFlee,         PersonStateTurning,
    PersonStateDying,        PersonStateDead,         PersonStateZombieRising, PersonStateZombieWander,
    PersonStateZombieChase,  PersonStateZombieAttack, PersonStateZombieDying,  PersonStateZombieDead,
};

} // namespace

namespace PersonStateUtils {

SaveFilePersonState personStateToSaveFilePersonState(PersonState state)
{
    for (int i = 0; i < 16; i++)
        if (kSaveFileStates[i] == state)
            return (SaveFilePersonState)i;
    throw std::runtime_error("Invalid person state to convert to save file person state: " +
                             Utils::intToString((int)state));
}

PersonState saveFilePersonStateToPersonState(SaveFilePersonState state)
{
    if ((unsigned)state < 16)
        return kSaveFileStates[state];
    throw std::runtime_error("Invalid save file person state to convert to person state: " +
                             Utils::intToString((int)state));
}

} // namespace PersonStateUtils
