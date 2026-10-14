#include "game/maprenderer.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <set>
#include <stdexcept>

#include "cocos/director.h"
#include "game/utils.h"
#include "workers.h"

using cocos2d::Mat4;
using cocos2d::Vec2;

namespace {
// The imagery's credit, as a tile's copyright line (the service's
// copyrightText).
const char* const kImageryCredit = "Source: Esri, Vantor, Earthstar Geographics, and the GIS User Community";
// A tile that failed to download is asked for again after this many frames
// (ten seconds).
const int kRetryFrames = 600;
// One the loader dropped from its queue (the view moved on), after this many
// (half a second), rather than every frame.
const int kCancelledRetryFrames = 30;
// The most answers made textures in a frame (each an online tile's 128 KB
// copied into linear memory): a burst of them waits for the next frames
// rather than make one long.
const int kMaxTexturesPerFrame = 4;
} // namespace

MapRenderer::TileInfo& MapRenderer::ZoomLevelInfo::getTile(int x, int y)
{
    const long long key = (long long)tilesX * y + x;
    return tiles[key];
}

MapRenderer::MapRenderer(float density) : _density(density)
{
    LightLock_Init(&_lock);
    CondVar_Init(&_cond);
    Utils::log("Map renderer max tiles: %d", _maxTiles);
    // On the workers' core (workers.h), so a frame is never kept waiting on
    // a file or a JPEG's decoding.
    _thread = createWorkerThread(threadWrapper, this, 32 * 1024, 1);
    if (!_thread)
        throw std::runtime_error("Error creating background thread");
}

MapRenderer::~MapRenderer()
{
    LightLock_Lock(&_lock);
    _quit = true;
    _requests.clear();
    CondVar_Broadcast(&_cond);
    LightLock_Unlock(&_lock);
    threadJoin(_thread, U64_MAX);
    threadFree(_thread);
    if (_usedOnline)
        SatelliteTileLoader::getInstance()->cancel(this, true);
    releaseAllTextures();
}

void MapRenderer::threadWrapper(void* arg)
{
    static_cast<MapRenderer*>(arg)->onBackgroundThread();
}

void MapRenderer::onBackgroundThread()
{
    for (;;) {
        LightLock_Lock(&_lock);
        while (_requests.empty() && !_quit)
            CondVar_Wait(&_cond, &_lock);
        if (_quit) {
            LightLock_Unlock(&_lock);
            return;
        }
        TileLoadInfo info = std::move(_requests.front());
        _requests.pop_front();
        LightLock_Unlock(&_lock);

        // Online, a tile not kept (the open map's pack, or memory) goes to
        // SatelliteTileLoader, whose answer comes back through
        // onTileDownloaded.
        if (info.type != MapType::Online) {
            loadTileData(info.type, info.path, info.zoom, info.x, info.y, info.data, info.copyrights);
        } else if (!loadCachedTile(info)) {
            LightLock_Lock(&_lock);
            _onlineStats.cardReads++;
            _onlineStats.sent++;
            LightLock_Unlock(&_lock);
            SatelliteTileLoader::Request r;
            r.zoom = info.zoom;
            r.x = info.x;
            r.y = info.y;
            r.tile = info.tile;
            r.generation = info.generation;
            SatelliteTileLoader::getInstance()->request(this, r);
            continue;
        } else {
            LightLock_Lock(&_lock);
            _onlineStats.cardReads++;
            LightLock_Unlock(&_lock);
        }

        LightLock_Lock(&_lock);
        _loaded.push_back(std::move(info));
        LightLock_Unlock(&_lock);
    }
}

