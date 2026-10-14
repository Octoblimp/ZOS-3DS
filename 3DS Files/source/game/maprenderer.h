// MapRenderer (decomp/classes/MapRenderer.c): the map under the people, a
// pyramid of 256-pixel tiles. Zoom 20's pixels are map pixels; each level
// down halves the tiles across. render picks the level whose tiles are at
// least 128 screen pixels, draws the tiles in view, and asks the background
// thread for the ones it lacks, drawing meanwhile the part of the nearest
// loaded ancestor that covers them. Tiles not drawn for 60 frames are freed
// once more than the maximum are loaded (the bottom level never is).
//
// The game decoded JPEGs on its thread into cocos2d::Images, uploaded on the
// GL thread. Here the thread reads the tile's .t3x (ETC1, made from the
// JPEG) and the main thread makes the texture: the 3DS's linear memory
// allocator, which every texture comes from, has no lock.
//
// Online, the tiles are satellite imagery (SatelliteTileLoader, Esri's in
// place of Google's): the thread takes one the loader keeps (the open map's
// pack, or memory) if it is there, and otherwise asks SatelliteTileLoader to
// download it, whose answer comes back to the same queue. They arrive as RGB565 (128 KB, against an
// ETC1 tile's 32 KB), so fewer are kept. A tile the imagery lacks (zoom 20
// in many places) is drawn from its ancestor, as one still loading is.
//
// Positions are screen pixels from the top left, as the game's; render turns
// them into its node's space (y up), as SimulationRenderer does.
#pragma once

#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

#include <3ds.h>

#include "cocos/base.h"
#include "game/satellitetileloader.h"
#include "gfx/gfx.h"

class MapRenderer : private SatelliteTileLoader::Client {
public:
    // The game's: Google (0), Google's satellite tiles through
    // GoogleMapTileLoader, which the port drops; Online takes its place for
    // satellite imagery from Esri. Embedded (1): an offline map's tiles in the
    // data. None (2) before setupMap and after clear.
    enum class MapType { Online = 0, Embedded = 1, None = 2 };

    // density is the screen's (the game's content scale): 1 on the 3DS.
    // Throws "Error creating background thread".
    explicit MapRenderer(float density);
    ~MapRenderer();

    MapRenderer(const MapRenderer&) = delete;
    MapRenderer& operator=(const MapRenderer&) = delete;

    // The game made its shader here; gfx has the port's.
    void onSurfaceCreated() {}
    // The screen, in pixels.
    void onSurfaceChanged(int width, int height);
    // The map's tile pyramid: Embedded, mapId's, width x height map pixels
    // (a square, a power of two of tiles across); Online, the world's, zoom
    // 1 to 21. Throws as the game does when the size will not do.
    void setupMap(MapType type, int mapId, int width, int height);
    // Loads, now, the tiles render would draw for this view (and an embedded
    // map's bottom tile, which render falls back on), so the first frame has
    // its map.
    void preloadTiles(double x, double y, float scale);
    // x, y is the zoom-20 pixel at the screen's top left (a map pixel, for an
    // embedded map); scale the zoom, screen pixels a map pixel before
    // density. transform is the node's (its space is the screen's, y up).
    // The port's viewWidth, viewHeight (screen pixels) bound the tiles drawn
    // and asked for, for a view smaller than the screen (GUIScene's
    // minimap); 0 is the game's, the screen's width both ways.
    void render(double x, double y, float scale, const cocos2d::Mat4& transform, float viewWidth = 0.0f,
                float viewHeight = 0.0f);
    // Forgets the map and its tiles; queued loads are dropped.
    void clear();
    int getNumTilesLoaded() const { return _numTilesLoaded; }
    // Frees, next frame, every tile not drawn for 60 frames.
    void onTrimMemory();
    // The port's: of the tiles the last render wanted in view, how many it
    // drew (a tile the imagery lacks counts, drawn from its ancestor), for
    // the loading line on the top screen.
    void getViewProgress(int& ready, int& total) const
    {
        ready = _viewReady;
        total = _viewTotal;
    }
    // The copyright lines of the tiles drawn, joined by ", ", updated once a
    // second. Reading it clears the updated flag.
    const std::string& getRenderedTilesCombinedCopyright();
    bool getRenderedTilesCombinedCopyrightUpdated() const { return _copyrightUpdated; }

    // For the log: the online tiles' traffic since the start.
    struct OnlineStats {
        int asked = 0;      // loads render asked of the background thread
        int cardReads = 0;  // of them, looked for among the kept tiles
        int sent = 0;       // not there: sent to SatelliteTileLoader
        int dropped = 0;    // its answers Cancelled (dropped from its queue)
        int downloaded = 0; // its answers Loaded
    };
    OnlineStats getOnlineStats();

private:
    // 0x18 bytes.
    struct TileInfo {
        gfx::Texture texture;                   // 0x00 (a GL name)
        std::vector<std::string> copyrights;    // 0x04
        bool loading = false;                   // 0x10
        int lastRenderFrame = 0;                // 0x14
        // The port's: online, a tile that failed to download is not asked
        // for again before this frame.
        int retryFrame = 0;
    };

