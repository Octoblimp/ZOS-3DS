#include "game/satellitetileloader.h"

#include <cstdio>
#include <sys/stat.h>

#include "debuglog.h"
#include "fileio.h"
#include "game/tilepack.h"
#include "net/http.h"
#include "net/jpeg.h"
#include "paths.h"
#include "workers.h"

namespace {

SatelliteTileLoader* s_instance = nullptr;
// Set at the app's exit: the threads touch nothing more, and end.
volatile bool s_quit = false;

struct ThreadArg {
    SatelliteTileLoader* loader;
    int index;
};

// TilePack's key, for the tiles kept in memory.
u64 key(int zoom, int x, int y)
{
    return ((u64)zoom << 48) | ((u64)(u32)x << 24) | (u64)(u32)y;
}

// A JPEG's memory cost beyond its bytes, so missing tiles count too.
const size_t kMemoryOverhead = 64;

// A tile's JPEG as RGB565, tiled. False if it is not a 256 x 256 JPEG.
bool decodeTile(const std::vector<u8>& data, std::vector<u16>& pixels, std::string& error)
{
    int w, h;
    if (!jpeg::decodeTiledRGB565(data.data(), data.size(), pixels, w, h, error))
        return false;
    if (w != SatelliteTileLoader::kTileSize || h != SatelliteTileLoader::kTileSize) {
        pixels.clear();
        error = "not 256 x 256";
        return false;
    }
    return true;
}

// 1.0.0 kept the imagery a file a tile under cache/imagery (an empty file a
// tile the imagery lacks). A map played or made with it has its tiles carried
// from there into its pack rather than downloaded again.
bool readLegacy(int zoom, int x, int y, std::vector<u8>& jpeg)
{
    // Looked for once: most cards never had it.
    static int s_present = -1;
    if (s_present < 0) {
        struct stat st;
        s_present = stat(ZOS_DATA_ROOT "/cache/imagery", &st) == 0 ? 1 : 0;
    }
    if (!s_present)
        return false;
    char path[96];
    snprintf(path, sizeof(path), ZOS_DATA_ROOT "/cache/imagery/%d/%d/%d.jpg", zoom, x, y);
    return readWholeFile(path, jpeg);
}

// Aborts a transfer when the app is closing, or its thread's job is dropped.
bool keepGoing(void* abort, size_t, size_t)
{
    return !s_quit && !*static_cast<volatile bool*>(abort);
}

} // namespace

SatelliteTileLoader* SatelliteTileLoader::getInstance()
{
    if (!s_instance)
        s_instance = new SatelliteTileLoader();
    return s_instance;
}

SatelliteTileLoader::SatelliteTileLoader()
{
    LightLock_Init(&_lock);
    CondVar_Init(&_cond);
    // The sockets and TLS, now, on the main thread.
    net::init();
    // On the workers' core (workers.h), below MapRenderer's: a TLS handshake
    // is seconds of CPU on an Old 3DS, and must not cost frames. 128 KB:
    // mbedTLS's handshake is deep.
    static ThreadArg args[kThreads];
    for (int i = 0; i < kThreads; i++) {
        args[i] = ThreadArg{this, i};
        _threads[i] = createWorkerThread(threadMain, &args[i], 128 * 1024, 2);
        if (!_threads[i])
            dbg("imagery: no download thread %d", i);
    }
}

