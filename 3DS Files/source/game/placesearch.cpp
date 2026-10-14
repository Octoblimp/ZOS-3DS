#include "game/placesearch.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "debuglog.h"
#include "game/satellitetileloader.h"
#include "net/http.h"
#include "workers.h"

namespace {

const char* const kNominatim = "https://nominatim.openstreetmap.org/search";

// After the first `"key":` in json, the string value (escapes undone, \u as
// UTF-8). False if the key is not there or its value is no string.
bool stringValue(const std::string& json, const char* key, std::string& out)
{
    const std::string quoted = std::string("\"") + key + "\"";
    size_t p = json.find(quoted);
    if (p == std::string::npos)
        return false;
    p = json.find_first_not_of(" \t\r\n", p + quoted.size());
    if (p == std::string::npos || json[p] != ':')
        return false;
    p = json.find_first_not_of(" \t\r\n", p + 1);
    if (p == std::string::npos || json[p] != '"')
        return false;
    out.clear();
    for (p++; p < json.size(); p++) {
        const char c = json[p];
        if (c == '"')
            return true;
        if (c != '\\' || p + 1 >= json.size()) {
            out += c;
            continue;
        }
        const char e = json[++p];
        if (e == 'u' && p + 4 < json.size()) {
            const unsigned cp = (unsigned)std::strtoul(json.substr(p + 1, 4).c_str(), nullptr, 16);
            p += 4;
            if (cp < 0x80) {
                out += (char)cp;
            } else if (cp < 0x800) {
                out += (char)(0xc0 | cp >> 6);
                out += (char)(0x80 | (cp & 0x3f));
            } else {
                out += (char)(0xe0 | cp >> 12);
                out += (char)(0x80 | (cp >> 6 & 0x3f));
                out += (char)(0x80 | (cp & 0x3f));
            }
        } else {
            out += e == 'n' ? '\n' : e == 't' ? '\t' : e;
        }
    }
    return false;
}

} // namespace

namespace {
PlaceSearch* s_instance = nullptr;
} // namespace

PlaceSearch* PlaceSearch::getInstance()
{
    if (!s_instance)
        s_instance = new PlaceSearch();
    return s_instance;
}

bool PlaceSearch::shutdown()
{
    PlaceSearch* s = s_instance;
    if (!s || !s->_thread)
        return true;
    LightLock_Lock(&s->_lock);
    s->_quit = true;
    s->_generation++;
    s->_pending = false;
    CondVar_Broadcast(&s->_cond);
    LightLock_Unlock(&s->_lock);
    if (R_FAILED(threadJoin(s->_thread, 3000ULL * 1000 * 1000))) {
        dbg("place search: still busy at exit");
        return false;
    }
    threadFree(s->_thread);
    s->_thread = nullptr;
    return true;
}

PlaceSearch::PlaceSearch()
{
    LightLock_Init(&_lock);
    CondVar_Init(&_cond);
    // The sockets and TLS, on the main thread.
    SatelliteTileLoader::getInstance();
    // As PlayerMaps' builder: on the workers' core, room for the handshake.
    // Joined at the exit.
    _thread = createWorkerThread(threadMain, this, 128 * 1024, 2);
    if (!_thread)
        dbg("place search: no thread");
}

void PlaceSearch::start(const std::string& query)
{
    LightLock_Lock(&_lock);
    _generation++;
    _query = query;
    _job = query;
    _pending = true;
    _state = _thread ? State::Working : State::Failed;
    _error = _thread ? std::string() : std::string("The search could not start");
    CondVar_Signal(&_cond);
    LightLock_Unlock(&_lock);
}

void PlaceSearch::cancel()
{
    LightLock_Lock(&_lock);
    _generation++;
    _pending = false;
    _state = State::Idle;
    LightLock_Unlock(&_lock);
}

