#include "game/mapconfigfactory.h"

#include <stdexcept>

const char* MapConfig::mapTypeToString(MapType type)
{
    switch (type) {
    case MapType::Google:
        return "google";
    case MapType::Embedded:
        return "embedded";
    case MapType::Player:
        return "player";
    }
    throw std::runtime_error("Invalid map type");
}

MapConfigFactory::~MapConfigFactory()
{
    for (MapConfig*& m : _maps) {
        delete m;
        m = nullptr;
    }
}

void MapConfigFactory::updateMapAvailability(bool allMaps)
{
    for (MapConfig* m : _maps)
        m->available = allMaps ? true : m->free;
}

MapConfig* MapConfigFactory::getMapByTypeAndId(MapConfig::MapType type, int id) const
{
    for (MapConfig* m : _maps)
        if (m->type == type && m->id == id)
            return m;
    return nullptr;
}
