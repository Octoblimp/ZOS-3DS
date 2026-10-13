#include "game/collisionlayer.h"

#include <cstdio>
#include <cstdlib>
#include <stdexcept>

#include "game/utils.h"

CollisionOverlay::CollisionOverlay(const std::string& path, int width, int height)
    : _width(width), _height(height)
{
    Utils::loadBinaryFile(path, _data);
    if ((int)_data.size() != width * height)
        throw std::runtime_error(path + " has length " + Utils::intToString((int)_data.size()) + ", expected " +
                                 Utils::intToString(width * height));
}

unsigned char CollisionOverlay::getValue(int x, int y) const
{
    int ix = (_width >> 1) + (x >> 2);
    int iy = (_height >> 1) + (y >> 2);
    if (ix < 0 || iy < 0 || ix >= _width || iy >= _height)
        return 0;
    return _data[iy * _width + ix];
}

CollisionLayer::CollisionLayer(MapConfig::MapType type, int mapId, int mapWidth, int mapHeight,
                               const std::string& playerPath)
    : _width(mapWidth >> 2), _height(mapHeight >> 2), _version(1)
{
    std::string path;
    char buf[256];
    if (type == MapConfig::MapType::Player) {
        path = playerPath;
    } else if (type == MapConfig::MapType::Embedded) {
        snprintf(buf, sizeof(buf), "data/collision_layer_map%02d.cl", mapId);
        path = buf;
    } else if (type == MapConfig::MapType::Google) {
        snprintf(buf, sizeof(buf), "data/collision_layer_%d.cl", mapId);
        path = buf;
    } else {
        throw std::runtime_error("Invalid map type");
    }
    std::vector<unsigned char> packed;
    Utils::loadBinaryFile(path, packed);
    Utils::decompressData(packed, _width * _height, _data);
}

CollisionLayer::CollisionLayer(const CollisionLayer& other)
    : _width(other._width), _height(other._height), _data(other._data), _version(1)
{
}

bool CollisionLayer::isVisibleFrom(int x0, int y0, int x, int y) const
{
    // Bresenham, as the game has it: x0/y0 are cells already, the target is
    // map pixels.
    int x1 = x >> 2;
    int y1 = y >> 2;
    int dx = std::abs(x1 - x0);
    int dy = std::abs(y1 - y0);
    if (dx == 0 && dy == 0)
        return true;
    int sy = y1 < y0 ? -1 : 1;
    int sx = x1 < x0 ? -1 : 1;
    if (dy < dx) {
        int err = dx / 2;
        do {
            x0 += sx;
            if (x0 == x1)
                return true;
            err -= dy;
            if (err < 0) {
                y0 += sy;
                err += dx;
            }
        } while (_data[_width * y0 + x0] & 2);
    } else {
        int err = dy / 2;
        do {
            y0 += sy;
            if (y0 == y1)
                return true;
            err -= dx;
            if (err < 0) {
                x0 += sx;
                err += dy;
            }
        } while (_data[_width * y0 + x0] & 2);
    }
    return false;
}

bool CollisionLayer::canApplyOverlay(const CollisionOverlay* overlay, int cellX, int cellY, int value) const
{
    int ow = overlay->getWidth();
    int oh = overlay->getHeight();
    int left = cellX - ow / 2;
    int top = cellY - oh / 2;
    if ((left | top) < 0 || left + ow > _width || top + oh > _height)
        return false;
    const unsigned char* src = overlay->getData().data();
    for (int oy = 0; oy < oh; oy++) {
        const unsigned char* row = &_data[_width * (top + oy) + left];
        for (int ox = 0; ox < ow; ox++)
            if (src[oy * ow + ox] != 0 && row[ox] != value)
                return false;
    }
    return true;
}

// applyOverlay (value < 0: the overlay's own cells) and removeOverlay. The
// game bounds x by the layer's height, not its width. That is kept, and the
// width bound added so a tall map cannot wrap into the next row.
void CollisionLayer::paintOverlay(const CollisionOverlay* overlay, int cellX, int cellY, int value)
{
    int ow = overlay->getWidth();
    int oh = overlay->getHeight();
    int left = cellX - ow / 2;
    int top = cellY - oh / 2;
    const unsigned char* src = overlay->getData().data();
    for (int oy = 0; oy < oh; oy++) {
        int y = top + oy;
        if (y < 0)
            continue;
        if (y >= _height)
            break;
        for (int ox = 0; ox < ow; ox++) {
            int x = left + ox;
            if (x < 0)
                continue;
            if (x >= _width || x >= _height)
                break;
            unsigned char c = src[oy * ow + ox];
            if (c != 0)
                _data[_width * y + x] = value < 0 ? c : (unsigned char)value;
        }
    }
    _version++;
}

void CollisionLayer::applyOverlay(const CollisionOverlay* overlay, int cellX, int cellY)
{
    paintOverlay(overlay, cellX, cellY, -1);
}

void CollisionLayer::removeOverlay(const CollisionOverlay* overlay, int cellX, int cellY, int value)
{
    paintOverlay(overlay, cellX, cellY, value);
}
