#include "game/playermaps.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <zlib.h>

#include "debuglog.h"
#include "game/gameoptions.h"
#include "game/osmcollision.h"
#include "game/satellitetileloader.h"
#include "net/http.h"
#include "net/jpeg.h"
#include "paths.h"
#include "workers.h"

namespace {

// Overpass's public instances, the main one first. The others take over when
// it is busy (429, 50x) or does not answer.
const char* const kOverpass[] = {
    "https://overpass-api.de/api/interpreter",
    "https://overpass.private.coffee/api/interpreter",
    "https://maps.mail.ru/osm/tools/overpass/api/interpreter",
};

// A dense old town's 4000-pixel square is about 600 KB of JSON; this is
// room for far worse, inside the 24 MB heap.
const size_t kMaxJson = 8 * 1024 * 1024;
// The query's own timeout is 90 s; the transfer after it.
const long kOverpassTimeout = 150;
// The imagery level a pixel of which is a cell.
const int kImageryZoom = 18;
// Less open ground than this is no map (the sea, a lake, a roof).
const float kMinOpenShare = 0.1f;

std::string httpError(const char* what, int status, const std::string& error)
{
    char buf[160];
    if (status == 0)
        snprintf(buf, sizeof(buf), "%s did not answer:\n%s", what, error.c_str());
    else
        snprintf(buf, sizeof(buf), "%s answered HTTP %d", what, status);
    return buf;
}

} // namespace

namespace {
PlayerMaps* s_instance = nullptr;
} // namespace

PlayerMaps* PlayerMaps::getInstance()
{
    if (!s_instance)
        s_instance = new PlayerMaps();
    return s_instance;
}

bool PlayerMaps::shutdown()
{
    PlayerMaps* m = s_instance;
    if (!m || !m->_thread)
        return true;
    LightLock_Lock(&m->_lock);
    m->_quit = true;
    m->_generation++;
    m->_pending = false;
    CondVar_Broadcast(&m->_cond);
    LightLock_Unlock(&m->_lock);
    // A transfer stops at curl's next progress call; drawing a big map's
    // layer takes a few seconds, and is not waited for to the end.
    if (R_FAILED(threadJoin(m->_thread, 3000ULL * 1000 * 1000))) {
        dbg("player maps: the builder still busy at exit");
        return false;
    }
    threadFree(m->_thread);
    m->_thread = nullptr;
    return true;
}

PlayerMaps::PlayerMaps()
{
    LightLock_Init(&_lock);
    LightLock_Init(&_fileLock);
    CondVar_Init(&_cond);
    // The imagery's cache, and the sockets and TLS, on the main thread.
    SatelliteTileLoader::getInstance();
    loadIndex();
    // On the workers' core, like the imagery's; 256 KB for mbedTLS's
    // handshake and the builder's own. Joined at the exit.
    _thread = createWorkerThread(threadMain, this, 256 * 1024, 2);
    if (!_thread)
        dbg("player maps: no builder thread");
}

std::string PlayerMaps::dir()
{
    return ZOS_DATA_ROOT "/save/playermaps";
}

std::string PlayerMaps::layerPath(int id)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "/%d.cl", id);
    return dir() + buf;
}

std::string PlayerMaps::packPath(int id)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "/%d.tiles", id);
    return dir() + buf;
}

void PlayerMaps::loadIndex()
{
    FILE* f = fopen((dir() + "/index.txt").c_str(), "r");
    if (!f)
        return;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        Map m;
        if (sscanf(line, "%d %d %d %d", &m.id, &m.worldX, &m.worldY, &m.size) != 4 || m.id <= 0 || m.size <= 0)
            continue;
        if (m.id >= _nextId)
            _nextId = m.id + 1;
        struct stat st;
        if (stat(layerPath(m.id).c_str(), &st) != 0)
            continue;
        _maps.push_back(m);
    }
    fclose(f);
    dbg("player maps: %d on the card", (int)_maps.size());
}

const PlayerMaps::Map* PlayerMaps::getMap(int id) const
{
    for (const Map& m : _maps)
        if (m.id == id)
            return &m;
    return nullptr;
}

void PlayerMaps::fillConfig(const Map& map, MapConfig& config) const
{
    config = MapConfig();
    config.id = map.id;
    config.type = MapConfig::MapType::Player;
    config.worldX = map.worldX;
    config.worldY = map.worldY;
    config.width = map.size;
    config.height = map.size;
    config.startX = map.size / 2;
    config.startY = map.size / 2;
    config.zombieOutbreak = GameOptions::ZombieOutbreakNorth;
    config.free = true;
    config.available = true;
    config.playerPath = layerPath(map.id);
}