bool SatelliteTileLoader::shutdown()
{
    SatelliteTileLoader* l = s_instance;
    if (!l)
        return true;
    LightLock_Lock(&l->_lock);
    s_quit = true;
    l->_queue.clear();
    l->_prefetch.clear();
    for (int i = 0; i < kThreads; i++)
        l->_abort[i] = true;
    CondVar_Broadcast(&l->_cond);
    LightLock_Unlock(&l->_lock);

    // A download gives up at curl's next progress call, within a second; a
    // name lookup cannot be cut short, and is waited for only a while.
    const u64 deadline = osGetTime() + 3000;
    bool stopped = true;
    for (int i = 0; i < kThreads; i++) {
        if (!l->_threads[i])
            continue;
        const u64 now = osGetTime();
        const u64 left = deadline > now ? deadline - now : 0;
        if (R_SUCCEEDED(threadJoin(l->_threads[i], left * 1000000ULL))) {
            threadFree(l->_threads[i]);
            l->_threads[i] = nullptr;
        } else {
            dbg("imagery: download thread %d still busy at exit", i);
            stopped = false;
        }
    }

    // The packs: the open map's, and any not yet written out. (One a stuck
    // thread is writing out is waited for inside close.)
    LightLock_Lock(&l->_lock);
    l->closeMapLocked(false);
    std::deque<Closing> closing;
    closing.swap(l->_closing);
    std::shared_ptr<TilePack> writing = l->_closingNow;
    LightLock_Unlock(&l->_lock);
    for (Closing& c : closing)
        finishPack(c.pack.get(), c.remove);
    if (writing)
        writing->close();
    return stopped;
}

std::string SatelliteTileLoader::url(int zoom, int x, int y)
{
    char u[160];
    snprintf(u, sizeof(u),
             "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/%d/%d/%d"
             "?blankTile=false",
             zoom, y, x);
    return u;
}

void SatelliteTileLoader::tileRange(int zoom, int worldX, int worldY, int width, int height, int& minX, int& minY,
                                    int& maxX, int& maxY)
{
    // A tile at this level is 2^(28 - zoom) zoom-20 world pixels across.
    const int shift = 28 - zoom;
    const int count = 1 << zoom;
    auto clampTo = [count](int v) { return v < 0 ? 0 : v >= count ? count - 1 : v; };
    minX = clampTo(worldX >> shift);
    minY = clampTo(worldY >> shift);
    maxX = clampTo((worldX + width - 1) >> shift);
    maxY = clampTo((worldY + height - 1) >> shift);
}

bool SatelliteTileLoader::overMap(int zoom, int x, int y) const
{
    if (!_pack || _mapW <= 0 || _mapH <= 0 || zoom < 1 || zoom > kMaxZoom)
        return false;
    int minX, minY, maxX, maxY;
    tileRange(zoom, _mapX, _mapY, _mapW, _mapH, minX, minY, maxX, maxY);
    return x >= minX && x <= maxX && y >= minY && y <= maxY;
}

SatelliteTileLoader::Cached SatelliteTileLoader::findJpeg(int zoom, int x, int y, std::vector<u8>& jpeg)
{
    jpeg.clear();
    LightLock_Lock(&_lock);
    std::shared_ptr<TilePack> pack = overMap(zoom, x, y) ? _pack : nullptr;
    bool found = false;
    auto it = _memory.find(key(zoom, x, y));
    if (it != _memory.end()) {
        jpeg = it->second;
        found = true;
    }
    LightLock_Unlock(&_lock);
    if (s_quit)
        return found ? (jpeg.empty() ? Cached::Missing : Cached::Tile) : Cached::None;
    if (pack) {
        if (!found && pack->get(zoom, x, y, jpeg))
            return jpeg.empty() ? Cached::Missing : Cached::Tile;
        // Browsed before the map was opened, or kept by 1.0.0: into the pack.
        if (!found)
            found = readLegacy(zoom, x, y, jpeg);
        if (found)
            pack->add(zoom, x, y, jpeg);
    }
    if (!found)
        return Cached::None;
    return jpeg.empty() ? Cached::Missing : Cached::Tile;
}

void SatelliteTileLoader::keep(int zoom, int x, int y, const std::vector<u8>& jpeg)
{
    LightLock_Lock(&_lock);
    std::shared_ptr<TilePack> pack = overMap(zoom, x, y) ? _pack : nullptr;
    if (!pack) {
        const u64 k = key(zoom, x, y);
        if (!_memory.count(k)) {
            _memory[k] = jpeg;
            _memoryOrder.push_back(k);
            _memoryUsed += jpeg.size() + kMemoryOverhead;
            while (_memoryUsed > kMemoryBytes && !_memoryOrder.empty()) {
                auto old = _memory.find(_memoryOrder.front());
                _memoryOrder.pop_front();
                if (old == _memory.end())
                    continue;
                _memoryUsed -= old->second.size() + kMemoryOverhead;
                _memory.erase(old);
            }
        }
    }
    LightLock_Unlock(&_lock);
    if (pack && !s_quit)
        pack->add(zoom, x, y, jpeg);
}

