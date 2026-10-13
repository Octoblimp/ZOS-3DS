#include "game/osmcollision.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace osm {
namespace {

const double kPi = 3.14159265358979323846;
const double kWorld = 268435456.0; // zoom-20 pixels around the world, 2^28
// A zoom-20 pixel at the equator, in metres (the Earth's circumference over
// 2^28); at latitude a, times cos(a).
const double kEquatorMetresPerPixel = 2.0 * kPi * 6378137.0 / kWorld;

// The layer's values (collisionlayer.h).
const u8 kWall = 0x00;
const u8 kOpen = 0x03;
const u8 kCover = 0x07;
const u8 kDoor = 0x0f;
const u8 kWindow = 0x16;
const u8 kInside = 0x1f;
const u8 kFence = 0x22;
const u8 kWater = 0x82;

double lonToWorldX(double lon)
{
    return (lon + 180.0) / 360.0 * kWorld;
}

double latToWorldY(double lat)
{
    const double s = std::sin(lat * kPi / 180.0);
    return (0.5 - std::log((1.0 + s) / (1.0 - s)) / (4.0 * kPi)) * kWorld;
}

double worldXToLon(double x)
{
    return x / kWorld * 360.0 - 180.0;
}

double worldYToLat(double y)
{
    return std::atan(std::sinh(kPi * (1.0 - 2.0 * y / kWorld))) * 180.0 / kPi;
}

// A point in cells from the layer's top left.
struct Pt {
    float x, y;
};
typedef std::vector<Pt> Line;

// ---------------------------------------------------------------- JSON

// One of Overpass's elements, as much of it as is drawn.
struct Element {
    enum Type { Other, Node, Way, Relation } type = Other;
    Pt pos = {0.0f, 0.0f};
    bool hasPos = false;
    std::vector<std::pair<std::string, std::string>> tags;
    // A way's geometry; a relation's members', one line each. A line breaks
    // where Overpass left a point out (null).
    std::vector<Line> lines;

    const std::string* tag(const char* key) const
    {
        for (const auto& t : tags)
            if (t.first == key)
                return &t.second;
        return nullptr;
    }
    bool is(const char* key, const char* value) const
    {
        const std::string* t = tag(key);
        return t != nullptr && *t == value;
    }
};

// A reader for just what Overpass sends: everything else is skipped.
class Reader {
public:
    Reader(const char* p, size_t size, const Area& area) : _p(p), _end(p + size), _area(area) {}

    bool failed() const { return _failed; }
    // Where it went wrong, for the log.
    size_t offset(const char* start) const { return (size_t)(_p - start); }

    void ws()
    {
        while (_p < _end && (*_p == ' ' || *_p == '\n' || *_p == '\r' || *_p == '\t'))
            _p++;
    }
    bool peek(char c)
    {
        ws();
        return _p < _end && *_p == c;
    }
    bool eat(char c)
    {
        if (!peek(c))
            return false;
        _p++;
        return true;
    }
    bool expect(char c) { return eat(c) || fail(); }
    bool fail()
    {
        _failed = true;
        return false;
    }

    bool string(std::string& out)
    {
        out.clear();
        if (!expect('"'))
            return false;
        while (_p < _end) {
            const char c = *_p++;
            if (c == '"')
                return true;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (_p >= _end)
                break;
            const char e = *_p++;
            switch (e) {
            case 'b':
                out += '\b';
                break;
            case 'f':
                out += '\f';
                break;
            case 'n':
                out += '\n';
                break;
            case 'r':
                out += '\r';
                break;
            case 't':
                out += '\t';
                break;
            case 'u': {
                unsigned cp = hex4();
                if (cp >= 0xd800 && cp < 0xdc00 && _end - _p >= 6 && _p[0] == '\\' && _p[1] == 'u') {
                    _p += 2;
                    const unsigned lo = hex4();
                    cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
                }
                utf8(out, cp);
                break;
            }
            default:
                out += e;
                break;
            }
        }
        return fail();
    }

    bool number(double& out)
    {
        ws();
        char buf[48];
        size_t n = 0;
        while (_p < _end && n + 1 < sizeof(buf) &&
               ((*_p >= '0' && *_p <= '9') || *_p == '-' || *_p == '+' || *_p == '.' || *_p == 'e' || *_p == 'E'))
            buf[n++] = *_p++;
        if (n == 0)
            return fail();
        buf[n] = 0;
        out = std::strtod(buf, nullptr);
        return true;
    }

    bool literal(const char* word)
    {
        ws();
        const size_t n = std::strlen(word);
        if ((size_t)(_end - _p) < n || std::memcmp(_p, word, n) != 0)
            return false;
        _p += n;
        return true;
    }

    // Any value.
    bool skip(int depth = 0)
    {
        if (depth > 64)
            return fail();
        ws();
        if (_p >= _end)
            return fail();
        std::string s;
        double d;
        switch (*_p) {
        case '{':
            _p++;
            if (eat('}'))
                return true;
            do {
                if (!string(s) || !expect(':') || !skip(depth + 1))
                    return false;
            } while (eat(','));
            return expect('}');
        case '[':
            _p++;
            if (eat(']'))
                return true;
            do {
                if (!skip(depth + 1))
                    return false;
            } while (eat(','));
            return expect(']');
        case '"':
            return string(s);
        case 't':
            return literal("true") || fail();
        case 'f':
            return literal("false") || fail();
        case 'n':
            return literal("null") || fail();
        default:
            return number(d);
        }
    }