bool PlayerMaps::deleteMap(int id)
{
    for (auto it = _maps.begin(); it != _maps.end(); ++it) {
        if (it->id == id) {
            _maps.erase(it);
            break;
        }
    }
    // The index without its line, then the layer: a line there always has
    // its file.
    LightLock_Lock(&_fileLock);
    const std::string index = dir() + "/index.txt";
    std::string kept;
    if (FILE* in = fopen(index.c_str(), "r")) {
        char line[128];
        int lineId = 0;
        while (fgets(line, sizeof(line), in))
            if (sscanf(line, "%d", &lineId) != 1 || lineId != id)
                kept += line;
        fclose(in);
    }
    const std::string tmp = index + ".part";
    FILE* f = fopen(tmp.c_str(), "w");
    bool ok = f && fwrite(kept.data(), 1, kept.size(), f) == kept.size();
    if (f && fclose(f) != 0)
        ok = false;
    if (ok) {
        remove(index.c_str());
        ok = rename(tmp.c_str(), index.c_str()) == 0;
    } else {
        remove(tmp.c_str());
    }
    LightLock_Unlock(&_fileLock);
    if (ok) {
        remove(layerPath(id).c_str());
        SatelliteTileLoader::getInstance()->removePack(packPath(id));
    }
    dbg("player map %d deleted%s", id, ok ? "" : ", but the index could not be written");
    return ok;
}

void PlayerMaps::build(int centreX, int centreY, int size)
{
    Map m;
    m.size = size;
    // The corner on the cell grid (4 world pixels), inside the world.
    const int world = 1 << 28;
    m.worldX = (centreX - size / 2) & ~3;
    m.worldY = (centreY - size / 2) & ~3;
    if (m.worldX < 0)
        m.worldX = 0;
    if (m.worldY < 0)
        m.worldY = 0;
    if (m.worldX > world - size)
        m.worldX = world - size;
    if (m.worldY > world - size)
        m.worldY = world - size;
    closeBuildImagery();
    // Its imagery, every level, downloading from now on, beside the builder.
    m.id = _nextId++;
    _imageryId = m.id;
    SatelliteTileLoader* loader = SatelliteTileLoader::getInstance();
    loader->openMap(packPath(m.id), m.worldX, m.worldY, m.size, m.size);
    loader->prefetchMap(false);
    LightLock_Lock(&_lock);
    _generation++;
    _job = m;
    _pending = true;
    _state = State::Working;
    _text = "Starting";
    _error.clear();
    CondVar_Signal(&_cond);
    LightLock_Unlock(&_lock);
}

void PlayerMaps::cancel()
{
    LightLock_Lock(&_lock);
    _generation++;
    _pending = false;
    _state = State::Idle;
    LightLock_Unlock(&_lock);
    closeBuildImagery();
}

void PlayerMaps::closeBuildImagery()
{
    if (_imageryId) {
        SatelliteTileLoader::getInstance()->closeMap(true);
        _imageryId = 0;
    }
}

void PlayerMaps::acknowledge()
{
    LightLock_Lock(&_lock);
    if (_state == State::Done || _state == State::Failed)
        _state = State::Idle;
    LightLock_Unlock(&_lock);
}

void PlayerMaps::update()
{
    LightLock_Lock(&_lock);
    for (const Map& m : _finished)
        _maps.push_back(m);
    _finished.clear();
    LightLock_Unlock(&_lock);
}

PlayerMaps::State PlayerMaps::poll(std::string& text, int& mapId)
{
    update();
    LightLock_Lock(&_lock);
    const State state = _state;
    text = state == State::Failed ? _error : state == State::Working ? _text : std::string();
    mapId = _doneId;
    LightLock_Unlock(&_lock);
    // A map made keeps its imagery open, to be played now; one not made has
    // it removed.
    if (state == State::Done && mapId == _imageryId)
        _imageryId = 0;
    else if (state != State::Working)
        closeBuildImagery();
    return state;
}

bool PlayerMaps::cancelled(int generation)
{
    LightLock_Lock(&_lock);
    const bool c = generation != _generation;
    LightLock_Unlock(&_lock);
    return c;
}

void PlayerMaps::setText(int generation, const std::string& text)
{
    LightLock_Lock(&_lock);
    if (generation == _generation)
        _text = text;
    LightLock_Unlock(&_lock);
}