SatelliteTileLoader::Cached SatelliteTileLoader::loadCached(int zoom, int x, int y, std::vector<u16>& pixels)
{
    pixels.clear();
    std::vector<u8> jpegData;
    const Cached cached = findJpeg(zoom, x, y, jpegData);
    if (cached != Cached::Tile)
        return cached;
    std::string error;
    if (!decodeTile(jpegData, pixels, error)) {
        dbg("imagery: kept %d/%d/%d is bad (%s)", zoom, x, y, error.c_str());
        pixels.clear();
        return Cached::None;
    }
    return Cached::Tile;
}

SatelliteTileLoader::Status SatelliteTileLoader::fetchJpeg(void* client, int zoom, int x, int y,
                                                           std::vector<u8>& jpegData)
{
    switch (findJpeg(zoom, x, y, jpegData)) {
    case Cached::Tile:
        return Status::Loaded;
    case Cached::Missing:
        return Status::Missing;
    default:
        break;
    }
    if (s_quit)
        return Status::Failed;
    net::HttpClient& http = *static_cast<net::HttpClient*>(client);
    std::string error;
    const int status = http.get(url(zoom, x, y), jpegData, error);
    if (s_quit)
        return Status::Failed;
    if (status == 404) {
        jpegData.clear();
        keep(zoom, x, y, jpegData);
        return Status::Missing;
    }
    if (status != 200 || !jpeg::isJpeg(jpegData.data(), jpegData.size())) {
        if (status == 0)
            dbg("imagery: %d/%d/%d: %s", zoom, x, y, error.c_str());
        else
            dbg("imagery: %d/%d/%d: HTTP %d", zoom, x, y, status);
        jpegData.clear();
        return Status::Failed;
    }
    keep(zoom, x, y, jpegData);
    return Status::Loaded;
}

void SatelliteTileLoader::request(Client* client, const Request& request)
{
    std::vector<Job> dropped;
    LightLock_Lock(&_lock);
    if (s_quit) {
        LightLock_Unlock(&_lock);
        return;
    }
    _queue.push_back(Job{client, request, 0});
    while (_queue.size() > kMaxQueued) {
        dropped.push_back(_queue.front());
        _queue.pop_front();
    }
    CondVar_Signal(&_cond);
    LightLock_Unlock(&_lock);
    for (Job& j : dropped) {
        Answer a;
        a.request = j.request;
        a.status = Status::Cancelled;
        j.client->onTileDownloaded(std::move(a));
    }
}

void SatelliteTileLoader::cancel(Client* client, bool wait)
{
    LightLock_Lock(&_lock);
    for (auto it = _queue.begin(); it != _queue.end();) {
        if (it->client == client)
            it = _queue.erase(it);
        else
            ++it;
    }
    // Its downloads give up at curl's next progress call (within a second),
    // rather than run to their end and keep the threads from the next view's.
    for (int i = 0; i < kThreads; i++)
        if (_busy[i] == client)
            _abort[i] = true;
    // (After the exit's shutdown no answer comes: there is nothing to wait
    // for, and a thread stuck in a name lookup must not hold the exit up.)
    while (wait && !s_quit) {
        bool busy = false;
        for (Client* c : _busy)
            busy = busy || c == client;
        if (!busy)
            break;
        CondVar_Wait(&_cond, &_lock);
    }
    LightLock_Unlock(&_lock);
}

