// PlayerMaps: the port's PlayerMapCache. The game's player
// maps were drawn by its players in class3outbreak.com's editor and
// downloaded from there; the site is gone. The port makes them itself: the
// player picks an area over the satellite imagery (PlayerMapNode on
// MapSelectScene's Player tab), and a builder thread
// 1. asks Overpass (OpenStreetMap's query service) for the area's
//    buildings, barriers, trees and water,
// 2. draws them into a collision layer (osmcollision.h),
// 3. adds the trees the imagery shows (its zoom-18 tiles, a pixel per cell,
//    which the map's own imagery download then has already),
// 4. and keeps the layer on the card, zlib-packed like the game's own:
//    save/playermaps/<id>.cl, with save/playermaps/index.txt a line
//    "id worldX worldY size" per map.
//
// A map is a square of 2000 or 4000 zoom-20 world pixels (the featured
// online maps' sizes), its corner on the cell grid. It starts in the middle,
// the outbreak from the north (the options change it).
#pragma once

#include <string>
#include <vector>

#include <3ds.h>

#include "game/mapconfig.h"

class PlayerMaps {
public:
    static const int kSmallSize = 2000;
    static const int kLargeSize = 4000;

    struct Map {
        int id = 0;
        int worldX = 0;
        int worldY = 0;
        int size = 0;
    };

    // What the builder is doing, for the main thread to poll.
    enum class State {
        Idle,
        Working,
        // The map is made (mapId); acknowledge() to go back to Idle.
        Done,
        // It could not be made (error); likewise.
        Failed,
    };

    // Made on the main thread first (the network comes up there).
    static PlayerMaps* getInstance();

    // The maps on the card, oldest first.
    const std::vector<Map>& getMaps() const { return _maps; }
    // nullptr if there is none.
    const Map* getMap(int id) const;
    // A map's MapConfig, for GameNode::startMap (which copies it).
    void fillConfig(const Map& map, MapConfig& config) const;
    // Off the list, its line out of the index and its layer off the card (its
    // imagery stays in the shared cache). False if the index could not be
    // written; it is off the list all the same.
    bool deleteMap(int id);

    // Starts making a map of size world pixels square centred on centreX,
    // centreY (zoom-20 world pixels). Any build before is cancelled.
    void build(int centreX, int centreY, int size);
    // Cancels the build; the state goes back to Idle at once.
    void cancel();
    // Done or Failed to Idle.
    void acknowledge();

    // The state now, and for Working a word on the step (short, for the
    // download popup's second line), for Failed the reason, for Done the new
    // map's id.
    State poll(std::string& text, int& mapId);

    // The maps finished since into the list (poll does it too): on the main
    // thread, so getMaps never changes under the scene.
    void update();

private:
    PlayerMaps();
    static void threadMain(void* arg);
    void run();
    // One build, on the builder thread. True with the layer saved under id.
    bool make(int generation, const Map& map, std::string& error);
    bool cancelled(int generation);
    void setText(int generation, const std::string& text);

    void loadIndex();
    bool save(const Map& map, const std::vector<u8>& cells, std::string& error);
    static std::string dir();
    static std::string layerPath(int id);

    static bool onProgress(void* user, size_t now, size_t total);

    std::vector<Map> _maps;
    int _nextId = 1; // under _lock once the thread runs

    LightLock _lock;
    // index.txt's: the builder adds to it, deleteMap rewrites it.
    LightLock _fileLock;
    CondVar _cond;
    Thread _thread = nullptr;
    // Each build's: a cancel or a new build moves it on, and the builder
    // stops at its next check when it has.
    int _generation = 0;
    bool _pending = false; // a build waits for the thread
    Map _job;
    int _jobGeneration = 0; // the build the thread is on
    State _state = State::Idle;
    std::string _text;
    std::string _error;
    int _doneId = 0;
    // Saved by the builder, not yet in _maps.
    std::vector<Map> _finished;
    // Overpass's answer is coming: its kilobytes are the text, the last
    // shown in _shownKB.
    bool _countBytes = false;
    size_t _shownKB = 0;
};