void MapRenderer::onTileDownloaded(SatelliteTileLoader::Answer&& answer)
{
    TileLoadInfo info;
    info.tile = static_cast<TileInfo*>(answer.request.tile);
    info.zoom = answer.request.zoom;
    info.x = answer.request.x;
    info.y = answer.request.y;
    info.generation = answer.request.generation;
    info.type = MapType::Online;
    info.pixels = std::move(answer.pixels);
    if (answer.status == SatelliteTileLoader::Status::Loaded)
        info.copyrights.push_back(kImageryCredit);
    info.retryLater = answer.status == SatelliteTileLoader::Status::Failed;
    info.cancelled = answer.status == SatelliteTileLoader::Status::Cancelled;
    info.missing = answer.status == SatelliteTileLoader::Status::Missing;
    LightLock_Lock(&_lock);
    if (answer.status == SatelliteTileLoader::Status::Cancelled)
        _onlineStats.dropped++;
    else if (answer.status == SatelliteTileLoader::Status::Loaded)
        _onlineStats.downloaded++;
    _loaded.push_back(std::move(info));
    LightLock_Unlock(&_lock);
}

void MapRenderer::onSurfaceChanged(int width, int height)
{
    _screen = cocos2d::Size((float)width, (float)height);
}

std::string MapRenderer::tilePath(int zoom, int x, int y) const
{
    if (_mapType != MapType::Embedded)
        return std::string();
    // The game's "images/maps/map%02d/tile_%02d_%02d_%02d.jpg": zoom, then
    // the row, then the column.
    char name[64];
    snprintf(name, sizeof(name), "tile_%02d_%02d_%02d.t3x", zoom, y, x);
    return _tileDir + name;
}

bool MapRenderer::loadCachedTile(TileLoadInfo& info)
{
    info.copyrights.clear();
    switch (SatelliteTileLoader::getInstance()->loadCached(info.zoom, info.x, info.y, info.pixels)) {
    case SatelliteTileLoader::Cached::Tile:
        info.copyrights.push_back(kImageryCredit);
        return true;
    case SatelliteTileLoader::Cached::Missing:
        info.missing = true;
        return true;
    default:
        return false;
    }
}

bool MapRenderer::loadTileData(MapType type, const std::string& path, int zoom, int x, int y,
                               std::vector<u8>& data, std::vector<std::string>& copyrights)
{
    data.clear();
    copyrights.clear();
    if (type == MapType::Embedded) {
        FILE* f = fopen(path.c_str(), "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            const long size = ftell(f);
            fseek(f, 0, SEEK_SET);
            if (size > 0) {
                data.resize(size);
                if (fread(data.data(), 1, size, f) != (size_t)size)
                    data.clear();
            }
            fclose(f);
        }
    } else {
        throw std::runtime_error("Invalid map type");
    }
    if (data.empty()) {
        Utils::log("Error loading tile %d %d %d", zoom, x, y);
        return false;
    }
    return true;
}

bool MapRenderer::createTileTexture(gfx::Texture& out, const std::vector<u8>& data, int zoom, int x, int y)
{
    if (!gfx::loadTextureFromMemory(&out, data.data(), data.size())) {
        Utils::log("Error loading tile %d %d %d", zoom, x, y);
        return false;
    }
    if (out.width != 256 || out.height != 256) {
        Utils::log(out.width != 256 ? "Tile %d %d %d has unexpected width %d"
                                    : "Tile %d %d %d has unexpected height %d",
                   zoom, x, y, out.width != 256 ? out.width : out.height);
        gfx::destroyTexture(&out);
        return false;
    }
    return true;
}

bool MapRenderer::createTileTexture(TileInfo& tile, TileLoadInfo& info)
{
    if (info.type != MapType::Online)
        return !info.data.empty() && createTileTexture(tile.texture, info.data, info.zoom, info.x, info.y);
    if (info.pixels.empty())
        return false;
    const int size = SatelliteTileLoader::kTileSize;
    if (!gfx::createTiledTexture(&tile.texture, info.pixels.data(), info.pixels.size() * sizeof(u16), size, size,
                                 GPU_RGB565)) {
        Utils::log("Error loading tile %d %d %d", info.zoom, info.x, info.y);
        return false;
    }
    return true;
}

