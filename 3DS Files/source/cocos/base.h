// The cocos2d-x 3.11 basics the game uses: Vec2, Size, Rect, the colour
// types, a 2D transform, and Ref's reference counting with its autorelease
// pool. Same names and semantics as cocos2d-x, so the ported code reads
// like the decompilation; only what the game calls is here.
#pragma once

#include <3ds/types.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace cocos2d {

struct Vec2 {
    float x = 0.0f, y = 0.0f;
    Vec2() = default;
    Vec2(float x_, float y_) : x(x_), y(y_) {}

    Vec2 operator+(const Vec2& o) const { return Vec2(x + o.x, y + o.y); }
    Vec2 operator-(const Vec2& o) const { return Vec2(x - o.x, y - o.y); }
    Vec2 operator-() const { return Vec2(-x, -y); }
    Vec2 operator*(float s) const { return Vec2(x * s, y * s); }
    Vec2 operator/(float s) const { return Vec2(x / s, y / s); }
    Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }
    Vec2& operator*=(float s) { x *= s; y *= s; return *this; }
    bool operator==(const Vec2& o) const { return x == o.x && y == o.y; }
    bool operator!=(const Vec2& o) const { return !(*this == o); }

    float length() const { return sqrtf(x * x + y * y); }
    float lengthSquared() const { return x * x + y * y; }
    float distance(const Vec2& o) const { return (*this - o).length(); }
    float distanceSquared(const Vec2& o) const { return (*this - o).lengthSquared(); }
    float dot(const Vec2& o) const { return x * o.x + y * o.y; }
    Vec2 getNormalized() const
    {
        float l = length();
        return l > 0.0f ? Vec2(x / l, y / l) : *this;
    }
    Vec2 getMidpoint(const Vec2& o) const { return Vec2((x + o.x) * 0.5f, (y + o.y) * 0.5f); }

    static const Vec2 ZERO;
    static const Vec2 ANCHOR_MIDDLE;
    static const Vec2 ANCHOR_BOTTOM_LEFT;
};
using Point = Vec2;

struct Size {
    float width = 0.0f, height = 0.0f;
    Size() = default;
    Size(float w, float h) : width(w), height(h) {}
    Size operator+(const Size& o) const { return Size(width + o.width, height + o.height); }
    Size operator-(const Size& o) const { return Size(width - o.width, height - o.height); }
    Size operator*(float s) const { return Size(width * s, height * s); }
    Size operator/(float s) const { return Size(width / s, height / s); }
    bool equals(const Size& o) const { return width == o.width && height == o.height; }
    bool operator==(const Size& o) const { return equals(o); }
    bool operator!=(const Size& o) const { return !equals(o); }
    static const Size ZERO;
};

struct Rect {
    Vec2 origin;
    Size size;
    Rect() = default;
    Rect(float x, float y, float w, float h) : origin(x, y), size(w, h) {}
    Rect(const Vec2& o, const Size& s) : origin(o), size(s) {}

    float getMinX() const { return origin.x; }
    float getMaxX() const { return origin.x + size.width; }
    float getMidX() const { return origin.x + size.width * 0.5f; }
    float getMinY() const { return origin.y; }
    float getMaxY() const { return origin.y + size.height; }
    float getMidY() const { return origin.y + size.height * 0.5f; }
    bool containsPoint(const Vec2& p) const
    {
        return p.x >= getMinX() && p.x <= getMaxX() && p.y >= getMinY() && p.y <= getMaxY();
    }
    bool intersectsRect(const Rect& r) const
    {
        return !(getMaxX() < r.getMinX() || r.getMaxX() < getMinX() || getMaxY() < r.getMinY() ||
                 r.getMaxY() < getMinY());
    }
    Rect unionWithRect(const Rect& r) const
    {
        float x0 = std::min(getMinX(), r.getMinX()), y0 = std::min(getMinY(), r.getMinY());
        float x1 = std::max(getMaxX(), r.getMaxX()), y1 = std::max(getMaxY(), r.getMaxY());
        return Rect(x0, y0, x1 - x0, y1 - y0);
    }
    static const Rect ZERO;
};

struct Color3B {
    u8 r = 0, g = 0, b = 0;
    Color3B() = default;
    Color3B(u8 r_, u8 g_, u8 b_) : r(r_), g(g_), b(b_) {}
    bool operator==(const Color3B& o) const { return r == o.r && g == o.g && b == o.b; }
    bool operator!=(const Color3B& o) const { return !(*this == o); }
    static const Color3B WHITE;
    static const Color3B BLACK;
};

struct Color4F;

struct Color4B {
    u8 r = 0, g = 0, b = 0, a = 0;
    Color4B() = default;
    Color4B(u8 r_, u8 g_, u8 b_, u8 a_) : r(r_), g(g_), b(b_), a(a_) {}
    explicit Color4B(const Color4F& c);
    static const Color4B WHITE;
    static const Color4B BLACK;
};

