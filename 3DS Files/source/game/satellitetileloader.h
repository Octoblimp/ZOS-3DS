// SatelliteTileLoader: the port's GoogleMapTileLoader. The
// game's online maps were Google's satellite imagery, read out of the Google
// Maps SDK's cache on the phone. The port downloads the same kind of imagery
// from Esri's World Imagery (server.arcgisonline.com, no key, free for
// non-commercial use with its credit on screen).
//
// - Tiles are 256-pixel JPEGs in the usual XYZ scheme. With blankTile=false
//   a level the imagery lacks is a 404, not a "no data" picture: the tile is
//   remembered as missing and MapRenderer draws its ancestor instead.
// - Two download threads, one connection each, with net::kUserAgent.
// - Requests come from what MapRenderer draws, in the order it asks (the
//   nearest the middle of the view first); old ones are dropped if the queue
//   grows, which MapRenderer's limit keeps it from.
// - The map being played or made is "open": its imagery is kept on the card
//   in one file, a TilePack (tilepack.h says why one file), so it plays
//   offline afterwards. A player map's is downloaded whole while it is made;
//   a featured map's while it is played, by one thread with a rest between
//   tiles so the game does not feel it.
// - Tiles elsewhere (browsing the world for a place) are kept in memory
//   only, the latest few megabytes' worth.
// - The credit, "Source: Esri, Vantor, Earthstar Geographics, and the GIS
//   User Community" (the service's copyrightText), is on the top screen
//   wherever the imagery shows: GUIScene's in a map, MapSelectScene's on the
//   Player tab.
//
// A tile comes back as RGB565 in the GPU's tiled order, ready for
// gfx::createTiledTexture on the main thread.
#pragma once

#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <3ds.h>

class TilePack;

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
        // Dropped from the queue unasked: ask again in a while.
        Cancelled,
    };

    // What is here of a tile already.
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

    // Made on the main thread first (it brings up the network).
    static SatelliteTileLoader* getInstance();
    // For the app's exit, if the loader was ever made: stops the downloads
    // and the threads, and writes out and closes the open map's pack. After
    // it the loader touches the card no more, and answers no one. False if a
    // thread is still in the network (a name lookup cannot be cut short):
    // the network must then be left up.
    static bool shutdown();

    // The tile, if it is here already (the open map's pack, or memory), into
    // pixels, now, on the caller's thread: Tile with pixels, Missing, or None.
    Cached loadCached(int zoom, int x, int y, std::vector<u16>& pixels);
    // Its URL.
    static std::string url(int zoom, int x, int y);
    // The port's player maps' (playermaps.cpp): the tile's JPEG, from here or
    // else downloaded with http (a net::HttpClient*) and kept, now, on the
    // caller's thread. Loaded with jpeg, Missing, or Failed.
    Status fetchJpeg(void* http, int zoom, int x, int y, std::vector<u8>& jpeg);
    // The tiles over an area of zoom-20 world pixels at a level.
    static void tileRange(int zoom, int worldX, int worldY, int width, int height, int& minX, int& minY, int& maxX,
                          int& maxY);

    // Queues a download; the answer comes to client later, Cancelled at the
    // latest when the queue drops it.
    void request(Client* client, const Request& request);
    // Drops client's queued requests, without answers, and abandons its
    // downloads in progress (their answers, Failed, may still come). With
    // wait, waits for those: after that no answer comes to it (for its
    // destructor).
    void cancel(Client* client, bool wait);

    // The map being played or made: its imagery's pack at packPath (made if
    // it is new) over its area, zoom-20 world pixels. Tiles downloaded over
    // it go into the pack. The map open before is closed, unless it is the
    // same. From the main thread.
    void openMap(const std::string& packPath, int worldX, int worldY, int width, int height);
    // Downloads into the open map's pack, behind every request, each tile it
    // lacks at every level up to kMaxZoom. With background (a game is on),
    // one thread does it, resting between tiles; without (a map is being
    // made, and the player waits), both, as fast as they go.
    void prefetchMap(bool background);
    // Its progress: tiles done (in the pack, or given up) of all. 0 of 0 when
    // there is none.
    void prefetchProgress(int& done, int& total);
    // Writes out what the open map's pack has gathered, now.
    void flushMap();
    // Closes the open map, dropping its prefetch; a download thread writes
    // its pack out. With removePack, the pack goes from the card instead (a
    // map that was not made after all). From the main thread.
    void closeMap(bool removePack = false);
    // Removes a pack from the card (a map deleted), closing it first if it
    // is open or waiting to be written out. From the main thread.
    void removePack(const std::string& packPath);

    // For the log.
    int downloaded() const { return _downloaded; }
    int failed() const { return _failed; }