bool MapRenderer::loadTileTexture(TileInfo& tile, int zoom, int x, int y)
{
    TileLoadInfo info;
    info.zoom = zoom;
    info.x = x;
    info.y = y;
    info.type = _mapType;
    if (_mapType == MapType::Online) {
        if (!loadCachedTile(info))
            return false;
        if (info.missing) {
            tile.retryFrame = INT_MAX;
            return false;
        }
        tile.copyrights = std::move(info.copyrights);
        return createTileTexture(tile, info);
    }
    if (!loadTileData(_mapType, tilePath(zoom, x, y), zoom, x, y, info.data, tile.copyrights))
        return false;
    return createTileTexture(tile, info);
}

void MapRenderer::setupMap(MapType type, int mapId, int width, int height)
{
    // (The game left this to clear: loads still queued for the old tiles
    // would land in freed ones.)
    LightLock_Lock(&_lock);
    _requests.clear();
    LightLock_Unlock(&_lock);
    _generation++;
    releaseAllTextures();
    _mapType = type;
    _mapId = mapId;
    _levels.clear();
    _numTilesLoaded = 0;
    _frame = 0;
    _copyright.clear();
    _copyrightFrame = -1000;
    _copyrightUpdated = false;
    if (_usedOnline)
        SatelliteTileLoader::getInstance()->cancel(this, false);
    _maxTiles = type == MapType::Online ? 80 : 150;
    if (type == MapType::Embedded) {
        _maxZoom = 20;
        _levels.resize(21);
        if ((width | height) & 0xff)
            throw std::runtime_error("The playing area must be an integral number of tiles in width and height");
        ZoomLevelInfo& top = _levels[_maxZoom];
        top.tilesX = width / 256;
        top.tilesY = height / 256;
        if (top.tilesX != top.tilesY)
            throw std::runtime_error("The number of tiles must be a square");
        if (!Utils::isPowerOf2(top.tilesX) || !Utils::isPowerOf2(top.tilesY))
            throw std::runtime_error("The numbers of tiles must be a power of 2");
        // Each level down halves the tiles across, to a single tile.
        int zoom = _maxZoom;
        int tx = top.tilesX, ty = top.tilesY;
        do {
            const bool more = tx > 3 || ty > 3;
            zoom--;
            tx /= 2;
            ty /= 2;
            _levels[zoom].tilesX = tx;
            _levels[zoom].tilesY = ty;
            if (!more)
                break;
        } while (true);
        _minZoom = zoom;
        char name[64];
        snprintf(name, sizeof(name), "images/maps/map%02d/tile_%02d_00_00.jpg", mapId, _minZoom);
        _tileDir = cocos2d::FileUtils::getInstance()->fullPathForFilename(name);
        _tileDir.erase(_tileDir.rfind('/') + 1);
    } else if (type == MapType::Online) {
        // The game's Google levels ran 1..21. The port's stop at 20, where a
        // tile pixel is a map pixel; deeper, the 20s are stretched.
        _minZoom = 1;
        _maxZoom = SatelliteTileLoader::kMaxZoom;
        // The loader, its threads and the network, made here on the main
        // thread.
        SatelliteTileLoader::getInstance();
        _usedOnline = true;
        _levels.resize(_maxZoom + 1);
        for (int z = _minZoom; z <= _maxZoom; z++)
            _levels[z].tilesX = _levels[z].tilesY = 1 << z;
    } else {
        throw std::runtime_error("Invalid map type");
    }
}