bool PlayerMaps::onProgress(void* user, size_t now, size_t)
{
    PlayerMaps* self = static_cast<PlayerMaps*>(user);
    LightLock_Lock(&self->_lock);
    const bool go = self->_jobGeneration == self->_generation;
    const size_t kb = now / 1024;
    if (go && self->_countBytes && kb >= self->_shownKB + 16) {
        char buf[48];
        snprintf(buf, sizeof(buf), "Data %u KB", (unsigned)kb);
        self->_text = buf;
        self->_shownKB = kb;
    }
    LightLock_Unlock(&self->_lock);
    return go;
}

void PlayerMaps::threadMain(void* arg)
{
    static_cast<PlayerMaps*>(arg)->run();
}

void PlayerMaps::run()
{
    for (;;) {
        LightLock_Lock(&_lock);
        while (!_pending && !_quit)
            CondVar_Wait(&_cond, &_lock);
        if (_quit) {
            LightLock_Unlock(&_lock);
            return;
        }
        _pending = false;
        const Map job = _job;
        const int generation = _generation;
        _jobGeneration = generation;
        LightLock_Unlock(&_lock);

        const u64 t0 = osGetTime();
        std::string error;
        const bool ok = make(generation, job, error);
        // (Not where: the log may be shared, and a map is often of home.)
        dbg("player map %d, size %d: %s in %d ms", job.id, job.size,
            ok ? "made" : error.empty() ? "cancelled" : error.c_str(), (int)(osGetTime() - t0));

        LightLock_Lock(&_lock);
        if (ok)
            _finished.push_back(job);
        if (generation == _generation) {
            if (ok) {
                _state = State::Done;
                _doneId = job.id;
            } else {
                _state = State::Failed;
                _error = error;
            }
        }
        LightLock_Unlock(&_lock);
    }
}

