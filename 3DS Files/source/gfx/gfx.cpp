#include "gfx.h"

#include <tex3ds.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "debuglog.h"
#include "zos_shbin.h"

namespace gfx {
namespace {

// 96K vertices, 1.9 MB of linear memory. C3D_FrameBegin waits for the GPU
// to finish the last frame, so one buffer, refilled each frame, is enough.
const int kMaxVertices = 0x18000;

const u32 kTransferFlags =
    GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |
    GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) |
    GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);

DVLB_s* s_dvlb;
shaderProgram_s s_program;
int s_locProjection;
int s_locUvxform;
C3D_RenderTarget* s_targets[2];
Screen s_screen = Screen::Bottom;

Vertex* s_vbo;
int s_used;
int s_batchStart;
Stats s_stats;
Stats s_lastStats;

// The state the pending run is drawn in.
const C3D_Tex* s_tex;
Combine s_combine = Combine::Color;
bool s_scissorOn;

LightLock s_freeLock;
std::vector<C3D_Tex> s_pendingFree;

void applyCombine(Combine combine)
{
    C3D_TexEnv* env = C3D_GetTexEnv(0);
    C3D_TexEnvInit(env);
    switch (combine) {
    case Combine::Modulate:
        C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
        C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
        break;
    case Combine::Color:
        C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
        C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
        break;
    case Combine::AlphaMask:
        // rgb = colour.rgb * texel.a, a = colour.a * texel.a: the colour is
        // premultiplied, so the result stays premultiplied.
        C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, GPU_TEXTURE0, GPU_PRIMARY_COLOR);
        C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_ALPHA, GPU_TEVOP_RGB_SRC_COLOR);
        C3D_TexEnvOpAlpha(env, GPU_TEVOP_A_SRC_ALPHA, GPU_TEVOP_A_SRC_ALPHA, GPU_TEVOP_A_SRC_ALPHA);
        C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
        break;
    }
}

void setUvxform(float sx, float sy, float ox, float oy)
{
    C3D_FVUnifSet(GPU_VERTEX_SHADER, s_locUvxform, sx, sy, ox, oy);
}

void freePending()
{
    LightLock_Lock(&s_freeLock);
    for (C3D_Tex& t : s_pendingFree)
        C3D_TexDelete(&t);
    s_pendingFree.clear();
    LightLock_Unlock(&s_freeLock);
}

int nextPow2(int v)
{
    int p = 8;
    while (p < v)
        p <<= 1;
    return p;
}

inline u32 morton(u32 x, u32 y)
{
    return (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3);
}

} // namespace

bool init()
{
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    LightLock_Init(&s_freeLock);

    s_dvlb = DVLB_ParseFile((u32*)zos_shbin, zos_shbin_size);
    if (!s_dvlb) {
        dbg("gfx: shader parse failed");
        return false;
    }
    shaderProgramInit(&s_program);
    shaderProgramSetVsh(&s_program, &s_dvlb->DVLE[0]);
    C3D_BindProgram(&s_program);
    s_locProjection = shaderInstanceGetUniformLocation(s_program.vertexShader, "projection");
    s_locUvxform = shaderInstanceGetUniformLocation(s_program.vertexShader, "uvxform");

    s_vbo = (Vertex*)linearAlloc(sizeof(Vertex) * kMaxVertices);
    if (!s_vbo) {
        dbg("gfx: no linear memory for the vertex buffer");
        return false;
    }

    C3D_AttrInfo* attr = C3D_GetAttrInfo();
    AttrInfo_Init(attr);
    AttrInfo_AddLoader(attr, 0, GPU_FLOAT, 2);
    AttrInfo_AddLoader(attr, 1, GPU_FLOAT, 2);
    AttrInfo_AddLoader(attr, 2, GPU_UNSIGNED_BYTE, 4);

    C3D_BufInfo* buf = C3D_GetBufInfo();
    BufInfo_Init(buf);
    BufInfo_Add(buf, s_vbo, sizeof(Vertex), 3, 0x210);

    for (int i = 1; i < 6; i++)
        C3D_TexEnvInit(C3D_GetTexEnv(i));

    C3D_CullFace(GPU_CULL_NONE);
    C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);

    s_targets[0] = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    s_targets[1] = C3D_RenderTargetCreate(240, 320, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    C3D_RenderTargetSetOutput(s_targets[0], GFX_TOP, GFX_LEFT, kTransferFlags);
    C3D_RenderTargetSetOutput(s_targets[1], GFX_BOTTOM, GFX_LEFT, kTransferFlags);

    applyCombine(Combine::Color);
    setUvxform(1.0f, 1.0f, 0.0f, 0.0f);
    blendPremultiplied();
    return true;
}