void MapRenderer::preloadTiles(double x, double y, float scale)
{
    const u64 start = osGetTime();
    const int before = _numTilesLoaded;
    int zoom, minX, minY, maxX, maxY, tileSizeX, tileSizeY;
    float screenTileW, screenTileH;
    calculateZoomLevelAndTileRange((int)x, (int)y, _density * scale, zoom, minX, minY, maxX, maxY, tileSizeX,
                                   tileSizeY, screenTileW, screenTileH);
    for (int ty = minY; ty <= maxY; ty++) {
        for (int tx = minX; tx <= maxX; tx++) {
            // The tile, or failing it its nearest ancestor that loads.
            int z = zoom, ax = tx, ay = ty;
            TileInfo* tile = &_levels[z].getTile(ax, ay);
            if (tile->texture.valid)
                continue;
            bool loaded = loadTileTexture(*tile, z, ax, ay);
            // Online, a tile not on the card is left to render (to download),
            // its ancestors with it: the card is not searched level by level.
            while (!loaded && _mapType == MapType::Embedded) {
                if (z <= _minZoom)
                    break;
                z--;
                ax >>= 1;
                ay >>= 1;
                tile = &_levels[z].getTile(ax, ay);
                if (tile->texture.valid)
                    break;
                loaded = loadTileTexture(*tile, z, ax, ay);
            }
            if (loaded)
                _numTilesLoaded++;
        }
    }
    // The bottom tile, which render falls back on.
    if (_mapType == MapType::Embedded) {
        TileInfo& bottom = _levels[_minZoom].getTile(0, 0);
        if (!bottom.texture.valid && loadTileTexture(bottom, _minZoom, 0, 0))
            _numTilesLoaded++;
    }
    Utils::verboseLog("%s line %d num tiles %d", "void MapRenderer::preloadTiles(double, double, float)", 0x28c,
                      _numTilesLoaded);
    Utils::log("map: preloaded %d tiles in %d ms", _numTilesLoaded - before, (int)(osGetTime() - start));
}

void MapRenderer::calculateZoomLevelAndTileRange(int x, int y, float scale, int& zoom, int& minX, int& minY,
                                                 int& maxX, int& maxY, int& tileSizeX, int& tileSizeY,
                                                 float& screenTileW, float& screenTileH, float viewWidth,
                                                 float viewHeight) const
{
    // The deepest level whose tiles are over 128 screen pixels.
    zoom = _maxZoom;
    while (zoom > _minZoom) {
        if ((float)(1.0 / ldexp(1.0, 20 - zoom)) * 0.5f < scale)
            break;
        zoom--;
    }
    if (zoom <= 20) {
        tileSizeX = 256 << (20 - zoom);
        tileSizeY = 256 << (20 - zoom);
    } else {
        tileSizeX = 256 >> (zoom - 20);
        tileSizeY = 256 >> (zoom - 20);
    }
    screenTileW = (float)tileSizeX * scale;
    screenTileH = (float)tileSizeY * scale;
    // The game passes the screen's width for both extents.
    if (viewWidth <= 0.0f || viewHeight <= 0.0f) {
        const int extent = (int)(_screen.width / scale);
        calculateTileRange(x, y, extent, extent, zoom, minX, minY, maxX, maxY);
        return;
    }
    // The port's smaller view: its own extents.
    int ignored;
    calculateTileRange(x, y, (int)(viewWidth / scale), 0, zoom, minX, ignored, maxX, ignored);
    calculateTileRange(x, y, (int)(viewHeight / scale), 0, zoom, ignored, minY, ignored, maxY);
}

void MapRenderer::calculateTileRange(int x, int y, int width, int /*height*/, int zoom, int& minX, int& minY,
                                     int& maxX, int& maxY) const
{
    // A tile at this zoom is 2^(28 - zoom) zoom-20 pixels across. As the
    // game, the width bounds both axes.
    const int shift = 28 - zoom;
    const ZoomLevelInfo& level = _levels[zoom];
    auto clampTo = [](int v, int count) { return v < 1 ? 0 : std::min(v, count - 1); };
    minX = clampTo(x >> shift, level.tilesX);
    minY = clampTo(y >> shift, level.tilesY);
    maxX = clampTo((x + width) >> shift, level.tilesX);
    maxY = clampTo((y + width) >> shift, level.tilesY);
}

Vec2 MapRenderer::toNode(const Mat4& transform, float x, float y) const
{
    return transform.apply(Vec2(x, _screen.height - y));
}

