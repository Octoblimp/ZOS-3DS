// MapConfigFactory (decomp/classes/MapConfigFactory.c): every map the game
// knows, generated from its constructor (mapconfigs_data.cpp). 15 were "google" maps whose tiles and collision the
// game downloaded from class3outbreak.com; the port can rebuild those areas
// from OpenStreetMap. The other 5 ship with the game.
#pragma once

#include <vector>

#include "game/mapconfig.h"

class MapConfigFactory {
public:
    MapConfigFactory() { createMaps(); }
    ~MapConfigFactory();
    MapConfigFactory(const MapConfigFactory&) = delete;
    MapConfigFactory& operator=(const MapConfigFactory&) = delete;

    // Every map available, or only the free ones.
    void updateMapAvailability(bool allMaps);
    const std::vector<MapConfig*>& getMaps() const { return _maps; }
    // nullptr if there is none.
    MapConfig* getMapByTypeAndId(MapConfig::MapType type, int id) const;

private:
    void createMaps();

    std::vector<MapConfig*> _maps;
};