    // 0x24 bytes. Tiles by tilesX * y + x, made as asked for.
    struct ZoomLevelInfo {
        int tilesX = 0;                         // 0x00
        int tilesY = 0;                         // 0x04
        std::unordered_map<long long, TileInfo> tiles;  // 0x08
        TileInfo& getTile(int x, int y);
    };

    // 0x24 bytes: a load asked of the background thread, and its answer.
    struct TileLoadInfo {
        TileInfo* tile = nullptr;               // 0x00
        int zoom = 0, x = 0, y = 0;             // 0x04
        // clear's count when asked; the answer is dropped if it has moved.
        int generation = -1;                    // 0x10
        // Made on the main thread: the background thread touches nothing of
        // the renderer's but the queues.
        MapType type = MapType::None;
        std::string path;
        // The answer: the .t3x (the game's: a cocos2d::Image at 0x14).
        std::vector<u8> data;
        std::vector<std::string> copyrights;    // 0x18
        // Online: the tile, RGB565 and tiled; empty if it did not come.
        std::vector<u16> pixels;
        // Online: try again later (a failed download), or soon (a request
        // the loader dropped).
        bool retryLater = false;
        bool cancelled = false;
        // Online: the imagery has no such tile; never ask again.
        bool missing = false;
    };

    static void threadWrapper(void* arg);
    void onBackgroundThread();
    // SatelliteTileLoader's answers, on its threads.
    void onTileDownloaded(SatelliteTileLoader::Answer&& answer) override;
    // On the main thread: an answer as the tile's texture.
    bool createTileTexture(TileInfo& tile, TileLoadInfo& info);
    // The tile's file (or, online, its URL).
    std::string tilePath(int zoom, int x, int y) const;
    // On any thread: an embedded tile's .t3x into data. False if there is
    // none.
    static bool loadTileData(MapType type, const std::string& path, int zoom, int x, int y,
                             std::vector<u8>& data, std::vector<std::string>& copyrights);
    // On any thread: an online tile the loader keeps into info (its pixels,
    // or missing). False if it is not there.
    static bool loadCachedTile(TileLoadInfo& info);
    // On the main thread: data as a tile's texture, which must be 256 x 256.
    static bool createTileTexture(gfx::Texture& out, const std::vector<u8>& data, int zoom, int x, int y);
    // Both, now; for preloadTiles.
    bool loadTileTexture(TileInfo& tile, int zoom, int x, int y);

    // The level for the scale (density's included), the tiles in view at it,
    // a tile's size in zoom-20 pixels, and on screen. viewWidth, viewHeight
    // as render's.
    void calculateZoomLevelAndTileRange(int x, int y, float scale, int& zoom, int& minX, int& minY, int& maxX,
                                        int& maxY, int& tileSizeX, int& tileSizeY, float& screenTileW,
                                        float& screenTileH, float viewWidth = 0.0f,
                                        float viewHeight = 0.0f) const;
    void calculateTileRange(int x, int y, int width, int height, int zoom, int& minX, int& minY, int& maxX,
                            int& maxY) const;
    void beginAsyncLoadTileTexture(TileInfo& tile, int zoom, int x, int y);
    // The thread's answers become textures; over the maximum, old tiles go.
    void handleLoadedTiles();
    void releaseAllTextures();
    cocos2d::Vec2 toNode(const cocos2d::Mat4& transform, float x, float y) const;

    float _density;                             // 0x00
    MapType _mapType = MapType::None;           // 0x08
    int _mapId = 0;                             // 0x0c
    cocos2d::Size _screen;                      // 0x10
    int _minZoom = 0;                           // 0x30
    int _maxZoom = 0;                           // 0x34
    std::vector<ZoomLevelInfo> _levels;         // 0x38
    int _numTilesLoaded = 0;                    // 0x44
    // The game's 400, or 150 on four phones short of memory. A tile here is
    // 32 KB of linear memory (ETC1), so 150 is 4.8 MB; online, 128 KB
    // (RGB565), and setupMap makes it 80 (10 MB), still four screens' worth.
    int _maxTiles = 150;                        // 0x48
    int _frame = 0;                             // 0x4c
    int _generation = 0;                        // 0x50
    std::deque<TileLoadInfo> _requests;         // 0x54
    std::deque<TileLoadInfo> _loaded;           // 0x7c
    bool _trimMemory = false;                   // 0xa4
    LightLock _lock;                            // 0xa8
    CondVar _cond;                              // 0xac
    std::string _copyright;                     // 0xb0
    int _copyrightFrame = -1000;                // 0xb4
    bool _copyrightUpdated = false;             // 0xb8
    // An embedded map's tile folder on the card, found by setupMap.
    std::string _tileDir;
    Thread _thread = nullptr;
    bool _quit = false;
    // Whether SatelliteTileLoader has been asked anything (its answers must
    // stop before this goes).
    bool _usedOnline = false;
    OnlineStats _onlineStats; // under _lock
    int _viewReady = 0;
    int _viewTotal = 0;
};