    // [{"lat": .., "lon": ..} or null, ...] into lines, in cells.
    bool geometry(std::vector<Line>& lines)
    {
        if (!expect('['))
            return false;
        Line line;
        if (!eat(']')) {
            do {
                if (literal("null")) {
                    if (line.size() >= 2)
                        lines.push_back(line);
                    line.clear();
                    continue;
                }
                double lat = 0.0, lon = 0.0;
                if (!expect('{'))
                    return false;
                std::string key;
                do {
                    if (!string(key) || !expect(':'))
                        return false;
                    if (key == "lat") {
                        if (!number(lat))
                            return false;
                    } else if (key == "lon") {
                        if (!number(lon))
                            return false;
                    } else if (!skip()) {
                        return false;
                    }
                } while (eat(','));
                if (!expect('}'))
                    return false;
                line.push_back(toCells(lat, lon));
            } while (eat(','));
            if (!expect(']'))
                return false;
        }
        if (line.size() >= 2)
            lines.push_back(line);
        return true;
    }

    bool element(Element& e)
    {
        e = Element();
        if (!expect('{'))
            return false;
        if (eat('}'))
            return true;
        std::string key, value;
        double lat = 0.0, lon = 0.0;
        bool hasLat = false, hasLon = false;
        do {
            if (!string(key) || !expect(':'))
                return false;
            if (key == "type") {
                if (!string(value))
                    return false;
                e.type = value == "node" ? Element::Node
                         : value == "way" ? Element::Way
                         : value == "relation" ? Element::Relation
                                               : Element::Other;
            } else if (key == "lat") {
                hasLat = number(lat);
                if (!hasLat)
                    return false;
            } else if (key == "lon") {
                hasLon = number(lon);
                if (!hasLon)
                    return false;
            } else if (key == "tags") {
                if (!expect('{'))
                    return false;
                if (!eat('}')) {
                    do {
                        if (!string(key) || !expect(':') || !string(value))
                            return false;
                        e.tags.emplace_back(key, value);
                    } while (eat(','));
                    if (!expect('}'))
                        return false;
                }
            } else if (key == "geometry") {
                if (!geometry(e.lines))
                    return false;
            } else if (key == "members") {
                if (!expect('['))
                    return false;
                if (!eat(']')) {
                    do {
                        if (!expect('{'))
                            return false;
                        if (!eat('}')) {
                            do {
                                if (!string(key) || !expect(':'))
                                    return false;
                                if (key == "geometry") {
                                    if (!geometry(e.lines))
                                        return false;
                                } else if (!skip()) {
                                    return false;
                                }
                            } while (eat(','));
                            if (!expect('}'))
                                return false;
                        }
                    } while (eat(','));
                    if (!expect(']'))
                        return false;
                }
            } else if (!skip()) {
                return false;
            }
        } while (eat(','));
        if (hasLat && hasLon) {
            e.pos = toCells(lat, lon);
            e.hasPos = true;
        }
        return expect('}');
    }

private:
    unsigned hex4()
    {
        unsigned v = 0;
        for (int i = 0; i < 4 && _p < _end; i++) {
            const char c = *_p++;
            v <<= 4;
            if (c >= '0' && c <= '9')
                v |= (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f')
                v |= (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                v |= (unsigned)(c - 'A' + 10);
        }
        return v;
    }

    static void utf8(std::string& out, unsigned cp)
    {
        if (cp < 0x80) {
            out += (char)cp;
        } else if (cp < 0x800) {
            out += (char)(0xc0 | (cp >> 6));
            out += (char)(0x80 | (cp & 0x3f));
        } else if (cp < 0x10000) {
            out += (char)(0xe0 | (cp >> 12));
            out += (char)(0x80 | ((cp >> 6) & 0x3f));
            out += (char)(0x80 | (cp & 0x3f));
        } else {
            out += (char)(0xf0 | (cp >> 18));
            out += (char)(0x80 | ((cp >> 12) & 0x3f));
            out += (char)(0x80 | ((cp >> 6) & 0x3f));
            out += (char)(0x80 | (cp & 0x3f));
        }
    }

    Pt toCells(double lat, double lon) const
    {
        return Pt{(float)((lonToWorldX(lon) - (double)_area.worldX) * 0.25),
                  (float)((latToWorldY(lat) - (double)_area.worldY) * 0.25)};
    }

    const char* _p;
    const char* _end;
    const Area& _area;
    bool _failed = false;
};

// ---------------------------------------------------------------- features

struct Thick {
    Line line;
    float width; // cells
};

struct Disc {
    Pt centre;
    float radius; // cells
};

// What is drawn, by kind; an area is its lines (one ring, or a relation's
// members), filled even-odd.
struct Features {
    std::vector<std::vector<Line>> cover, water, bridgeAreas, buildings;
    std::vector<Thick> coverLines, waterLines, bridgeLines;
    std::vector<Disc> trees;
    std::vector<Line> walls, fences, coast;
    // Gaps in walls and fences, and buildings' doors.
    std::vector<Pt> gates, entrances;
};

bool closedLine(const Line& l)
{
    return l.size() >= 4 && l.front().x == l.back().x && l.front().y == l.back().y;
}

bool allClosed(const std::vector<Line>& lines)
{
    if (lines.empty())
        return false;
    for (const Line& l : lines)
        if (!closedLine(l))
            return false;
    return true;
}

bool oneOf(const std::string* value, const char* const* set)
{
    if (value == nullptr)
        return false;
    for (; *set; set++)
        if (*value == *set)
            return true;
    return false;
}

// A tag's leading number (metres, as "width" and "diameter_crown" are), or
// fallback.
float metres(const Element& e, const char* key, float fallback)
{
    const std::string* t = e.tag(key);
    if (t == nullptr)
        return fallback;
    const float v = (float)std::strtod(t->c_str(), nullptr);
    return v > 0.0f && v < 500.0f ? v : fallback;
}

const char* const kSolidBarriers[] = {"wall", "city_wall", "hedge", "hedge_bank", "noise_barrier", "flood_wall",
                                      nullptr};
const char* const kSeeThroughBarriers[] = {"fence",          "railing",       "guard_rail", "handrail",
                                           "chain",          "retaining_wall", "jersey_barrier", "cable_barrier",
                                           "wire_fence",     "bars",          nullptr};
const char* const kGates[] = {"gate",        "lift_gate",    "swing_gate",     "kissing_gate", "stile",
                              "entrance",    "turnstile",    "sliding_gate",   "hampshire_gate", "bump_gate",
                              "wicket_gate", "cattle_grid",  "motorcycle_barrier", "chicane", "cycle_barrier",
                              nullptr};
const char* const kWaterways[] = {"river", "stream", "canal", "drain", "ditch", nullptr};

// Metres across a bridge, by its highway (or the width tag).
float bridgeWidth(const Element& e)
{
    const std::string* h = e.tag("highway");
    float w = 6.0f;
    if (h != nullptr) {
        if (*h == "motorway" || *h == "trunk")
            w = 18.0f;
        else if (*h == "primary")
            w = 12.0f;
        else if (*h == "secondary")
            w = 10.0f;
        else if (*h == "tertiary" || *h == "residential" || *h == "unclassified" || *h == "living_street")
            w = 8.0f;
        else if (*h == "footway" || *h == "path" || *h == "cycleway" || *h == "steps" || *h == "bridleway" ||
                 *h == "pedestrian")
            w = 3.0f;
    }
    return metres(e, "width", w);
}

float waterwayWidth(const Element& e, const std::string& kind)
{
    const float w = kind == "river" ? 12.0f : kind == "canal" ? 8.0f : kind == "stream" ? 3.0f : 2.0f;
    return metres(e, "width", w);
}

void classify(const Element& e, float cellsPerMetre, Features& f)
{
    if (e.type == Element::Node) {
        if (!e.hasPos)
            return;
        if (e.is("natural", "tree")) {
            const float radius = metres(e, "diameter_crown", 6.0f) * 0.5f * cellsPerMetre;
            f.trees.push_back(Disc{e.pos, std::max(radius, 1.0f)});
        }
        if (oneOf(e.tag("barrier"), kGates))
            f.gates.push_back(e.pos);
        const std::string* entrance = e.tag("entrance");
        if (entrance != nullptr && *entrance != "no")
            f.entrances.push_back(e.pos);
        return;
    }
    if (e.lines.empty())
        return;
    const bool relation = e.type == Element::Relation;
    if (relation && !e.is("type", "multipolygon"))
        return;
    const bool area = relation || allClosed(e.lines);

    // Buildings that stand: not "no", not open roofs, not underground.
    const std::string* building = e.tag("building");
    if (building != nullptr && *building != "no" && *building != "roof" && area &&
        !e.is("location", "underground")) {
        const std::string* layer = e.tag("layer");
        if (layer == nullptr || std::atoi(layer->c_str()) >= 0)
            f.buildings.push_back(e.lines);
    }

    const std::string* natural = e.tag("natural");
    const std::string* landuse = e.tag("landuse");
    const std::string* waterway = e.tag("waterway");
    if (area) {
        if ((natural && (*natural == "wood" || *natural == "scrub")) || (landuse && *landuse == "forest"))
            f.cover.push_back(e.lines);
        if ((natural && *natural == "water") || e.tag("water") != nullptr ||
            (landuse && (*landuse == "reservoir" || *landuse == "basin")) || e.is("leisure", "swimming_pool") ||
            (waterway && (*waterway == "riverbank" || *waterway == "dock")))
            f.water.push_back(e.lines);
    }
    if (relation)
        return;

    for (const Line& line : e.lines) {
        if (natural && *natural == "tree_row")
            f.coverLines.push_back(Thick{line, 6.0f * cellsPerMetre});
        if (natural && *natural == "coastline")
            f.coast.push_back(line);
        if (oneOf(waterway, kWaterways)) {
            const std::string* tunnel = e.tag("tunnel");
            if (tunnel == nullptr || *tunnel == "no")
                f.waterLines.push_back(Thick{line, waterwayWidth(e, *waterway) * cellsPerMetre});
        }
        const std::string* barrier = e.tag("barrier");
        if (oneOf(barrier, kSolidBarriers))
            f.walls.push_back(line);
        else if (oneOf(barrier, kSeeThroughBarriers))
            f.fences.push_back(line);
        const std::string* bridge = e.tag("bridge");
        if (e.tag("highway") != nullptr && bridge != nullptr && *bridge != "no")
            f.bridgeLines.push_back(Thick{line, bridgeWidth(e) * cellsPerMetre});
        const std::string* manMade = e.tag("man_made");
        if (manMade && (*manMade == "bridge" || *manMade == "pier") && !area)
            f.bridgeLines.push_back(Thick{line, metres(e, "width", *manMade == "pier" ? 4.0f : 6.0f) * cellsPerMetre});
    }
    const std::string* manMade = e.tag("man_made");
    if (manMade && (*manMade == "bridge" || *manMade == "pier") && area)
        f.bridgeAreas.push_back(e.lines);
}

// ---------------------------------------------------------------- drawing

class Grid {
public:
    Grid(int w, int h, std::vector<u8>& cells) : w(w), h(h), c(cells) { c.assign((size_t)w * (size_t)h, kOpen); }

    bool in(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h; }
    u8& at(int x, int y) { return c[(size_t)y * (size_t)w + (size_t)x]; }
    // The cell under p, or 0xff off the layer.
    u8 probe(Pt p)
    {
        const int x = (int)std::floor(p.x), y = (int)std::floor(p.y);
        return in(x, y) ? at(x, y) : 0xff;
    }

    const int w, h;
    std::vector<u8>& c;
};

// Calls f(cell) for each cell whose centre is inside the lines, even-odd.
template <class F>
void fillArea(Grid& g, const std::vector<Line>& lines, F f)
{
    float minY = 1e30f, maxY = -1e30f;
    for (const Line& l : lines)
        for (const Pt& p : l) {
            minY = std::min(minY, p.y);
            maxY = std::max(maxY, p.y);
        }
    const int y0 = std::max(0, (int)std::floor(minY));
    const int y1 = std::min(g.h - 1, (int)std::ceil(maxY));
    std::vector<float> xs;
    for (int y = y0; y <= y1; y++) {
        const float cy = (float)y + 0.5f;
        xs.clear();
        for (const Line& l : lines)
            for (size_t i = 0; i + 1 < l.size(); i++) {
                const Pt& a = l[i];
                const Pt& b = l[i + 1];
                if ((a.y <= cy) != (b.y <= cy))
                    xs.push_back(a.x + (cy - a.y) * (b.x - a.x) / (b.y - a.y));
            }
        std::sort(xs.begin(), xs.end());
        for (size_t k = 0; k + 1 < xs.size(); k += 2) {
            const int x0 = std::max(0, (int)std::ceil(xs[k] - 0.5f));
            const int x1 = std::min(g.w - 1, (int)std::floor(xs[k + 1] - 0.5f));
            for (int x = x0; x <= x1; x++)
                f(g.at(x, y));
        }
    }
}

// Calls f(cell) for each cell the segment passes through, in order: a
// 4-connected chain, so nothing slips between its cells diagonally.
template <class F>
void traceSegment(Grid& g, Pt a, Pt b, F f)
{
    // Clipped to the layer (Liang-Barsky).
    double t0 = 0.0, t1 = 1.0;
    const double dx = (double)b.x - (double)a.x, dy = (double)b.y - (double)a.y;
    const double p[4] = {-dx, dx, -dy, dy};
    const double q[4] = {(double)a.x, (double)g.w - (double)a.x, (double)a.y, (double)g.h - (double)a.y};
    for (int i = 0; i < 4; i++) {
        if (p[i] == 0.0) {
            if (q[i] < 0.0)
                return;
            continue;
        }
        const double r = q[i] / p[i];
        if (p[i] < 0.0)
            t0 = std::max(t0, r);
        else
            t1 = std::min(t1, r);
        if (t0 > t1)
            return;
    }
    const double ax = (double)a.x + t0 * dx, ay = (double)a.y + t0 * dy;
    const double bx = (double)a.x + t1 * dx, by = (double)a.y + t1 * dy;
    auto clampCell = [](double v, int n) { return std::max(0, std::min(n - 1, (int)std::floor(v))); };
    int x = clampCell(ax, g.w), y = clampCell(ay, g.h);
    const int ex = clampCell(bx, g.w), ey = clampCell(by, g.h);
    const int sx = dx > 0.0 ? 1 : -1, sy = dy > 0.0 ? 1 : -1;
    const double inf = 1e30;
    double tMaxX = dx != 0.0 ? (sx > 0 ? (double)(x + 1) - ax : ax - (double)x) / std::fabs(dx) : inf;
    double tMaxY = dy != 0.0 ? (sy > 0 ? (double)(y + 1) - ay : ay - (double)y) / std::fabs(dy) : inf;
    const double tDeltaX = dx != 0.0 ? 1.0 / std::fabs(dx) : inf;
    const double tDeltaY = dy != 0.0 ? 1.0 / std::fabs(dy) : inf;
    f(g.at(x, y));
    for (int steps = std::abs(ex - x) + std::abs(ey - y); steps > 0; steps--) {
        if (tMaxX < tMaxY) {
            tMaxX += tDeltaX;
            x += sx;
        } else {
            tMaxY += tDeltaY;
            y += sy;
        }
        if (!g.in(x, y))
            break;
        f(g.at(x, y));
    }
}

template <class F>
void traceLine(Grid& g, const Line& l, F f)
{
    for (size_t i = 0; i + 1 < l.size(); i++)
        traceSegment(g, l[i], l[i + 1], f);
}

template <class F>
void fillDisc(Grid& g, Pt c, float r, F f)
{
    const int y0 = std::max(0, (int)std::floor(c.y - r)), y1 = std::min(g.h - 1, (int)std::ceil(c.y + r));
    const int x0 = std::max(0, (int)std::floor(c.x - r)), x1 = std::min(g.w - 1, (int)std::ceil(c.x + r));
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            const float ddx = (float)x + 0.5f - c.x, ddy = (float)y + 0.5f - c.y;
            if (ddx * ddx + ddy * ddy <= r * r)
                f(g.at(x, y));
        }
}

// A line width cells across, with round joins and ends; a thin one as a
// chain of cells.
template <class F>
void fillThick(Grid& g, const Thick& t, F f)
{
    if (t.width < 1.5f) {
        traceLine(g, t.line, f);
        return;
    }
    const float half = t.width * 0.5f;
    for (size_t i = 0; i + 1 < t.line.size(); i++) {
        const Pt a = t.line[i], b = t.line[i + 1];
        const float dx = b.x - a.x, dy = b.y - a.y;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len <= 0.0f)
            continue;
        const float nx = -dy / len * half, ny = dx / len * half;
        const std::vector<Line> quad = {
            Line{{a.x + nx, a.y + ny}, {b.x + nx, b.y + ny}, {b.x - nx, b.y - ny}, {a.x - nx, a.y - ny}, {a.x + nx, a.y + ny}}};
        fillArea(g, quad, f);
    }
    for (const Pt& p : t.line)
        fillDisc(g, p, half, f);
}

// The sea: the coastline has the land on its left, so of the pieces it cuts
// the layer into, those with more of its right side than its left are water.
void fillSea(Grid& g, const std::vector<Line>& coast)
{
    if (coast.empty())
        return;
    const size_t n = (size_t)g.w * (size_t)g.h;
    // 0 free, 1 the coastline; then each free piece's number from 2.
    std::vector<int> label(n, 0);
    for (const Line& l : coast)
        for (size_t i = 0; i + 1 < l.size(); i++) {
            // traceSegment hands out cells; find each one's index.
            traceSegment(g, l[i], l[i + 1], [&](u8& cell) { label[(size_t)(&cell - g.c.data())] = 1; });
        }
    std::vector<int> stack;
    int pieces = 0;
    for (size_t i = 0; i < n; i++) {
        if (label[i] != 0)
            continue;
        const int id = 2 + pieces++;
        label[i] = id;
        stack.push_back((int)i);
        while (!stack.empty()) {
            const int c = stack.back();
            stack.pop_back();
            const int x = c % g.w, y = c / g.w;
            const int next[4] = {x > 0 ? c - 1 : -1, x + 1 < g.w ? c + 1 : -1, y > 0 ? c - g.w : -1,
                                 y + 1 < g.h ? c + g.w : -1};
            for (int k : next)
                if (k >= 0 && label[(size_t)k] == 0) {
                    label[(size_t)k] = id;
                    stack.push_back(k);
                }
        }
    }
    // Votes: a point just right of each segment's middle is the sea's, just
    // left the land's.
    std::vector<int> votes((size_t)pieces, 0);
    for (const Line& l : coast)
        for (size_t i = 0; i + 1 < l.size(); i++) {
            const Pt a = l[i], b = l[i + 1];
            const float dx = b.x - a.x, dy = b.y - a.y;
            const float len = std::sqrt(dx * dx + dy * dy);
            if (len <= 0.0f)
                continue;
            // Right of the way's direction, with y down: (-dy, dx).
            const float rx = -dy / len * 1.5f, ry = dx / len * 1.5f;
            const Pt mid = {(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
            for (int side = 1; side >= -1; side -= 2) {
                const int x = (int)std::floor(mid.x + rx * (float)side);
                const int y = (int)std::floor(mid.y + ry * (float)side);
                if (!g.in(x, y))
                    continue;
                const int id = label[(size_t)y * (size_t)g.w + (size_t)x];
                if (id >= 2)
                    votes[(size_t)(id - 2)] += side;
            }
        }
    for (size_t i = 0; i < n; i++)
        if (label[i] >= 2 && votes[(size_t)(label[i] - 2)] > 0)
            g.c[i] = kWater;
}

struct Edge {
    Pt a, b;
    float length;
};

// Doors and windows go only where a wall has a building's inside on one side
// and somewhere to go on the other. Looks 2 cells either side of p.
bool facesOut(Grid& g, Pt p, Pt normal, bool door)
{
    const u8 s1 = g.probe(Pt{p.x + normal.x * 2.0f, p.y + normal.y * 2.0f});
    const u8 s2 = g.probe(Pt{p.x - normal.x * 2.0f, p.y - normal.y * 2.0f});
    auto open = [door](u8 v) { return door ? (v == kOpen || v == kCover) : (v == kOpen || v == kCover || v == kWater); };
    return (s1 == kInside && open(s2)) || (s2 == kInside && open(s1));
}

// Replaces kWall with value along the edge from t0 to t1 cells.
void openWall(Grid& g, const Edge& e, float t0, float t1, u8 value)
{
    const float ux = (e.b.x - e.a.x) / e.length, uy = (e.b.y - e.a.y) / e.length;
    t0 = std::max(t0, 0.0f);
    t1 = std::min(t1, e.length);
    if (t1 <= t0)
        return;
    traceSegment(g, Pt{e.a.x + ux * t0, e.a.y + uy * t0}, Pt{e.a.x + ux * t1, e.a.y + uy * t1}, [value](u8& v) {
        if (v == kWall)
            v = value;
    });
}

void drawBuildings(Grid& g, const Features& f, float cellsPerMetre)
{
    // Insides first, then every wall, so walls shared by two buildings stay.
    for (const auto& b : f.buildings)
        fillArea(g, b, [](u8& v) { v = kInside; });
    for (const auto& b : f.buildings)
        for (const Line& l : b)
            traceLine(g, l, [](u8& v) { v = kWall; });

    const float doorHalf = std::max(1.0f, 1.0f * cellsPerMetre);
    const float windowLength = std::max(2.0f, 1.2f * cellsPerMetre);
    const float windowGap = std::max(3.0f, 2.5f * cellsPerMetre);
    std::vector<Edge> edges;
    for (const auto& b : f.buildings) {
        edges.clear();
        float perimeter = 0.0f;
        for (const Line& l : b)
            for (size_t i = 0; i + 1 < l.size(); i++) {
                const float dx = l[i + 1].x - l[i].x, dy = l[i + 1].y - l[i].y;
                const float len = std::sqrt(dx * dx + dy * dy);
                if (len > 0.0f) {
                    edges.push_back(Edge{l[i], l[i + 1], len});
                    perimeter += len;
                }
            }
        if (edges.empty())
            continue;

        // Doors at the building's mapped entrances.
        int doors = 0;
        for (const Pt& p : f.entrances) {
            for (const Edge& e : edges) {
                const float ux = (e.b.x - e.a.x) / e.length, uy = (e.b.y - e.a.y) / e.length;
                const float t = (p.x - e.a.x) * ux + (p.y - e.a.y) * uy;
                if (t < -0.5f || t > e.length + 0.5f)
                    continue;
                const float off = (p.x - e.a.x) * -uy + (p.y - e.a.y) * ux;
                if (std::fabs(off) > 1.5f)
                    continue;
                openWall(g, e, t - doorHalf, t + doorHalf, kDoor);
                doors++;
                break;
            }
        }
        // Otherwise the middle of the longest wall that faces somewhere to go,
        // and a second, well away from it, on a big building.
        if (doors == 0) {
            std::vector<const Edge*> byLength;
            for (const Edge& e : edges)
                if (e.length >= doorHalf * 2.0f + 2.0f)
                    byLength.push_back(&e);
            std::sort(byLength.begin(), byLength.end(),
                      [](const Edge* x, const Edge* y) { return x->length > y->length; });
            const int wanted = perimeter > 80.0f * cellsPerMetre ? 2 : 1;
            Pt first = {0.0f, 0.0f};
            for (const Edge* e : byLength) {
                const Pt mid = {(e->a.x + e->b.x) * 0.5f, (e->a.y + e->b.y) * 0.5f};
                const Pt normal = {-(e->b.y - e->a.y) / e->length, (e->b.x - e->a.x) / e->length};
                if (!facesOut(g, mid, normal, true))
                    continue;
                if (doors == 1) {
                    const float dx = mid.x - first.x, dy = mid.y - first.y;
                    if (dx * dx + dy * dy < 900.0f)
                        continue;
                }
                openWall(g, *e, e->length * 0.5f - doorHalf, e->length * 0.5f + doorHalf, kDoor);
                first = mid;
                if (++doors >= wanted)
                    break;
            }
        }

        // Windows along the walls that face out, clear of the corners.
        for (const Edge& e : edges) {
            const float margin = windowGap * 0.75f;
            if (e.length < margin * 2.0f + windowLength)
                continue;
            const float ux = (e.b.x - e.a.x) / e.length, uy = (e.b.y - e.a.y) / e.length;
            const Pt normal = {-uy, ux};
            // Spread evenly between the margins.
            const float span = e.length - margin * 2.0f;
            const int count = std::max(1, (int)((span + windowGap) / (windowLength + windowGap)));
            const float step = count > 1 ? (span - windowLength) / (float)(count - 1) : 0.0f;
            const float start = count > 1 ? margin : (e.length - windowLength) * 0.5f;
            for (int i = 0; i < count; i++) {
                const float t = start + step * (float)i;
                const Pt mid = {e.a.x + ux * (t + windowLength * 0.5f), e.a.y + uy * (t + windowLength * 0.5f)};
                if (facesOut(g, mid, normal, false))
                    openWall(g, e, t, t + windowLength, kWindow);
            }
        }
    }
}

// A wall the connecting pass may put a doorway through (not water, not cars).
bool passable(u8 v)
{
    return v == kWall || v == kWindow || v == kFence;
}

// Nobody shut in: OpenStreetMap leaves out most doors between rooms and row
// houses and most gates in fences, so a building in the middle of a block, or
// a courtyard, can end up closed off. Where two walkable pieces are a wall
// (one or two cells, as a traced diagonal is) apart, the wall gets a doorway,
// one per pair of pieces until they are all joined. What is still closed off
// after that and small (a fountain's middle, a gap between two buildings) is
// filled in.
void connect(Grid& g)
{
    const int w = g.w, h = g.h;
    const size_t n = (size_t)w * (size_t)h;
    std::vector<int> piece(n, -1);
    std::vector<int> sizes;
    std::vector<int> stack;
    for (size_t i = 0; i < n; i++) {
        if (piece[i] >= 0 || !(g.c[i] & 1))
            continue;
        const int id = (int)sizes.size();
        int size = 0;
        piece[i] = id;
        stack.push_back((int)i);
        while (!stack.empty()) {
            const int c = stack.back();
            stack.pop_back();
            size++;
            const int x = c % w, y = c / w;
            const int next[4] = {x > 0 ? c - 1 : -1, x + 1 < w ? c + 1 : -1, y > 0 ? c - w : -1, y + 1 < h ? c + w : -1};
            for (int k : next)
                if (k >= 0 && piece[(size_t)k] < 0 && (g.c[(size_t)k] & 1)) {
                    piece[(size_t)k] = id;
                    stack.push_back(k);
                }
        }
        sizes.push_back(size);
    }
    if (sizes.size() < 2)
        return;

    std::vector<int> parent(sizes.size());
    for (size_t i = 0; i < parent.size(); i++)
        parent[i] = (int)i;
    auto root = [&parent](int a) {
        while (parent[(size_t)a] != a) {
            parent[(size_t)a] = parent[(size_t)parent[(size_t)a]];
            a = parent[(size_t)a];
        }
        return a;
    };

    // Along rows (step 1) and then columns (step w): from a walkable cell over
    // one or two wall cells to a walkable cell of another piece.
    for (int pass = 0; pass < 2; pass++) {
        const int step = pass == 0 ? 1 : w;
        const int lines = pass == 0 ? h : w, length = pass == 0 ? w : h;
        // Across the wall, for widening the doorway along it.
        const int side = pass == 0 ? w : 1;
        for (int line = 0; line < lines; line++) {
            const int first = pass == 0 ? line * w : line;
            for (int i = 0; i + 2 < length; i++) {
                const int a = first + i * step;
                if (piece[(size_t)a] < 0 || piece[(size_t)(a + step)] >= 0 || !passable(g.c[(size_t)(a + step)]))
                    continue;
                int k = 1;
                if (i + 3 < length && piece[(size_t)(a + 2 * step)] < 0 && passable(g.c[(size_t)(a + 2 * step)]))
                    k = 2;
                const int b = a + (k + 1) * step;
                if (piece[(size_t)b] < 0)
                    continue;
                const int ra = root(piece[(size_t)a]), rb = root(piece[(size_t)b]);
                if (ra == rb)
                    continue;
                parent[(size_t)ra] = rb;
                // The doorway: these cells, and the ones beside them along the
                // wall where the crossing is the same.
                const int along = line;
                for (int d = -1; d <= 1; d++) {
                    if (along + d < 0 || along + d >= lines)
                        continue;
                    const int a2 = a + d * side, b2 = b + d * side;
                    bool ok = d == 0 || (piece[(size_t)a2] >= 0 && piece[(size_t)b2] >= 0 &&
                                         root(piece[(size_t)a2]) == rb && root(piece[(size_t)b2]) == rb);
                    for (int j = 1; ok && j <= k; j++)
                        ok = d == 0 || passable(g.c[(size_t)(a2 + j * step)]);
                    if (!ok)
                        continue;
                    for (int j = 1; j <= k; j++) {
                        g.c[(size_t)(a2 + j * step)] = kDoor;
                        piece[(size_t)(a2 + j * step)] = piece[(size_t)a];
                    }
                }
            }
        }
    }

    // What is left apart from the biggest: filled in if small.
    std::vector<int> joined(sizes.size(), 0);
    for (size_t i = 0; i < sizes.size(); i++)
        joined[(size_t)root((int)i)] += sizes[i];
    const int biggest = (int)(std::max_element(joined.begin(), joined.end()) - joined.begin());
    for (size_t i = 0; i < n; i++)
        if (piece[i] >= 0) {
            const int r = root(piece[i]);
            if (r != biggest && joined[(size_t)r] < 64)
                g.c[i] = kWall;
        }
}

} // namespace

void areaBounds(const Area& area, double& south, double& west, double& north, double& east)
{
    west = worldXToLon((double)area.worldX);
    east = worldXToLon((double)(area.worldX + area.width));
    north = worldYToLat((double)area.worldY);
    south = worldYToLat((double)(area.worldY + area.height));
}

std::string overpassQuery(const Area& area)
{
    double s, w, n, e;
    areaBounds(area, s, w, n, e);
    char bbox[96];
    snprintf(bbox, sizeof(bbox), "%.7f,%.7f,%.7f,%.7f", s, w, n, e);
    return std::string("[out:json][timeout:90][bbox:") + bbox +
           "];\n"
           "(\n"
           "  way[building];\n"
           "  relation[building][type=multipolygon];\n"
           "  way[barrier];\n"
           "  node[barrier];\n"
           "  node[entrance];\n"
           "  node[natural=tree];\n"
           "  way[natural~\"^(wood|tree_row|scrub|water|coastline)$\"];\n"
           "  relation[natural~\"^(wood|scrub|water)$\"];\n"
           "  way[landuse~\"^(forest|reservoir|basin)$\"];\n"
           "  relation[landuse~\"^(forest|reservoir|basin)$\"];\n"
           "  way[waterway];\n"
           "  relation[waterway=riverbank];\n"
           "  way[water];\n"
           "  relation[water];\n"
           "  way[leisure=swimming_pool];\n"
           "  way[highway][bridge];\n"
           "  way[man_made~\"^(pier|bridge)$\"];\n"
           ");\n"
           "out geom;\n";
}

bool buildCollisionLayer(const Area& area, const char* json, size_t size, std::vector<u8>& cells, std::string& error)
{
    const double midLat = worldYToLat((double)area.worldY + (double)area.height * 0.5);
    const float cellsPerMetre = (float)(1.0 / (kEquatorMetresPerPixel * 4.0 * std::cos(midLat * kPi / 180.0)));

    // Overpass's answer: {"version":..., ..., "remark": "...", "elements": [...]}.
    Features f;
    Reader r(json, size, area);
    std::string key, remark;
    int elements = 0;
    bool ok = r.expect('{');
    if (ok && !r.eat('}')) {
        do {
            if (!r.string(key) || !r.expect(':')) {
                ok = false;
                break;
            }
            if (key == "elements") {
                if (!r.expect('[')) {
                    ok = false;
                    break;
                }
                if (!r.eat(']')) {
                    Element e;
                    do {
                        if (!r.element(e)) {
                            ok = false;
                            break;
                        }
                        classify(e, cellsPerMetre, f);
                        elements++;
                    } while (r.eat(','));
                    if (!ok || !r.expect(']')) {
                        ok = false;
                        break;
                    }
                }
            } else if (key == "remark") {
                if (!r.string(remark)) {
                    ok = false;
                    break;
                }
            } else if (!r.skip()) {
                ok = false;
                break;
            }
        } while (r.eat(','));
        ok = ok && r.expect('}');
    }
    if (!ok) {
        char text[96];
        snprintf(text, sizeof(text), "unreadable map data at byte %u", (unsigned)r.offset(json));
        error = text;
        return false;
    }
    // A timeout or an overload comes back as a remark, with what was found
    // by then.
    if (remark.find("error") != std::string::npos) {
        error = remark;
        return false;
    }

    Grid g(area.width / 4, area.height / 4, cells);
    for (const auto& a : f.cover)
        fillArea(g, a, [](u8& v) { v = kCover; });
    for (const Thick& t : f.coverLines)
        fillThick(g, t, [](u8& v) { v = kCover; });
    for (const Disc& d : f.trees)
        fillDisc(g, d.centre, d.radius, [](u8& v) { v = kCover; });
    for (const auto& a : f.water)
        fillArea(g, a, [](u8& v) { v = kWater; });
    for (const Thick& t : f.waterLines)
        fillThick(g, t, [](u8& v) { v = kWater; });
    fillSea(g, f.coast);
    for (const auto& a : f.bridgeAreas)
        fillArea(g, a, [](u8& v) { v = kOpen; });
    for (const Thick& t : f.bridgeLines)
        fillThick(g, t, [](u8& v) { v = kOpen; });
    for (const Line& l : f.walls)
        traceLine(g, l, [](u8& v) { v = kWall; });
    for (const Line& l : f.fences)
        traceLine(g, l, [](u8& v) { v = kFence; });
    // Gates (and entrances in walls): a gap the gate's width.
    const float gateRadius = std::max(1.5f, 1.5f * cellsPerMetre);
    for (const std::vector<Pt>* points : {&f.gates, &f.entrances})
        for (const Pt& p : *points)
            fillDisc(g, p, gateRadius, [](u8& v) {
                if (v == kWall || v == kFence)
                    v = kOpen;
            });
    drawBuildings(g, f, cellsPerMetre);
    connect(g);
    return true;
}

void addImageryCover(const std::vector<u8>& rgba, int width, int height, std::vector<u8>& cells)
{
    const size_t n = (size_t)width * (size_t)height;
    if (rgba.size() < n * 4 || cells.size() != n)
        return;
    // Summed green pixels, (width + 1) x (height + 1).
    std::vector<int> sum((size_t)(width + 1) * (size_t)(height + 1), 0);
    for (int y = 0; y < height; y++) {
        int row = 0;
        for (int x = 0; x < width; x++) {
            const u8* p = &rgba[((size_t)y * (size_t)width + (size_t)x) * 4];
            const int r = p[0], gr = p[1], b = p[2];
            row += gr >= r && gr >= b + 15 && r + gr + b < 390 ? 1 : 0;
            sum[(size_t)(y + 1) * (size_t)(width + 1) + (size_t)(x + 1)] =
                sum[(size_t)y * (size_t)(width + 1) + (size_t)(x + 1)] + row;
        }
    }
    const int radius = 2;
    for (int y = 0; y < height; y++) {
        const int y0 = std::max(0, y - radius), y1 = std::min(height, y + radius + 1);
        for (int x = 0; x < width; x++) {
            u8& v = cells[(size_t)y * (size_t)width + (size_t)x];
            if (v != kOpen)
                continue;
            const int x0 = std::max(0, x - radius), x1 = std::min(width, x + radius + 1);
            const size_t W = (size_t)(width + 1);
            const int green = sum[(size_t)y1 * W + (size_t)x1] - sum[(size_t)y0 * W + (size_t)x1] -
                              sum[(size_t)y1 * W + (size_t)x0] + sum[(size_t)y0 * W + (size_t)x0];
            if (green * 2 >= (y1 - y0) * (x1 - x0))
                v = kCover;
        }
    }
}

float openShare(const std::vector<u8>& cells)
{
    if (cells.empty())
        return 0.0f;
    size_t open = 0;
    for (u8 v : cells)
        if (v == kOpen || v == kCover)
            open++;
    return (float)open / (float)cells.size();
}

} // namespace osm