private:
    SatelliteTileLoader();
    static void threadMain(void* arg);
    void run(int self);
    // The tile's JPEG if it is here: the open map's pack, then memory.
    Cached findJpeg(int zoom, int x, int y, std::vector<u8>& jpeg);
    // A downloaded tile (empty: missing) into the open map's pack when it is
    // over the map, else memory.
    void keep(int zoom, int x, int y, const std::vector<u8>& jpeg);
    // Fetches, checks and keeps one tile, decoding it into pixels when asked
    // to. Loaded, Missing or Failed. abort, if given, is the thread's flag.
    Status download(void* http, const Request& r, std::vector<u16>* pixels, bool& retryLater,
                    const volatile bool* abort);
    // With _lock held.
    bool overMap(int zoom, int x, int y) const;
    void closeMapLocked(bool removePack);
    // With _lock held, if there are no threads to: the closed packs, now.
    void finishClosingLocked();
    // Writes out and closes a pack, removing it after if asked to.
    static void finishPack(TilePack* pack, bool removePack);

    struct Job {
        Client* client; // null for a prefetch
        Request request;
        int attempts;
    };

    struct Closing {
        std::shared_ptr<TilePack> pack;
        bool remove;
    };

    // Each its own connection: a tile is mostly waiting on the server, so
    // more at once is more a second.
    static const int kThreads = 4;
    // Prefetching in the background (a game on), at most this many.
    static const int kBackgroundThreads = 2;
    // The most queued; beyond it the oldest go.
    static const size_t kMaxQueued = 48;
    // A prefetched tile that keeps failing for the network is given up after
    // this many tries.
    static const int kPrefetchAttempts = 5;
    // In the background, with the threads in the main thread's gaps, the
    // rest after each prefetched tile (ms).
    static const int kPrefetchRest = 100;
    // Tiles browsed outside a map, kept in memory: this many bytes of JPEG
    // (a few hundred tiles; the malloc heap is 24 MB on an Old 3DS).
    static const size_t kMemoryBytes = 6 * 1024 * 1024;

    LightLock _lock;
    CondVar _cond;
    std::deque<Job> _queue;              // oldest at the front
    std::deque<Job> _prefetch;           // in order
    bool _prefetchBackground = false;
    int _prefetchTotal = 0;
    int _prefetchDone = 0;
    int _prefetchGeneration = 0;
    // In the background: the threads on a prefetched tile now, and when the
    // next may start (osGetTime).
    int _prefetching = 0;
    u64 _nextPrefetch = 0;
    int _jobGeneration[kThreads] = {};   // the prefetch each thread's job was for
    Client* _busy[kThreads] = {};        // the client each thread works for
    bool _onPrefetch[kThreads] = {};     // or whether it is on a prefetched tile
    // Set by cancel with wait: the thread's download is abandoned.
    volatile bool _abort[kThreads] = {};
    Thread _threads[kThreads] = {};
    // After a failure that is the network's, no download starts before this
    // (osGetTime), so a dead connection is not tried over and over.
    u64 _pauseUntil = 0;
    int _downloaded = 0;
    int _failed = 0;

    // The open map.
    std::shared_ptr<TilePack> _pack;
    int _mapX = 0, _mapY = 0, _mapW = 0, _mapH = 0;
    // Maps closed, their packs waiting for a thread to write them out; and
    // the one being written out now.
    std::deque<Closing> _closing;
    std::shared_ptr<TilePack> _closingNow;

    // Tiles kept in memory, by TilePack's key; the oldest first in _memoryOrder.
    std::unordered_map<u64, std::vector<u8>> _memory;
    std::deque<u64> _memoryOrder;
    size_t _memoryUsed = 0;
};
