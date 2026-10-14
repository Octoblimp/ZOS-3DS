// The port's: the Player tab's search (MapSelectScene). A place's name, typed
// on the 3DS's keyboard, to where it is, from OpenStreetMap's Nominatim
// (nominatim.openstreetmap.org, one request a search, as its usage policy
// allows: no typing-as-you-go). The game had Google's map to zoom about; on
// the 3DS, zooming from the whole world to a street by hand takes a while.
//
// One search at a time, on a thread of its own; a new one replaces it.
#pragma once

#include <string>

#include <3ds.h>

class PlaceSearch {
public:
    enum class State { Idle, Working, Done, Failed };

    struct Place {
        std::string name;               // Nominatim's display_name
        double lat = 0.0, lon = 0.0;    // degrees
        // Its extent, in degrees.
        double south = 0.0, north = 0.0, west = 0.0, east = 0.0;
    };

    // Made on the main thread.
    static PlaceSearch* getInstance();
    // For the app's exit, if it was ever made: stops the search and waits a
    // while for its thread. False if it is still in the network.
    static bool shutdown();

    void start(const std::string& query);
    // Forgets the search under way, if any; its request is abandoned.
    void cancel();
    // On the main thread: the search's state; Done gives the place, Failed
    // the reason, once each (the state is Idle after).
    State poll(Place& place, std::string& error);
    const std::string& getQuery() const { return _query; }

private:
    PlaceSearch();
    static void threadMain(void* arg);
    void run();
    // curl's progress: false once the search is cancelled or replaced.
    static bool keepGoing(void* user, size_t now, size_t total);
    // The first answer of Nominatim's JSON (jsonv2) into place. False if
    // there is none.
    static bool parse(const std::string& json, Place& place);

    LightLock _lock;
    CondVar _cond;
    Thread _thread = nullptr;
    int _generation = 0;       // a search's, bumped by start and cancel
    int _jobGeneration = 0;    // the thread's search's
    bool _pending = false;
    bool _quit = false;
    std::string _query;        // the main thread's last
    std::string _job;          // the thread's
    State _state = State::Idle;
    Place _place;
    std::string _error;
};