void SatelliteTileLoader::openMap(const std::string& packPath, int worldX, int worldY, int width, int height)
{
    LightLock_Lock(&_lock);
    if (_pack && _pack->path() == packPath) {
        _mapX = worldX;
        _mapY = worldY;
        _mapW = width;
        _mapH = height;
        LightLock_Unlock(&_lock);
        return;
    }
    closeMapLocked(false);
    // The same map closed a moment ago (played again): its pack, taken back
    // before it is written out, or waited for while it is.
    std::shared_ptr<TilePack> pack;
    for (auto it = _closing.begin(); it != _closing.end(); ++it) {
        if (it->pack->path() == packPath) {
            pack = it->pack;
            _closing.erase(it);
            break;
        }
    }
    while (!pack && _closingNow && _closingNow->path() == packPath)
        CondVar_Wait(&_cond, &_lock);
    LightLock_Unlock(&_lock);
    // Its index: one small file.
    if (!pack && !s_quit)
        pack.reset(TilePack::open(packPath));
    LightLock_Lock(&_lock);
    _pack = pack;
    _mapX = worldX;
    _mapY = worldY;
    _mapW = width;
    _mapH = height;
    LightLock_Unlock(&_lock);
    dbg("imagery: map open, %d tiles kept", pack ? pack->count() : 0);
}

void SatelliteTileLoader::prefetchMap(bool background)
{
    LightLock_Lock(&_lock);
    std::shared_ptr<TilePack> pack = _pack;
    const int worldX = _mapX, worldY = _mapY, width = _mapW, height = _mapH;
    LightLock_Unlock(&_lock);
    if (!pack)
        return;
    // The level the game opens on (19 at its usual zoom) and outwards, then
    // 20, which the imagery has in fewer places; each tile the pack lacks.
    std::deque<Job> jobs;
    int kept = 0;
    for (int i = 0; i <= kMaxZoom - 1; i++) {
        const int zoom = i == kMaxZoom - 1 ? kMaxZoom : kMaxZoom - 1 - i;
        if (zoom < 1)
            continue;
        int minX, minY, maxX, maxY;
        tileRange(zoom, worldX, worldY, width, height, minX, minY, maxX, maxY);
        for (int y = minY; y <= maxY; y++) {
            for (int x = minX; x <= maxX; x++) {
                if (pack->has(zoom, x, y)) {
                    kept++;
                    continue;
                }
                Job j{nullptr, Request(), 0};
                j.request.zoom = zoom;
                j.request.x = x;
                j.request.y = y;
                jobs.push_back(j);
            }
        }
    }
    LightLock_Lock(&_lock);
    if (_pack != pack) {
        LightLock_Unlock(&_lock);
        return;
    }
    _prefetch.swap(jobs);
    _prefetchBackground = background;
    _prefetchTotal = (int)_prefetch.size();
    _prefetchDone = 0;
    _prefetchGeneration++;
    const int total = _prefetchTotal;
    CondVar_Broadcast(&_cond);
    LightLock_Unlock(&_lock);
    dbg("imagery: map has %d tiles, %d to download%s", kept, total, background ? " in the background" : "");
}

void SatelliteTileLoader::prefetchProgress(int& done, int& total)
{
    LightLock_Lock(&_lock);
    done = _prefetchDone;
    total = _prefetchTotal;
    LightLock_Unlock(&_lock);
}

void SatelliteTileLoader::flushMap()
{
    LightLock_Lock(&_lock);
    std::shared_ptr<TilePack> pack = _pack;
    LightLock_Unlock(&_lock);
    if (pack && !s_quit)
        pack->flush();
}

void SatelliteTileLoader::closeMap(bool removePack)
{
    LightLock_Lock(&_lock);
    closeMapLocked(removePack);
    finishClosingLocked();
    LightLock_Unlock(&_lock);
}

void SatelliteTileLoader::removePack(const std::string& packPath)
{
    LightLock_Lock(&_lock);
    if (_pack && _pack->path() == packPath)
        closeMapLocked(true);
    bool queued = false;
    for (Closing& c : _closing) {
        if (c.pack->path() == packPath) {
            c.remove = true;
            queued = true;
        }
    }
    while (!queued && _closingNow && _closingNow->path() == packPath)
        CondVar_Wait(&_cond, &_lock);
    finishClosingLocked();
    LightLock_Unlock(&_lock);
    if (!queued)
        TilePack::remove(packPath);
}