struct Color4F {
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
    Color4F() = default;
    Color4F(float r_, float g_, float b_, float a_) : r(r_), g(g_), b(b_), a(a_) {}
    explicit Color4F(const Color4B& c) : r(c.r / 255.0f), g(c.g / 255.0f), b(c.b / 255.0f), a(c.a / 255.0f) {}
    Color4F(const Color3B& c, float a_) : r(c.r / 255.0f), g(c.g / 255.0f), b(c.b / 255.0f), a(a_) {}
};

inline Color4B::Color4B(const Color4F& c)
    : r((u8)(c.r * 255.0f + 0.5f)), g((u8)(c.g * 255.0f + 0.5f)), b((u8)(c.b * 255.0f + 0.5f)),
      a((u8)(c.a * 255.0f + 0.5f))
{
}

// cocos2d hands a Mat4 to draw(). Nothing in the game is 3D, so here it is
// a 2D affine transform: x' = a x + c y + tx, y' = b x + d y + ty.
struct Mat4 {
    float a = 1.0f, b = 0.0f, c = 0.0f, d = 1.0f, tx = 0.0f, ty = 0.0f;

    Vec2 apply(const Vec2& p) const { return Vec2(a * p.x + c * p.y + tx, b * p.x + d * p.y + ty); }
    Vec2 applyVector(const Vec2& p) const { return Vec2(a * p.x + c * p.y, b * p.x + d * p.y); }
    // this * o: o first, then this.
    Mat4 operator*(const Mat4& o) const
    {
        Mat4 r;
        r.a = a * o.a + c * o.b;
        r.b = b * o.a + d * o.b;
        r.c = a * o.c + c * o.d;
        r.d = b * o.c + d * o.d;
        r.tx = a * o.tx + c * o.ty + tx;
        r.ty = b * o.tx + d * o.ty + ty;
        return r;
    }
    Mat4 inverse() const
    {
        float det = a * d - b * c;
        Mat4 r;
        if (det == 0.0f)
            return r;
        float inv = 1.0f / det;
        r.a = d * inv;
        r.b = -b * inv;
        r.c = -c * inv;
        r.d = a * inv;
        r.tx = (c * ty - d * tx) * inv;
        r.ty = (b * tx - a * ty) * inv;
        return r;
    }
    static const Mat4 IDENTITY;
};

class Ref {
public:
    virtual ~Ref();
    void retain() { ++_referenceCount; }
    void release();
    Ref* autorelease();
    unsigned getReferenceCount() const { return _referenceCount; }

protected:
    Ref() = default;
    unsigned _referenceCount = 1;
};

// Released at the end of every frame, as cocos2d's PoolManager does.
void drainAutoreleasePool();

#define CC_SAFE_RELEASE(p) do { if (p) (p)->release(); } while (0)
#define CC_SAFE_RELEASE_NULL(p) do { if (p) { (p)->release(); (p) = nullptr; } } while (0)
#define CC_SAFE_RETAIN(p) do { if (p) (p)->retain(); } while (0)

// cocos2d::Vector<T>: a vector of Refs it retains.
template <class T>
class Vector {
public:
    Vector() = default;
    Vector(const Vector& o) : _data(o._data) { for (T p : _data) p->retain(); }
    Vector& operator=(const Vector& o)
    {
        if (this != &o) {
            for (T p : o._data) p->retain();
            clear();
            _data = o._data;
        }
        return *this;
    }
    ~Vector() { clear(); }

    void pushBack(T p) { p->retain(); _data.push_back(p); }
    void eraseObject(T p)
    {
        auto it = std::find(_data.begin(), _data.end(), p);
        if (it != _data.end()) {
            _data.erase(it);
            p->release();
        }
    }
    void erase(size_t i)
    {
        T p = _data[i];
        _data.erase(_data.begin() + i);
        p->release();
    }
    void clear()
    {
        std::vector<T> old;
        old.swap(_data);
        for (T p : old) p->release();
    }
    bool contains(T p) const { return std::find(_data.begin(), _data.end(), p) != _data.end(); }
    int getIndex(T p) const
    {
        auto it = std::find(_data.begin(), _data.end(), p);
        return it == _data.end() ? -1 : (int)(it - _data.begin());
    }
    size_t size() const { return _data.size(); }
    bool empty() const { return _data.empty(); }
    T at(size_t i) const { return _data[i]; }
    T front() const { return _data.front(); }
    T back() const { return _data.back(); }
    T operator[](size_t i) const { return _data[i]; }
    typename std::vector<T>::const_iterator begin() const { return _data.begin(); }
    typename std::vector<T>::const_iterator end() const { return _data.end(); }
    std::vector<T>& raw() { return _data; }

private:
    std::vector<T> _data;
};

struct BlendFunc {
    int src, dst; // GPU_BLENDFACTOR values
    bool operator==(const BlendFunc& o) const { return src == o.src && dst == o.dst; }
    static const BlendFunc ALPHA_PREMULTIPLIED;
    static const BlendFunc ALPHA_NON_PREMULTIPLIED;
    static const BlendFunc ADDITIVE;
};

enum class TextHAlignment { LEFT, CENTER, RIGHT };
enum class TextVAlignment { TOP, CENTER, BOTTOM };

} // namespace cocos2d