void exit()
{
    freePending();
    for (C3D_RenderTarget*& t : s_targets) {
        if (t)
            C3D_RenderTargetDelete(t);
        t = nullptr;
    }
    if (s_vbo)
        linearFree(s_vbo);
    s_vbo = nullptr;
    shaderProgramFree(&s_program);
    if (s_dvlb)
        DVLB_Free(s_dvlb);
    s_dvlb = nullptr;
    C3D_Fini();
}

void frameBegin()
{
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    // The GPU is idle now: last frame's vertices and textures are free.
    freePending();
    s_used = 0;
    s_batchStart = 0;
    s_stats = {};
}

void frameEnd()
{
    flush();
    if (s_used)
        GSPGPU_FlushDataCache(s_vbo, s_used * sizeof(Vertex));
    s_lastStats = s_stats;
    s_lastStats.vertices = s_used;
    C3D_FrameEnd(0);
}

void beginScreen(Screen screen, u32 clearColor)
{
    flush();
    // Clears are queued as GX commands of their own: split first, so the
    // clear lands after what was drawn to the other screen, not before it.
    C3D_FrameSplit(0);
    s_screen = screen;
    C3D_RenderTarget* target = s_targets[screen == Screen::Top ? 0 : 1];
    C3D_RenderTargetClear(target, C3D_CLEAR_ALL, __builtin_bswap32(clearColor), 0);
    C3D_FrameDrawOn(target);
    scissorOff();
    ortho(0.0f, (float)screenWidth(screen), 0.0f, (float)kScreenHeight);
}

int screenWidth(Screen screen)
{
    return screen == Screen::Top ? 400 : 320;
}

void ortho(float left, float right, float bottom, float top)
{
    flush();
    C3D_Mtx m;
    Mtx_OrthoTilt(&m, left, right, bottom, top, 1.0f, -1.0f, true);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, s_locProjection, &m);
}

void projectionNDC()
{
    ortho(-1.0f, 1.0f, -1.0f, 1.0f);
}

void bindTexture(const Texture* texture, Combine combine)
{
    if (!texture || !texture->valid) {
        if (s_combine != Combine::Color) {
            flush();
            s_combine = Combine::Color;
            applyCombine(s_combine);
        }
        return;
    }
    if (s_tex == &texture->tex && s_combine == combine)
        return;
    flush();
    s_tex = &texture->tex;
    C3D_TexBind(0, const_cast<C3D_Tex*>(&texture->tex));
    setUvxform(texture->uvScaleX, texture->uvScaleY, texture->uvOffsetX, texture->uvOffsetY);
    if (s_combine != combine) {
        s_combine = combine;
        applyCombine(combine);
    }
}

void bindRawTexture(C3D_Tex* tex, Combine combine)
{
    if (s_tex == tex && s_combine == combine)
        return;
    flush();
    s_tex = tex;
    C3D_TexBind(0, tex);
    setUvxform(1.0f, 1.0f, 0.0f, 0.0f);
    if (s_combine != combine) {
        s_combine = combine;
        applyCombine(combine);
    }
}

void blend(GPU_BLENDFACTOR src, GPU_BLENDFACTOR dst)
{
    flush();
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, src, dst, src, dst);
}

void blendPremultiplied()
{
    blend(GPU_ONE, GPU_ONE_MINUS_SRC_ALPHA);
}

