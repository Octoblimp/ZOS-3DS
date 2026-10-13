// SatelliteTileLoader: the port's GoogleMapTileLoader. The
// game's online maps were Google's satellite imagery, read out of the Google
// Maps SDK's cache on the phone. The port downloads the same kind of imagery
// from Esri's World Imagery (server.arcgisonline.com, no key, free for
// non-commercial use with its credit on screen) and keeps it on the card.
//
// - Tiles are 256-pixel JPEGs in the usual XYZ scheme. With blankTile=false
//   a level the imagery lacks is a 404, not a "no data" picture: the tile is
//   remembered as missing (an empty file in the cache) and MapRenderer draws
//   its ancestor instead.
// - Two download threads, one connection each, with net::kUserAgent.
// - Requests come from what MapRenderer draws, the newest first; old ones are
//   dropped when the queue grows.
// - A map, once started, is downloaded whole behind those (prefetch), so it
//   plays offline afterwards.
// - The credit, "Source: Esri, Vantor, Earthstar Geographics, and the GIS
//   User Community" (the service's copyrightText), is on the top screen
//   wherever the imagery shows: GUIScene's in a map, MapSelectScene's on the
//   Player tab.
//
// A tile comes back as RGB565 in the GPU's tiled order, ready for
// gfx::createTiledTexture on the main thread.
#pragma once

#include <deque>
#include <string>
#include <vector>

#include <3ds.h>

class SatelliteTileLoader {
public:
    // The levels asked for: the game draws zoom 20 at most (a map pixel a
    // tile pixel). Esri has 0..23 in places and less in others.
    static const int kMinZoom = 0;
    static const int kMaxZoom = 20;
    static const int kTileSize = 256;

    enum class Status {
        Loaded,
        // The imagery has no such tile (a 404): never ask again.
        Missing,
        // Not now (no connection, the server busy, an error): ask again later.
        Failed,
        // Dropped from the queue unasked: ask again whenever.
        Cancelled,
    };

    // What the card's cache has of a tile.
    enum class Cached { None, Tile, Missing };

    struct Request {
        int zoom = 0, x = 0, y = 0;
        // The client's own, given back.
        void* tile = nullptr;
        int generation = 0;
    };

    struct Answer {
        Request request;
        Status status = Status::Failed;
        std::vector<u16> pixels; // kTileSize^2, RGB565, tiled
    };

    // Who asked: answers come to onTileDownloaded on a download thread, and
    // must be taken quickly (queue them for the main thread).
    class Client {
    public:
        virtual ~Client() {}
        virtual void onTileDownloaded(Answer&& answer) = 0;
    };

    static SatelliteTileLoader* getInstance();

    // The tile from the card's cache, now, on the caller's thread: Tile with
    // pixels, Missing, or None (also when the file was bad, and removed).
    static Cached loadCached(int zoom, int x, int y, std::vector<u16>& pixels);
    // Its file on the card.
    static std::string cachePath(int zoom, int x, int y);
    // Its URL.
    static std::string url(int zoom, int x, int y);
    // The port's player maps' (playermaps.cpp): the tile's JPEG from the
    // card's cache, or else downloaded with http (a net::HttpClient*) into it,
    // now, on the caller's thread. Loaded with jpeg, Missing, or Failed.
    static Status fetchJpeg(void* http, int zoom, int x, int y, std::vector<u8>& jpeg);
    // The tiles over an area of zoom-20 world pixels at a level.
    static void tileRange(int zoom, int worldX, int worldY, int width, int height, int& minX, int& minY, int& maxX,
                          int& maxY);

    // Queues a download; the answer comes to client later, Cancelled at the
    // latest when the queue drops it.
    void request(Client* client, const Request& request);
    // Drops client's queued requests, without answers. With wait, also
    // abandons its downloads in progress and waits for them: after that no
    // answer comes to it (for its destructor).
    void cancel(Client* client, bool wait);

    // Downloads into the cache, behind every request, each tile over the
    // area (zoom-20 world pixels) at every level up to kMaxZoom, replacing
    // any prefetch before.
    void prefetch(int worldX, int worldY, int width, int height);
    void cancelPrefetch();
    // Its progress: tiles done (on the card, missing, or given up) of all.
    // 0 of 0 when there is none.
    void prefetchProgress(int& done, int& total);

    // For the log.
    int downloaded() const { return _downloaded; }
    int failed() const { return _failed; }

private:
    SatelliteTileLoader();
    static void threadMain(void* arg);
    void run();
    // Fetches, checks and caches one tile, decoding it into pixels when
    // asked to. Loaded, Missing or Failed.
    Status download(void* http, const Request& r, std::vector<u16>* pixels, bool& retryLater);

    struct Job {
        Client* client; // null for a prefetch
        Request request;
        int attempts;
    };

    static const int kThreads = 2;
    // The most queued; beyond it the oldest go. More than two screens' worth.
    static const size_t kMaxQueued = 48;
    // A prefetched tile that keeps failing for the network is given up after
    // this many tries.
    static const int kPrefetchAttempts = 5;

    LightLock _lock;
    CondVar _cond;
    std::deque<Job> _queue;              // newest at the back
    std::deque<Job> _prefetch;           // in order
    int _prefetchTotal = 0;
    int _prefetchDone = 0;
    int _prefetchGeneration = 0;
    int _jobGeneration[kThreads] = {};   // the prefetch each thread's job was for
    Client* _busy[kThreads] = {};        // the client each thread works for
    // Set by cancel with wait: the thread's download is abandoned.
    volatile bool _abort[kThreads] = {};
    Thread _threads[kThreads] = {};
    // After a failure that is the network's, no download starts before this
    // (osGetTime), so a dead connection is not tried over and over.
    u64 _pauseUntil = 0;
    int _downloaded = 0;
    int _failed = 0;
};
