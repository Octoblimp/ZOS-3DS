// Person, PersonState, PersonStateUtils (decomp/classes/Person.c,
// PersonStateUtils.c; the states are Simulation::step's).
//
// A state is a number in the low byte plus flags that say what it is: the
// simulation tests the flags, never the list of states.
#pragma once

#include <list>
#include <vector>

class PersonType;

namespace PersonStateFlag {
enum : int {
    Zombie = 0x100,
    // Walks towards its target (step's movement).
    Moving = 0x200,
    // Shooters aim at it.
    LiveZombie = 0x400,
    // Zombies go for it.
    LiveHuman = 0x800,
    // A bite turns it at infectedTime + the infection time; a dead civilian
    // with it rises.
    Infectable = 0x1000,
    ZombieAnimated = 0x2000,
    // Others push it out of the way.
    Pushable = 0x4000,
};
}

enum PersonState : int {
    PersonStateNone = 0,
    PersonStateWalk = 0x5a01,
    PersonStateHunt = 0x5a02,
    PersonStateShoot = 0x803,
    // In the save file's table but never set.
    PersonStateUnused = 0x804,
    PersonStateIdle = 0x5805,
    PersonStateFlee = 0x5a06,
    // A bitten human turning.
    PersonStateTurning = 7,
    PersonStateDying = 8,
    // A dead civilian, who rises if infected.
    PersonStateDead = 0x1009,
    PersonStateZombieRising = 0x10a,
    PersonStateZombieWander = 0x670b,
    PersonStateZombieChase = 0x670c,
    PersonStateZombieAttack = 0x670d,
    PersonStateZombieDying = 0x10e,
    PersonStateZombieDead = 0x10f,
};

// The save file numbers the states 0..15 (the table at 0x513c80).
enum SaveFilePersonState : int {};

namespace PersonStateUtils {
// Throw "Invalid person state to convert to save file person state: N" and
// "Invalid save file person state to convert to person state: N".
SaveFilePersonState personStateToSaveFilePersonState(PersonState state);
PersonState saveFilePersonStateToPersonState(SaveFilePersonState state);
} // namespace PersonStateUtils

// 0x64 bytes in the game.
struct Person {
    // infectedTime when never bitten.
    static const int NeverInfected = 0x40000000;

    PersonType* type = nullptr;     // 0x00
    float x = 0, y = 0;             // 0x04
    float targetX = 0, targetY = 0; // 0x0c
    float dirX = 1.0f, dirY = 0;    // 0x14
    // When the current animation began (gameTime, ms).
    int animTime = 0;               // 0x1c
    // The collision layer's value under the person.
    int collisionValue = 0;         // 0x20
    Person* target = nullptr;       // 0x24
    PersonState state = PersonStateNone;    // 0x28
    // The last shot or bite.
    int lastActionTime = -100000;   // 0x2c
    // A civilian with a gun (police and soldiers always are).
    bool armed = false;             // 0x30
    // When Idle or Walk ends.
    int timer = 0;                  // 0x34
    // Which idle animation, 1 or 2.
    int idleAnim = 1;               // 0x38
    // Which death animation, 1 or 2 (2 is the player's doing).
    int dieAnim = 1;                // 0x3c
    int infectedTime = NeverInfected;   // 0x40
    // Who bit it, or what it flees.
    Person* attacker = nullptr;     // 0x44
    int attackedTime = -1000000;    // 0x48
    int health = 0;                 // 0x4c
    // Pushes from neighbours this step, summed, and how many.
    float pushX = 0, pushY = 0;     // 0x50
    int pushCount = 0;              // 0x58
    // Knockback from a blast, in pixels a second.
    float velX = 0, velY = 0;       // 0x5c
};

// 128 x 128 map pixels of the search grid. The living are in people, the
// dead (PersonStateDead, PersonStateZombieDead) in corpses.
struct SearchBucket {
    SearchBucket() { people.reserve(20); }

    std::vector<Person*> people;    // 0x00
    std::list<Person*> corpses;     // 0x0c
};