bool PlayerMaps::make(int generation, const Map& map, std::string& error)
{
    osm::Area area;
    area.worldX = map.worldX;
    area.worldY = map.worldY;
    area.width = map.size;
    area.height = map.size;

    // 1. The area's features from Overpass.
    const std::string form = "data=" + net::urlEncode(osm::overpassQuery(area));
    std::vector<u8> json;
    int status = 0;
    std::string httpErr;
    {
        net::HttpClient http;
        http.setTimeout(kOverpassTimeout);
        http.setMaxBytes(kMaxJson);
        http.setProgress(onProgress, this);
        for (const char* url : kOverpass) {
            setText(generation, "Asking OSM");
            LightLock_Lock(&_lock);
            _shownKB = 0;
            _countBytes = true;
            LightLock_Unlock(&_lock);
            status = http.post(url, form, json, httpErr);
            LightLock_Lock(&_lock);
            _countBytes = false;
            LightLock_Unlock(&_lock);
            if (cancelled(generation))
                return false;
            if (status == 200)
                break;
            dbg("player maps: %s: %s", url, httpError("Overpass", status, httpErr).c_str());
            // A bad query is bad everywhere; a busy or silent server is not.
            if (status != 0 && status != 429 && status < 500)
                break;
        }
    }
    if (status != 200) {
        error = httpError("OpenStreetMap", status, httpErr);
        return false;
    }

    // 2. The collision layer.
    setText(generation, "Drawing map");
    std::vector<u8> cells;
    if (!osm::buildCollisionLayer(area, reinterpret_cast<const char*>(json.data()), json.size(), cells, error))
        return false;
    std::vector<u8>().swap(json);
    // Nothing mapped (the open sea, or a place no one has mapped yet): every
    // cell open ground, nothing to hide behind.
    if (std::all_of(cells.begin(), cells.end(), [](u8 v) { return v == 0x03; })) {
        error = "OpenStreetMap has nothing mapped here:\npick a place with buildings";
        return false;
    }
    if (cancelled(generation))
        return false;

    // 3. The trees the imagery shows: its zoom-18 tiles over the area, a
    // pixel a cell. A tile that will not come leaves its cells as they are.
    const int cellsWide = map.size / 4;
    const int px0 = map.worldX / 4;
    const int py0 = map.worldY / 4;
    const int ts = SatelliteTileLoader::kTileSize;
    const int tx0 = px0 / ts, ty0 = py0 / ts;
    const int tx1 = (px0 + cellsWide - 1) / ts, ty1 = (py0 + cellsWide - 1) / ts;
    const int numTiles = (tx1 - tx0 + 1) * (ty1 - ty0 + 1);
    std::vector<u8> rgba((size_t)cellsWide * (size_t)cellsWide * 4, 0);
    {
        net::HttpClient http;
        http.setTimeout(30);
        http.setProgress(onProgress, this);
        int n = 0, missing = 0;
        std::vector<u8> jpg, pixels;
        for (int ty = ty0; ty <= ty1; ty++) {
            for (int tx = tx0; tx <= tx1; tx++) {
                char buf[48];
                snprintf(buf, sizeof(buf), "Imagery %d/%d", ++n, numTiles);
                setText(generation, buf);
                SatelliteTileLoader::Status s =
                    SatelliteTileLoader::getInstance()->fetchJpeg(&http, kImageryZoom, tx, ty, jpg);
                if (cancelled(generation))
                    return false;
                int w = 0, h = 0;
                std::string jpegError;
                if (s != SatelliteTileLoader::Status::Loaded ||
                    !jpeg::decode(jpg.data(), jpg.size(), pixels, w, h, jpegError) || w != ts || h != ts) {
                    missing++;
                    continue;
                }
                // The tile's overlap with the area.
                const int ox = tx * ts - px0, oy = ty * ts - py0;
                const int x0 = ox < 0 ? 0 : ox, y0 = oy < 0 ? 0 : oy;
                const int x1 = ox + ts > cellsWide ? cellsWide : ox + ts;
                const int y1 = oy + ts > cellsWide ? cellsWide : oy + ts;
                for (int y = y0; y < y1; y++)
                    memcpy(&rgba[((size_t)y * cellsWide + x0) * 4], &pixels[((size_t)(y - oy) * ts + (x0 - ox)) * 4],
                           (size_t)(x1 - x0) * 4);
            }
        }
        if (missing)
            dbg("player maps: %d of %d imagery tiles missing", missing, numTiles);
    }
    osm::addImageryCover(rgba, cellsWide, cellsWide, cells);
    std::vector<u8>().swap(rgba);

    const float open = osm::openShare(cells);
    if (open < kMinOpenShare) {
        char buf[96];
        snprintf(buf, sizeof(buf), "Only %d%% of this area is open ground:\npick one with more land", (int)(open * 100));
        error = buf;
        return false;
    }

    // 4. The rest of the imagery, which the loader's threads have been
    // downloading meanwhile: the map then plays with nothing more to fetch.
    // (A tile the network keeps failing is given up, and fetched in play.)
    SatelliteTileLoader* loader = SatelliteTileLoader::getInstance();
    for (;;) {
        int done = 0, total = 0;
        loader->prefetchProgress(done, total);
        if (done >= total)
            break;
        char buf[48];
        snprintf(buf, sizeof(buf), "Imagery %d%%", done * 100 / total);
        setText(generation, buf);
        if (cancelled(generation))
            return false;
        svcSleepThread(100 * 1000 * 1000);
    }
    loader->flushMap();

    // 5. Onto the card.
    setText(generation, "Saving");
    if (cancelled(generation))
        return false;
    return save(map, cells, error);
}

bool PlayerMaps::save(const Map& map, const std::vector<u8>& cells, std::string& error)
{
    uLongf packedSize = compressBound((uLong)cells.size());
    std::vector<u8> packed(packedSize);
    if (compress2(packed.data(), &packedSize, cells.data(), (uLong)cells.size(), 9) != Z_OK) {
        error = "Could not pack the map";
        return false;
    }
    mkdir(ZOS_DATA_ROOT, 0777);
    mkdir(ZOS_DATA_ROOT "/save", 0777);
    mkdir(dir().c_str(), 0777);
    const std::string path = layerPath(map.id);
    const std::string tmp = path + ".part";
    FILE* f = fopen(tmp.c_str(), "wb");
    bool ok = f && fwrite(packed.data(), 1, packedSize, f) == packedSize;
    if (f && fclose(f) != 0)
        ok = false;
    if (ok) {
        remove(path.c_str());
        ok = rename(tmp.c_str(), path.c_str()) == 0;
    }
    if (!ok) {
        remove(tmp.c_str());
        error = "Could not save the map on the SD card";
        return false;
    }
    // The index after the layer: a line there always has its file.
    LightLock_Lock(&_fileLock);
    f = fopen((dir() + "/index.txt").c_str(), "a");
    ok = f && fprintf(f, "%d %d %d %d\n", map.id, map.worldX, map.worldY, map.size) > 0;
    if (f && fclose(f) != 0)
        ok = false;
    LightLock_Unlock(&_fileLock);
    if (!ok) {
        remove(path.c_str());
        error = "Could not save the map on the SD card";
        return false;
    }
    return true;
}