void SatelliteTileLoader::finishClosingLocked()
{
    if (_threads[0] || _threads[1])
        return;
    while (!_closing.empty()) {
        finishPack(_closing.front().pack.get(), _closing.front().remove);
        _closing.pop_front();
    }
}

void SatelliteTileLoader::closeMapLocked(bool removePack)
{
    if (_pack)
        _closing.push_back(Closing{_pack, removePack});
    _pack.reset();
    _mapX = _mapY = _mapW = _mapH = 0;
    _prefetch.clear();
    _prefetchTotal = _prefetchDone = 0;
    _prefetchGeneration++;
    // Its tiles downloading now are abandoned too.
    for (int i = 0; i < kThreads; i++)
        if (_onPrefetch[i])
            _abort[i] = true;
    CondVar_Broadcast(&_cond);
}

void SatelliteTileLoader::finishPack(TilePack* pack, bool removePack)
{
    pack->close();
    if (removePack)
        TilePack::remove(pack->path());
}

void SatelliteTileLoader::threadMain(void* arg)
{
    const ThreadArg* a = static_cast<const ThreadArg*>(arg);
    a->loader->run(a->index);
}

void SatelliteTileLoader::run(int self)
{
    net::HttpClient http;
    http.setTimeout(30);
    http.setMaxBytes(512 * 1024);
    http.setProgress(keepGoing, const_cast<bool*>(&_abort[self]));
    for (;;) {
        Job job{nullptr, Request(), 0};
        Closing closing{nullptr, false};
        bool prefetch = false;
        bool inBackground = false;
        LightLock_Lock(&_lock);
        for (;;) {
            if (s_quit) {
                LightLock_Unlock(&_lock);
                return;
            }
            // A closed map's pack to write out, one at a time.
            if (!_closing.empty() && !_closingNow) {
                closing = _closing.front();
                _closing.pop_front();
                _closingNow = closing.pack;
                break;
            }
            const u64 now = osGetTime();
            if ((!_queue.empty() || !_prefetch.empty()) && now < _pauseUntil) {
                CondVar_WaitTimeout(&_cond, &_lock, (s64)(_pauseUntil - now) * 1000 * 1000);
                continue;
            }
            // What is on screen first, the newest first; then the prefetch:
            // made, every thread as fast as it goes; played, fewer. With the
            // threads on a core of their own, kBackgroundThreads of them;
            // in the main thread's gaps, one, resting between tiles.
            if (!_queue.empty()) {
                job = _queue.back();
                _queue.pop_back();
                break;
            }
            const bool gentle = !workersOffMainCore();
            const int background = gentle ? 1 : kBackgroundThreads;
            if (!_prefetch.empty() && (!_prefetchBackground || _prefetching < background)) {
                if (!_prefetchBackground || !gentle || now >= _nextPrefetch) {
                    job = _prefetch.front();
                    _prefetch.pop_front();
                    prefetch = true;
                    inBackground = _prefetchBackground;
                    if (inBackground)
                        _prefetching++;
                    break;
                }
                CondVar_WaitTimeout(&_cond, &_lock, (s64)(_nextPrefetch - now) * 1000 * 1000);
                continue;
            }
            CondVar_Wait(&_cond, &_lock);
        }

        if (closing.pack) {
            LightLock_Unlock(&_lock);
            finishPack(closing.pack.get(), closing.remove);
            LightLock_Lock(&_lock);
            _closingNow.reset();
            CondVar_Broadcast(&_cond);
            LightLock_Unlock(&_lock);
            continue;
        }

        _busy[self] = job.client;
        _onPrefetch[self] = prefetch;
        _jobGeneration[self] = _prefetchGeneration;
        _abort[self] = false;
        LightLock_Unlock(&_lock);

        const Request& r = job.request;
        bool retryLater = false;
        bool fetched = false;
        Status status = Status::Failed;
        Answer a;
        a.request = r;
        // The other thread, the prefetch or a client may have it since.
        std::vector<u8> jpegData;
        switch (findJpeg(r.zoom, r.x, r.y, jpegData)) {
        case Cached::Tile:
            status = Status::Loaded;
            if (job.client) {
                std::string error;
                if (!decodeTile(jpegData, a.pixels, error)) {
                    dbg("imagery: kept %d/%d/%d is bad (%s)", r.zoom, r.x, r.y, error.c_str());
                    a.pixels.clear();
                    status = Status::Failed;
                }
            }
            break;
        case Cached::Missing:
            status = Status::Missing;
            break;
        default:
            if (!s_quit) {
                status = download(&http, r, job.client ? &a.pixels : nullptr, retryLater, &_abort[self]);
                fetched = true;
            }
            break;
        }
        // (No answer after the exit's shutdown: the client may be gone.)
        if (job.client && !s_quit) {
            a.status = status;
            job.client->onTileDownloaded(std::move(a));
        }

        LightLock_Lock(&_lock);
        const bool abandoned = _abort[self];
        _busy[self] = nullptr;
        _onPrefetch[self] = false;
        if (inBackground) {
            _prefetching--;
            _nextPrefetch = osGetTime() + kPrefetchRest;
        }
        if (fetched && !abandoned) {
            if (status == Status::Failed)
                _failed++;
            else
                _downloaded++;
        }
        if (prefetch && _jobGeneration[self] == _prefetchGeneration) {
            if (status == Status::Failed && retryLater && !abandoned && ++job.attempts < kPrefetchAttempts)
                _prefetch.push_back(job);
            else
                _prefetchDone++;
            if (_prefetchDone == _prefetchTotal)
                dbg("imagery: map downloaded, %d downloaded, %d failed in all", _downloaded, _failed);
        }
        // (An abandoned download is no sign of the network's trouble.)
        if (retryLater && !abandoned)
            _pauseUntil = osGetTime() + 5000;
        _abort[self] = false;
        CondVar_Broadcast(&_cond);
        LightLock_Unlock(&_lock);
    }
}

