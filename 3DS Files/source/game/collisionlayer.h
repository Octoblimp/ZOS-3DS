// CollisionLayer and CollisionOverlay (decomp/classes/CollisionLayer.c,
// CollisionOverlay.c).
//
// The layer is one byte per 4x4 map pixels. Coordinates in are map pixels
// unless said otherwise; getValue and the overlay calls shift them by 2.
#pragma once

#include <string>
#include <vector>

#include "game/mapconfig.h"

class CollisionOverlay {
public:
    // width x height cells of a raw file (data/helicopter.co is 40 x 10).
    CollisionOverlay(const std::string& path, int width, int height);

    int getWidth() const { return _width; }
    int getHeight() const { return _height; }
    const std::vector<unsigned char>& getData() const { return _data; }
    // In map pixels, relative to the overlay's centre; 0 outside it.
    unsigned char getValue(int x, int y) const;

private:
    int _width;
    int _height;
    std::vector<unsigned char> _data;
};

class CollisionLayer {
public:
    // mapWidth x mapHeight map pixels. Embedded maps read
    // data/collision_layer_map%02d.cl, Google's data/collision_layer_%d.cl;
    // a player (downloaded) map reads playerPath.
    CollisionLayer(MapConfig::MapType type, int mapId, int mapWidth, int mapHeight,
                   const std::string& playerPath = std::string());
    CollisionLayer(const CollisionLayer& other);
    CollisionLayer& operator=(const CollisionLayer&) = delete;

    // In cells.
    int getWidth() const { return _width; }
    int getHeight() const { return _height; }
    std::vector<unsigned char>& getData() { return _data; }
    const std::vector<unsigned char>& getData() const { return _data; }
    // Bumped when an overlay goes on or comes off, so the renderer knows to
    // re-upload.
    int getVersion() const { return _version; }

    unsigned char getValue(int x, int y) const { return _data[_width * (y >> 2) + (x >> 2)]; }

    // A line of sight from cell (cellX, cellY) to the map pixel (x, y): true
    // when every cell between has bit 0x2 (see-through) set. The end cells
    // are not tested.
    bool isVisibleFrom(int cellX, int cellY, int x, int y) const;

    // An overlay centred on cell (cellX, cellY) fits inside the layer, and
    // every cell it covers is currently value.
    bool canApplyOverlay(const CollisionOverlay* overlay, int cellX, int cellY, int value) const;
    void applyOverlay(const CollisionOverlay* overlay, int cellX, int cellY);
    // Sets the cells the overlay covers back to value.
    void removeOverlay(const CollisionOverlay* overlay, int cellX, int cellY, int value);

private:
    void paintOverlay(const CollisionOverlay* overlay, int cellX, int cellY, int value);

    int _width;
    int _height;
    std::vector<unsigned char> _data;
    int _version;
};