void MapRenderer::render(double x, double y, float scale, const Mat4& transform, float viewWidth, float viewHeight)
{
    _frame++;
    handleLoadedTiles();
    if (_mapType == MapType::None || _levels.empty())
        return;
    const int sinceCopyright = _frame - _copyrightFrame;
    std::set<std::string> copyrights;
    int viewReady = 0, viewTotal = 0;

    const int ix = (int)x, iy = (int)y;
    const float s = _density * scale;
    int zoom, minX, minY, maxX, maxY, tileSizeX, tileSizeY;
    float screenTileW, screenTileH;
    calculateZoomLevelAndTileRange(ix, iy, s, zoom, minX, minY, maxX, maxY, tileSizeX, tileSizeY, screenTileW,
                                   screenTileH, viewWidth, viewHeight);
    auto collect = [&](const TileInfo& tile) {
        if (sinceCopyright >= 60)
            copyrights.insert(tile.copyrights.begin(), tile.copyrights.end());
    };
    auto draw = [&](const TileInfo& tile, float sx, float sy, float u0, float v0, float u1, float v1) {
        gfx::bindTexture(&tile.texture);
        const Vec2 a = toNode(transform, sx, sy);
        const Vec2 b = toNode(transform, sx + screenTileW, sy + screenTileH);
        gfx::quad(a.x, a.y, b.x, b.y, u0, v0, u1, v1, gfx::kWhite);
    };

    for (int ty = minY; ty <= maxY; ty++) {
        // The tile's top on screen, kept exact below a pixel.
        const float sy = s * (float)((double)(ty * tileSizeY - iy) - (y - (double)iy));
        for (int tx = minX; tx <= maxX; tx++) {
            const float sx = s * (float)((double)(tileSizeX * tx - ix) - (x - (double)ix));
            TileInfo& tile = _levels[zoom].getTile(tx, ty);
            viewTotal++;
            if (tile.texture.valid) {
                viewReady++;
                tile.lastRenderFrame = _frame;
                collect(tile);
                draw(tile, sx, sy, 0.0f, 0.0f, 1.0f, 1.0f);
                continue;
            }
            // The imagery has none better: what is drawn for it is the tile.
            if (tile.retryFrame == INT_MAX)
                viewReady++;
            else if (!tile.loading && _frame >= tile.retryFrame)
                beginAsyncLoadTileTexture(tile, zoom, tx, ty);
            // Meanwhile, the part of the nearest loaded ancestor over it.
            int ax = tx, ay = ty;
            for (int up = 1; zoom - up >= _minZoom; up++) {
                ax >>= 1;
                ay >>= 1;
                TileInfo& parent = _levels[zoom - up].getTile(ax, ay);
                if (parent.texture.valid) {
                    parent.lastRenderFrame = _frame;
                    collect(parent);
                    const float n = (float)(1 << up);
                    const float u0 = (float)(tx - (ax << up)) / n;
                    const float v0 = (float)(ty - (ay << up)) / n;
                    draw(parent, sx, sy, u0, v0, u0 + 1.0f / n, v0 + 1.0f / n);
                    break;
                }
                // Online, the ancestors are asked for too; an embedded map
                // has its bottom tile preloaded.
                if (_mapType == MapType::Online && !parent.loading && _frame >= parent.retryFrame)
                    beginAsyncLoadTileTexture(parent, zoom - up, ax, ay);
            }
        }
    }

    _viewReady = viewReady;
    _viewTotal = viewTotal;

    if (sinceCopyright >= 60) {
        std::string combined;
        for (const std::string& c : copyrights) {
            if (!combined.empty())
                combined += ", ";
            combined += c;
        }
        if (combined != _copyright) {
            _copyright = combined;
            _copyrightUpdated = true;
        }
        _copyrightFrame = _frame;
    }
}

const std::string& MapRenderer::getRenderedTilesCombinedCopyright()
{
    _copyrightUpdated = false;
    return _copyright;
}