void scissor(int x, int y, int w, int h)
{
    flush();
    int sw = screenWidth(s_screen);
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > sw ? sw : x + w, y1 = y + h > kScreenHeight ? kScreenHeight : y + h;
    if (x1 < x0)
        x1 = x0;
    if (y1 < y0)
        y1 = y0;
    // The framebuffer is the screen turned a quarter: 240 wide, sw tall,
    // its x running up the screen and its y running right to left.
    C3D_SetScissor(GPU_SCISSOR_NORMAL, kScreenHeight - y1, sw - x1, kScreenHeight - y0, sw - x0);
    s_scissorOn = true;
}

void scissorOff()
{
    flush();
    C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
    s_scissorOn = false;
}

Vertex* triangles(int count)
{
    if (s_used + count > kMaxVertices) {
        s_stats.overflowed++;
        return nullptr;
    }
    Vertex* v = s_vbo + s_used;
    s_used += count;
    return v;
}

void quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, u32 color)
{
    Vertex* v = triangles(6);
    if (!v)
        return;
    v[0] = {x0, y0, u0, v0, color};
    v[1] = {x1, y0, u1, v0, color};
    v[2] = {x0, y1, u0, v1, color};
    v[3] = {x1, y0, u1, v0, color};
    v[4] = {x1, y1, u1, v1, color};
    v[5] = {x0, y1, u0, v1, color};
}

void flush()
{
    int n = s_used - s_batchStart;
    if (n <= 0)
        return;
    C3D_DrawArrays(GPU_TRIANGLES, s_batchStart, n);
    s_batchStart = s_used;
    s_stats.draws++;
}

