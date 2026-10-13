// MapConfig (decomp/classes/MapConfig.c): one map's description, 0x4c
// bytes in the game. The fields are named from MapConfigFactory's data,
// GameNode::startMap/setupMap and MapSelectScene.
#pragma once

#include <string>

class MapConfig {
public:
    // mapTypeToString: "google", "embedded", "player". Google's maps were
    // downloaded from class3outbreak.com; the port keeps the type for the
    // featured maps' ids and saves, and fills "player" maps from
    // OpenStreetMap.
    enum class MapType { Google = 1, Embedded = 2, Player = 3 };

    MapConfig() = default;

    static const char* mapTypeToString(MapType type);

    int id = -1;                         // 0x00
    MapType type = MapType::Google;      // 0x04
    // The map's top left in world pixels at zoom 20 (Web Mercator, 2^28 px
    // around the world).
    int worldX = 0;                      // 0x08
    int worldY = 0;                      // 0x0c
    // Map pixels (zoom 20).
    int width = -1;                      // 0x10
    int height = -1;                     // 0x14
    // Where the view starts, in map pixels.
    int startX = -1;                     // 0x18
    int startY = -1;                     // 0x1c
    // The pin on MapSelectScene's world map, as fractions of its width and
    // height (equirectangular, y down).
    float pinX = 0.5f;                   // 0x20
    float pinY = 0.5f;                   // 0x24
    // GameOptions::populateValues's ZombieOutbreak and its last argument.
    int zombieOutbreak = 0;              // 0x28
    int populationLimit = 0x7fffffff;    // 0x2c
    bool free = false;                   // 0x30
    // free, or everything unlocked (updateMapAvailability).
    bool available = false;              // 0x31
    // The options below replace the defaults (a player map's saved setup).
    bool hasPresetOptions = false;       // 0x32
    int presetNumCivs = 0;               // 0x34
    int presetArmedCivs = 0;             // 0x38
    int presetCivAccuracy = 0;           // 0x3c
    int presetNumZombies = 0;            // 0x40
    int presetZombieSpeed = 2;           // 0x44
    int presetInfectionTime = 0;         // 0x48

    // The port's: a player map's folder on the card (tiles, collision).
    std::string playerPath;
};