PlaceSearch::State PlaceSearch::poll(Place& place, std::string& error)
{
    LightLock_Lock(&_lock);
    const State state = _state;
    if (state == State::Done)
        place = _place;
    if (state == State::Failed)
        error = _error;
    if (state == State::Done || state == State::Failed)
        _state = State::Idle;
    LightLock_Unlock(&_lock);
    return state;
}

void PlaceSearch::threadMain(void* arg)
{
    static_cast<PlaceSearch*>(arg)->run();
}

bool PlaceSearch::keepGoing(void* user, size_t, size_t)
{
    PlaceSearch* self = static_cast<PlaceSearch*>(user);
    LightLock_Lock(&self->_lock);
    const bool go = self->_jobGeneration == self->_generation;
    LightLock_Unlock(&self->_lock);
    return go;
}

void PlaceSearch::run()
{
    net::HttpClient http;
    http.setMaxBytes(256 * 1024);
    http.setTimeout(30);
    http.setProgress(keepGoing, this);
    for (;;) {
        LightLock_Lock(&_lock);
        while (!_pending && !_quit)
            CondVar_Wait(&_cond, &_lock);
        if (_quit) {
            LightLock_Unlock(&_lock);
            return;
        }
        _pending = false;
        const std::string query = _job;
        const int generation = _generation;
        _jobGeneration = generation;
        LightLock_Unlock(&_lock);

        // The best match only, its name in English where OpenStreetMap has
        // one (the font is Latin).
        const std::string url = std::string(kNominatim) + "?format=jsonv2&limit=1&accept-language=en&q=" +
                                net::urlEncode(query);
        std::vector<u8> body;
        std::string error;
        const u64 t0 = osGetTime();
        const int status = http.get(url, body, error);
        Place place;
        bool found = false;
        if (status == 200) {
            found = parse(std::string(body.begin(), body.end()), place);
            if (!found)
                error = "Nothing found for \"" + query + "\"";
        } else if (status == 0) {
            error = "The search did not answer:\n" + error;
        } else {
            char buf[64];
            snprintf(buf, sizeof(buf), "The search answered HTTP %d", status);
            error = buf;
        }
        // (Neither what was searched for nor what was found: the log may be
        // shared, and a search is often for home.)
        dbg("place search: %s in %d ms",
            found         ? "found"
            : status == 200 ? "nothing found"
            : status == 0   ? "no answer"
                            : "HTTP error",
            (int)(osGetTime() - t0));

        LightLock_Lock(&_lock);
        if (generation == _generation) {
            _state = found ? State::Done : State::Failed;
            _place = place;
            _error = error;
        }
        LightLock_Unlock(&_lock);
    }
}

bool PlaceSearch::parse(const std::string& json, Place& place)
{
    // [{"place_id":..., "lat":"51.5", "lon":"-0.12", ..., "display_name":
    // "...", "boundingbox":["s","n","w","e"]}], or [] for nothing.
    std::string lat, lon;
    if (!stringValue(json, "lat", lat) || !stringValue(json, "lon", lon))
        return false;
    place.lat = std::strtod(lat.c_str(), nullptr);
    place.lon = std::strtod(lon.c_str(), nullptr);
    if (!stringValue(json, "display_name", place.name))
        place.name.clear();
    place.south = place.north = place.lat;
    place.west = place.east = place.lon;
    const size_t box = json.find("\"boundingbox\"");
    if (box != std::string::npos) {
        double v[4];
        int n = 0;
        size_t p = json.find('[', box);
        while (p != std::string::npos && n < 4) {
            p = json.find('"', p + 1);
            if (p == std::string::npos)
                break;
            v[n++] = std::strtod(json.c_str() + p + 1, nullptr);
            p = json.find('"', p + 1);
        }
        if (n == 4 && v[0] <= v[1] && v[2] <= v[3]) {
            place.south = v[0];
            place.north = v[1];
            place.west = v[2];
            place.east = v[3];
        }
    }
    return true;
}
