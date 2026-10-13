#include "game/satellitetileloader.h"

#include <cstdio>
#include <set>
#include <sys/stat.h>

#include "debuglog.h"
#include "fileio.h"
#include "net/http.h"
#include "net/jpeg.h"
#include "paths.h"

namespace {

volatile bool s_quit = false;

LightLock s_dirLock;
std::set<std::string> s_dirs; // made already

void makeDir(const std::string& dir)
{
    LightLock_Lock(&s_dirLock);
    if (s_dirs.insert(dir).second)
        mkdir(dir.c_str(), 0777);
    LightLock_Unlock(&s_dirLock);
}

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

// What the cache has of a tile, without reading it.
SatelliteTileLoader::Cached cacheState(int zoom, int x, int y)
{
    struct stat st;
    if (stat(SatelliteTileLoader::cachePath(zoom, x, y).c_str(), &st) != 0)
        return SatelliteTileLoader::Cached::None;
    return st.st_size == 0 ? SatelliteTileLoader::Cached::Missing : SatelliteTileLoader::Cached::Tile;
}

// Into the cache: written aside, then renamed, so a power cut never leaves
// half a tile under the real name. An empty file marks a missing tile.
void writeCache(int zoom, int x, int y, const std::vector<u8>& body)
{
    char dir[80];
    snprintf(dir, sizeof(dir), ZOS_DATA_ROOT "/cache");
    makeDir(dir);
    snprintf(dir, sizeof(dir), ZOS_DATA_ROOT "/cache/imagery");
    makeDir(dir);
    snprintf(dir, sizeof(dir), ZOS_DATA_ROOT "/cache/imagery/%d", zoom);
    makeDir(dir);
    snprintf(dir, sizeof(dir), ZOS_DATA_ROOT "/cache/imagery/%d/%d", zoom, x);
    makeDir(dir);
    const std::string path = SatelliteTileLoader::cachePath(zoom, x, y);
    const std::string tmp = path + ".part";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f)
        return;
    const bool ok = body.empty() || fwrite(body.data(), 1, body.size(), f) == body.size();
    fclose(f);
    if (!ok) {
        remove(tmp.c_str());
        return;
    }
    // The SD card's rename will not replace a file: one is there only if the
    // other thread fetched it too.
    if (rename(tmp.c_str(), path.c_str()) != 0) {
        remove(path.c_str());
        if (rename(tmp.c_str(), path.c_str()) != 0)
            remove(tmp.c_str());
    }
}

// Aborts a transfer when the app is closing, or its client going.
bool keepGoing(void* abort, size_t, size_t)
{
    return !s_quit && !*static_cast<volatile bool*>(abort);
}

} // namespace

SatelliteTileLoader* SatelliteTileLoader::getInstance()
{
    static SatelliteTileLoader* s = new SatelliteTileLoader();
    return s;
}

SatelliteTileLoader::SatelliteTileLoader()
{
    LightLock_Init(&_lock);
    LightLock_Init(&s_dirLock);
    CondVar_Init(&_cond);
    // The sockets and TLS, now, on the main thread.
    net::init();
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    // Below the main thread: a TLS handshake is seconds of CPU on an Old
    // 3DS, and must not cost frames. 128 KB: mbedTLS's handshake is deep.
    for (int i = 0; i < kThreads; i++) {
        _threads[i] = threadCreate(threadMain, this, 128 * 1024, prio + 2, -2, false);
        if (!_threads[i])
            dbg("imagery: no download thread %d", i);
    }
}

