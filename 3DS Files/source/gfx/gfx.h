// The port's renderer: citro3d, one vertex shader (zos.v.pica), and a
// per-frame vertex buffer that every draw appends to.
//
// The game drew through two layers: cocos2d-x's renderer for its scenes and
// GUI, and its own GL code for the map and the simulation (MapRenderer,
// SimulationRenderer, EffectsRenderer, ...), which hands GL positions in
// normalized device coordinates. Both come here. A draw is a run of
// triangles in the current state (texture, combiner, blend, projection,
// scissor); changing the state flushes the run before it.
//
// Colours are premultiplied, as the game's are: the default blend is
// ONE, ONE_MINUS_SRC_ALPHA.
#pragma once

#include <3ds.h>
#include <citro3d.h>

#include <vector>

namespace gfx {

struct Vertex {
    float x, y;
    // Image space: (0, 0) is the image's top left, (1, 1) its bottom right.
    float u, v;
    // r | g << 8 | b << 16 | a << 24, premultiplied.
    u32 color;
};

inline u32 rgba(u8 r, u8 g, u8 b, u8 a)
{
    return r | g << 8 | b << 16 | (u32)a << 24;
}

// A colour premultiplied by its alpha.
inline u32 premul(u8 r, u8 g, u8 b, u8 a)
{
    return rgba(r * a / 255, g * a / 255, b * a / 255, a);
}

// Every channel, alpha included, scaled by a: what the game's
// "gl_FragColor *= alpha" shaders do to a premultiplied texel.
inline u32 alphaColor(float a)
{
    int v = a <= 0.0f ? 0 : a >= 1.0f ? 255 : (int)(a * 255.0f + 0.5f);
    return rgba(v, v, v, v);
}

const u32 kWhite = 0xffffffff;

struct Texture {
    C3D_Tex tex = {};
    // The image's size in pixels, not the power of two it is padded to.
    int width = 0;
    int height = 0;
    // Image space -> texture space: uv * scale + offset.
    float uvScaleX = 1.0f, uvScaleY = 1.0f, uvOffsetX = 0.0f, uvOffsetY = 0.0f;
    bool valid = false;
};

enum class Screen { Top, Bottom };

enum class Combine {
    // texture * colour: Map, CollisionLayer, EffectDecal, EffectAlphaMult,
    // Person, PersonShadow, and every cocos2d sprite.
    Modulate,
    // The colour alone: BoundaryLine, ExteriorFill, PersonMarker, DrawNode.
    Color,
    // An alpha-only texture (a font's glyph sheet): rgb and a both scaled by
    // the texel's alpha.
    AlphaMask,
};

bool init();
void exit();

// Once per frame, around every screen's drawing.
void frameBegin();
void frameEnd();

// Starts drawing to a screen, cleared to an rgba() colour. The projection is
// set to the screen's pixels (ortho(0, w, 0, h): y up, as cocos2d has it)
// and the scissor is off.
void beginScreen(Screen screen, u32 clearColor);
int screenWidth(Screen screen);
const int kScreenHeight = 240;

// The projection, as cocos2d's ortho: x left to right, y bottom to top.
void ortho(float left, float right, float bottom, float top);
// GL's normalized device coordinates, -1..1 on both axes, for the game's
// own renderers.
void projectionNDC();

// nullptr or an invalid texture draws untextured.
void bindTexture(const Texture* texture, Combine combine = Combine::Modulate);
// A raw texture (a font sheet) whose coordinates are already texture space.
void bindRawTexture(C3D_Tex* tex, Combine combine);

void blend(GPU_BLENDFACTOR src, GPU_BLENDFACTOR dst);
void blendPremultiplied();

// In the current screen's pixels, from its top left.
void scissor(int x, int y, int w, int h);
void scissorOff();

// Room for count vertices (a multiple of 3: triangle lists) in the current
// state, or nullptr when the frame's buffer is full.
Vertex* triangles(int count);
// Two triangles for an axis-aligned quad. (x0, y0) and (x1, y1) are opposite
// corners in the projection's units; the uvs likewise.
void quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, u32 color);
void flush();

// Textures. None of these is thread-safe: they take linear memory, whose
// allocator has no lock. Loaders on other threads read the file and leave
// the texture to the main thread.
bool loadTexture(Texture* out, const char* path);
// A .t3x already read into memory.
bool loadTextureFromMemory(Texture* out, const void* data, size_t size);
// RGBA pixels, r in the first byte, rows top down. Any size up to 1024; it
// is padded to a power of two.
bool createTexture(Texture* out, const u8* rgba, int width, int height, bool linearFilter = true);
// A texture from data already in the GPU's order and format (8 x 8 tiles,
// each in Morton order, as jpeg::decodeTiledRGB565's): only a copy, so a
// loader thread can do the rest. width and height powers of two.
bool createTiledTexture(Texture* out, const void* data, size_t size, int width, int height, GPU_TEXCOLOR format,
                        bool linearFilter = true);
// Freed once the GPU is done with the frame that may still be drawing it.
void destroyTexture(Texture* texture);
void setTextureFilter(Texture* texture, bool linear);

// Fonts (.bcfnt).
struct Font {
    CFNT_s* cfnt = nullptr;
    C3D_Tex* sheets = nullptr;
    int sheetCount = 0;
    bool valid = false;
};
bool loadFont(Font* out, const char* path);
void freeFont(Font* font);

struct Stats {
    int vertices;
    int draws;
    int overflowed;
};
Stats lastFrameStats();

} // namespace gfx