SatelliteTileLoader::Status SatelliteTileLoader::download(void* client, const Request& r, std::vector<u16>* pixels,
                                                          bool& retryLater, const volatile bool* abort)
{
    net::HttpClient& http = *static_cast<net::HttpClient*>(client);
    const std::string u = url(r.zoom, r.x, r.y);
    std::vector<u8> body;
    std::string error;
    const int status = http.get(u, body, error);
    if (s_quit || (abort && *abort))
        return Status::Failed;
    if (status == 404) {
        // No imagery at this level here: remembered, never asked again.
        keep(r.zoom, r.x, r.y, std::vector<u8>());
        return Status::Missing;
    }
    if (status != 200) {
        // No answer, too many requests, or the server's trouble: wait a while.
        retryLater = status == 0 || status == 429 || status >= 500;
        if (_failed < 20 || _failed % 100 == 0) {
            if (status == 0)
                dbg("imagery: %d/%d/%d: %s", r.zoom, r.x, r.y, error.c_str());
            else
                dbg("imagery: %d/%d/%d: HTTP %d", r.zoom, r.x, r.y, status);
        }
        return Status::Failed;
    }
    // A prefetched tile is only checked for being a JPEG: decoding it is
    // left to when it is drawn.
    if (!jpeg::isJpeg(body.data(), body.size())) {
        dbg("imagery: %d/%d/%d: not a JPEG", r.zoom, r.x, r.y);
        return Status::Failed;
    }
    if (pixels && !decodeTile(body, *pixels, error)) {
        dbg("imagery: %d/%d/%d: %s", r.zoom, r.x, r.y, error.c_str());
        return Status::Failed;
    }
    keep(r.zoom, r.x, r.y, body);
    return Status::Loaded;
}