namespace {

// The texture's first sub-image, set up as loadTexture's.
bool finishImport(Texture* out, Tex3DS_Texture t3x, const char* what)
{
    if (!t3x) {
        dbg("gfx: bad t3x %s", what);
        return false;
    }
    const Tex3DS_SubTexture* sub = Tex3DS_GetSubTexture(t3x, 0);
    out->width = sub->width;
    out->height = sub->height;
    out->uvScaleX = sub->right - sub->left;
    out->uvScaleY = sub->bottom - sub->top;
    out->uvOffsetX = sub->left;
    out->uvOffsetY = sub->top;
    Tex3DS_TextureFree(t3x);
    C3D_TexSetFilter(&out->tex, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(&out->tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    out->valid = true;
    return true;
}

} // namespace

bool loadTexture(Texture* out, const char* path)
{
    *out = Texture();
    FILE* f = fopen(path, "rb");
    if (!f)
        return false;
    // The file in one read, then decompressed from memory: through stdio,
    // Tex3DS_TextureImportStdio's small reads are each a trip to the file
    // system, and took 0.4 s a 512 KB texture.
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    void* mem = size > 0 ? malloc(size) : nullptr;
    const bool ok = mem && fread(mem, 1, size, f) == (size_t)size;
    fclose(f);
    if (!ok) {
        dbg("gfx: can't read %s", path);
        free(mem);
        return false;
    }
    Tex3DS_Texture t3x = Tex3DS_TextureImport(mem, size, &out->tex, nullptr, false);
    free(mem);
    return finishImport(out, t3x, path);
}

bool loadTextureFromMemory(Texture* out, const void* data, size_t size)
{
    *out = Texture();
    if (!data || size == 0)
        return false;
    return finishImport(out, Tex3DS_TextureImport(data, size, &out->tex, nullptr, false), "in memory");
}

bool createTexture(Texture* out, const u8* rgba, int width, int height, bool linearFilter)
{
    *out = Texture();
    if (width <= 0 || height <= 0 || width > 1024 || height > 1024)
        return false;
    int tw = nextPow2(width), th = nextPow2(height);
    if (!C3D_TexInit(&out->tex, tw, th, GPU_RGBA8))
        return false;
    u32* dst = (u32*)out->tex.data;
    if (tw != width || th != height)
        memset(dst, 0, tw * th * 4);
    // The first tile row in memory is the texture's top (v = 1), as in a
    // .t3x: image rows go in top down; within the texture, 8x8 tiles in
    // Morton order. (Checked on Azahar with a red top row.)
    const int tilesPerRow = tw >> 3;
    for (int y = 0; y < height; y++) {
        const u32 ty = y;
        const u8* src = rgba + y * width * 4;
        u32* tileRow = dst + (ty >> 3) * tilesPerRow * 64;
        for (int x = 0; x < width; x++, src += 4)
            tileRow[(x >> 3) * 64 + morton(x & 7, ty & 7)] =
                (u32)src[0] << 24 | (u32)src[1] << 16 | (u32)src[2] << 8 | src[3];
    }
    C3D_TexFlush(&out->tex);
    setTextureFilter(out, linearFilter);
    C3D_TexSetWrap(&out->tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    out->width = width;
    out->height = height;
    out->uvScaleX = (float)width / tw;
    out->uvScaleY = -(float)height / th;
    out->uvOffsetX = 0.0f;
    out->uvOffsetY = 1.0f;
    out->valid = true;
    return true;
}

bool createTiledTexture(Texture* out, const void* data, size_t size, int width, int height, GPU_TEXCOLOR format,
                        bool linearFilter)
{
    *out = Texture();
    if (width < 8 || height < 8 || width > 1024 || height > 1024 || nextPow2(width) != width ||
        nextPow2(height) != height)
        return false;
    if (!C3D_TexInit(&out->tex, width, height, format))
        return false;
    if (size != out->tex.size) {
        C3D_TexDelete(&out->tex);
        return false;
    }
    memcpy(out->tex.data, data, size);
    C3D_TexFlush(&out->tex);
    setTextureFilter(out, linearFilter);
    C3D_TexSetWrap(&out->tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    out->width = width;
    out->height = height;
    out->uvScaleX = 1.0f;
    out->uvScaleY = -1.0f;
    out->uvOffsetX = 0.0f;
    out->uvOffsetY = 1.0f;
    out->valid = true;
    return true;
}

void destroyTexture(Texture* texture)
{
    if (!texture->valid)
        return;
    if (s_tex == &texture->tex) {
        flush();
        s_tex = nullptr;
    }
    LightLock_Lock(&s_freeLock);
    s_pendingFree.push_back(texture->tex);
    LightLock_Unlock(&s_freeLock);
    *texture = Texture();
}

void setTextureFilter(Texture* texture, bool linear)
{
    GPU_TEXTURE_FILTER_PARAM f = linear ? GPU_LINEAR : GPU_NEAREST;
    C3D_TexSetFilter(&texture->tex, f, f);
}

bool loadFont(Font* out, const char* path)
{
    *out = Font();
    FILE* f = fopen(path, "rb");
    if (!f)
        return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    void* mem = size > 0 ? linearAlloc(size) : nullptr;
    bool ok = mem && fread(mem, 1, size, f) == (size_t)size;
    fclose(f);
    if (!ok || memcmp(mem, "CFNT", 4) != 0) {
        dbg("gfx: bad font %s", path);
        if (mem)
            linearFree(mem);
        return false;
    }
    out->cfnt = (CFNT_s*)mem;
    fontFixPointers(out->cfnt);
    TGLP_s* tglp = out->cfnt->finf.tglp;
    out->sheetCount = tglp->nSheets;
    out->sheets = new C3D_Tex[tglp->nSheets]();
    for (int i = 0; i < tglp->nSheets; i++) {
        C3D_Tex* tex = &out->sheets[i];
        tex->data = tglp->sheetData + tglp->sheetSize * i;
        tex->fmt = (GPU_TEXCOLOR)tglp->sheetFmt;
        tex->size = tglp->sheetSize;
        tex->width = tglp->sheetWidth;
        tex->height = tglp->sheetHeight;
        tex->param = GPU_TEXTURE_MAG_FILTER(GPU_LINEAR) | GPU_TEXTURE_MIN_FILTER(GPU_LINEAR) |
                     GPU_TEXTURE_WRAP_S(GPU_CLAMP_TO_EDGE) | GPU_TEXTURE_WRAP_T(GPU_CLAMP_TO_EDGE);
        tex->border = 0;
        tex->lodParam = 0;
    }
    GSPGPU_FlushDataCache(mem, size);
    out->valid = true;
    return true;
}

void freeFont(Font* font)
{
    if (!font->valid)
        return;
    flush();
    // The sheets point into the font's own memory; nothing else to free.
    delete[] font->sheets;
    linearFree(font->cfnt);
    *font = Font();
}

Stats lastFrameStats()
{
    return s_lastStats;
}

} // namespace gfx