std::string SatelliteTileLoader::cachePath(int zoom, int x, int y)
{
    char path[96];
    snprintf(path, sizeof(path), ZOS_DATA_ROOT "/cache/imagery/%d/%d/%d.jpg", zoom, x, y);
    return path;
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

SatelliteTileLoader::Cached SatelliteTileLoader::loadCached(int zoom, int x, int y, std::vector<u16>& pixels)
{
    pixels.clear();
    const std::string path = cachePath(zoom, x, y);
    std::vector<u8> data;
    if (!readWholeFile(path.c_str(), data))
        return Cached::None;
    if (data.empty())
        return Cached::Missing;
    std::string error;
    if (!decodeTile(data, pixels, error)) {
        dbg("imagery: cached %d/%d/%d is bad (%s), removed", zoom, x, y, error.c_str());
        remove(path.c_str());
        pixels.clear();
        return Cached::None;
    }
    return Cached::Tile;
}

SatelliteTileLoader::Status SatelliteTileLoader::fetchJpeg(void* client, int zoom, int x, int y,
                                                           std::vector<u8>& jpegData)
{
    getInstance(); // the cache's directory lock
    if (readWholeFile(cachePath(zoom, x, y).c_str(), jpegData))
        return jpegData.empty() ? Status::Missing : Status::Loaded;
    net::HttpClient& http = *static_cast<net::HttpClient*>(client);
    std::string error;
    const int status = http.get(url(zoom, x, y), jpegData, error);
    if (status == 404) {
        jpegData.clear();
        writeCache(zoom, x, y, jpegData);
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
    writeCache(zoom, x, y, jpegData);
    return Status::Loaded;
}

void SatelliteTileLoader::request(Client* client, const Request& request)
{
    std::vector<Job> dropped;
    LightLock_Lock(&_lock);
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
    if (wait) {
        // Its downloads give up at curl's next progress call (within a
        // second), rather than run to their end.
        for (int i = 0; i < kThreads; i++)
            if (_busy[i] == client)
                _abort[i] = true;
        for (;;) {
            bool busy = false;
            for (Client* c : _busy)
                busy = busy || c == client;
            if (!busy)
                break;
            CondVar_Wait(&_cond, &_lock);
        }
    }
    LightLock_Unlock(&_lock);
}

void SatelliteTileLoader::prefetch(int worldX, int worldY, int width, int height)
{
    // The level the game opens on (19 at its usual zoom) and outwards, then
    // 20, which the imagery has in fewer places.
    std::deque<Job> jobs;
    for (int i = 0; i <= kMaxZoom - 1; i++) {
        const int zoom = i == kMaxZoom - 1 ? kMaxZoom : kMaxZoom - 1 - i;
        if (zoom < 1)
            continue;
        int minX, minY, maxX, maxY;
        tileRange(zoom, worldX, worldY, width, height, minX, minY, maxX, maxY);
        for (int y = minY; y <= maxY; y++) {
            for (int x = minX; x <= maxX; x++) {
                Job j{nullptr, Request(), 0};
                j.request.zoom = zoom;
                j.request.x = x;
                j.request.y = y;
                jobs.push_back(j);
            }
        }
    }
    LightLock_Lock(&_lock);
    _prefetch.swap(jobs);
    _prefetchTotal = (int)_prefetch.size();
    _prefetchDone = 0;
    _prefetchGeneration++;
    CondVar_Broadcast(&_cond);
    LightLock_Unlock(&_lock);
    dbg("imagery: prefetching %d tiles", _prefetchTotal);
}

void SatelliteTileLoader::cancelPrefetch()
{
    LightLock_Lock(&_lock);
    _prefetch.clear();
    _prefetchTotal = _prefetchDone = 0;
    _prefetchGeneration++;
    LightLock_Unlock(&_lock);
}

void SatelliteTileLoader::prefetchProgress(int& done, int& total)
{
    LightLock_Lock(&_lock);
    done = _prefetchDone;
    total = _prefetchTotal;
    LightLock_Unlock(&_lock);
}

void SatelliteTileLoader::threadMain(void* arg)
{
    static_cast<SatelliteTileLoader*>(arg)->run();
}

void SatelliteTileLoader::run()
{
    // threadCreate hands back the handle after the thread may have started:
    // wait until it is there to be found.
    int self = -1;
    while (self < 0) {
        LightLock_Lock(&_lock);
        for (int i = 0; i < kThreads && self < 0; i++)
            if (_threads[i] == threadGetCurrent())
                self = i;
        LightLock_Unlock(&_lock);
        if (self < 0)
            svcSleepThread(1000 * 1000);
    }

    net::HttpClient http;
    http.setTimeout(30);
    http.setMaxBytes(512 * 1024);
    http.setProgress(keepGoing, const_cast<bool*>(&_abort[self]));
    for (;;) {
        LightLock_Lock(&_lock);
        for (;;) {
            if (s_quit) {
                LightLock_Unlock(&_lock);
                return;
            }
            const u64 now = osGetTime();
            if (_queue.empty() && _prefetch.empty())
                CondVar_Wait(&_cond, &_lock);
            else if (now < _pauseUntil)
                CondVar_WaitTimeout(&_cond, &_lock, (s64)(_pauseUntil - now) * 1000 * 1000);
            else
                break;
        }
        // What is on screen first, the newest first; then the prefetch.
        Job job;
        if (!_queue.empty()) {
            job = _queue.back();
            _queue.pop_back();
        } else {
            job = _prefetch.front();
            _prefetch.pop_front();
        }
        _busy[self] = job.client;
        _jobGeneration[self] = _prefetchGeneration;
        _abort[self] = false;
        LightLock_Unlock(&_lock);

        const Request& r = job.request;
        bool retryLater = false;
        bool fetched = false;
        Status status;
        if (job.client) {
            Answer a;
            a.request = r;
            // The other thread, or the prefetch, may have fetched it since.
            switch (loadCached(r.zoom, r.x, r.y, a.pixels)) {
            case Cached::Tile:
                a.status = Status::Loaded;
                break;
            case Cached::Missing:
                a.status = Status::Missing;
                break;
            default:
                a.status = download(&http, r, &a.pixels, retryLater);
                fetched = true;
                break;
            }
            status = a.status;
            job.client->onTileDownloaded(std::move(a));
        } else {
            switch (cacheState(r.zoom, r.x, r.y)) {
            case Cached::Tile:
                status = Status::Loaded;
                break;
            case Cached::Missing:
                status = Status::Missing;
                break;
            default:
                status = download(&http, r, nullptr, retryLater);
                fetched = true;
                break;
            }
        }

        LightLock_Lock(&_lock);
        _busy[self] = nullptr;
        if (fetched) {
            if (status == Status::Failed)
                _failed++;
            else
                _downloaded++;
        }
        if (!job.client && _jobGeneration[self] == _prefetchGeneration) {
            if (status == Status::Failed && retryLater && !_abort[self] && ++job.attempts < kPrefetchAttempts)
                _prefetch.push_back(job);
            else
                _prefetchDone++;
            if (_prefetchDone == _prefetchTotal)
                dbg("imagery: prefetch done, %d downloaded, %d failed in all", _downloaded, _failed);
        }
        // (An abandoned download is no sign of the network's trouble.)
        if (retryLater && !_abort[self])
            _pauseUntil = osGetTime() + 5000;
        _abort[self] = false;
        CondVar_Broadcast(&_cond);
        LightLock_Unlock(&_lock);
    }
}

SatelliteTileLoader::Status SatelliteTileLoader::download(void* client, const Request& r, std::vector<u16>* pixels,
                                                          bool& retryLater)
{
    net::HttpClient& http = *static_cast<net::HttpClient*>(client);
    const std::string u = url(r.zoom, r.x, r.y);
    std::vector<u8> body;
    std::string error;
    const int status = http.get(u, body, error);
    if (status == 404) {
        // No imagery at this level here: remembered, never asked again.
        writeCache(r.zoom, r.x, r.y, std::vector<u8>());
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
    // left to when it is drawn (which removes a bad one).
    if (!jpeg::isJpeg(body.data(), body.size())) {
        dbg("imagery: %d/%d/%d: not a JPEG", r.zoom, r.x, r.y);
        return Status::Failed;
    }
    if (pixels && !decodeTile(body, *pixels, error)) {
        dbg("imagery: %d/%d/%d: %s", r.zoom, r.x, r.y, error.c_str());
        return Status::Failed;
    }
    writeCache(r.zoom, r.x, r.y, body);
    return Status::Loaded;
}