void MapRenderer::beginAsyncLoadTileTexture(TileInfo& tile, int zoom, int x, int y)
{
    tile.loading = true;
    TileLoadInfo info;
    info.tile = &tile;
    info.zoom = zoom;
    info.x = x;
    info.y = y;
    info.generation = _generation;
    info.type = _mapType;
    info.path = tilePath(zoom, x, y);
    LightLock_Lock(&_lock);
    if (_mapType == MapType::Online)
        _onlineStats.asked++;
    _requests.push_back(std::move(info));
    CondVar_Signal(&_cond);
    LightLock_Unlock(&_lock);
}

void MapRenderer::handleLoadedTiles()
{
    LightLock_Lock(&_lock);
    if (_trimMemory || _numTilesLoaded > _maxTiles) {
        // Every level but the bottom: tiles not drawn for 60 frames.
        for (int z = _minZoom + 1; z <= _maxZoom && z < (int)_levels.size(); z++) {
            for (auto& entry : _levels[z].tiles) {
                TileInfo& tile = entry.second;
                if (tile.texture.valid && tile.lastRenderFrame < _frame - 60) {
                    gfx::destroyTexture(&tile.texture);
                    _numTilesLoaded--;
                }
            }
        }
        Utils::verboseLog("%s line %d num tiles %d", "void MapRenderer::handleLoadedTiles()", 0x6d3, _numTilesLoaded);
        _trimMemory = false;
    }
    std::deque<TileLoadInfo> loaded;
    loaded.swap(_loaded);
    LightLock_Unlock(&_lock);

    int made = 0;
    while (!loaded.empty()) {
        TileLoadInfo& info = loaded.front();
        // A load asked before the last clear is for a tile that is gone.
        if (info.generation != _generation) {
            loaded.pop_front();
            continue;
        }
        const bool texture = !info.pixels.empty() || !info.data.empty();
        if (texture && made >= kMaxTexturesPerFrame)
            break;
        if (texture)
            made++;
        if (createTileTexture(*info.tile, info)) {
            info.tile->copyrights = std::move(info.copyrights);
            _numTilesLoaded++;
        } else if (info.missing) {
            info.tile->retryFrame = INT_MAX;
        } else if (info.retryLater) {
            info.tile->retryFrame = _frame + kRetryFrames;
        } else if (info.cancelled) {
            info.tile->retryFrame = _frame + kCancelledRetryFrames;
        }
        info.tile->loading = false;
        loaded.pop_front();
    }
    // The rest next frame, ahead of what comes meanwhile.
    if (!loaded.empty()) {
        LightLock_Lock(&_lock);
        while (!_loaded.empty()) {
            loaded.push_back(std::move(_loaded.front()));
            _loaded.pop_front();
        }
        _loaded.swap(loaded);
        LightLock_Unlock(&_lock);
    }
}

void MapRenderer::releaseAllTextures()
{
    for (ZoomLevelInfo& level : _levels)
        for (auto& entry : level.tiles)
            gfx::destroyTexture(&entry.second.texture);
}

void MapRenderer::clear()
{
    Utils::verboseLog("%s line %d num tiles %d", "void MapRenderer::clear()", 0x438, _numTilesLoaded);
    LightLock_Lock(&_lock);
    _requests.clear();
    LightLock_Unlock(&_lock);
    if (_usedOnline)
        SatelliteTileLoader::getInstance()->cancel(this, false);
    _generation++;
    releaseAllTextures();
    _levels.clear();
    _numTilesLoaded = 0;
    _mapType = MapType::None;
    _mapId = 0;
    _viewReady = _viewTotal = 0;
}

MapRenderer::OnlineStats MapRenderer::getOnlineStats()
{
    LightLock_Lock(&_lock);
    const OnlineStats s = _onlineStats;
    LightLock_Unlock(&_lock);
    return s;
}

void MapRenderer::onTrimMemory()
{
    LightLock_Lock(&_lock);
    _trimMemory = true;
    LightLock_Unlock(&_lock);
}
