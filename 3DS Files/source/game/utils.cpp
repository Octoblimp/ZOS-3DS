#include "game/utils.h"

#include <3ds.h>
#include <zlib.h>

#include <cstdarg>
#include <cstring>
#include <stdexcept>
#include <sys/stat.h>

#include "cocos/director.h"
#include "debuglog.h"

namespace Utils {

namespace {

bool s_verboseLogging = false;

void vlog(const char* fmt, va_list ap)
{
    char line[512];
    vsnprintf(line, sizeof(line), fmt, ap);
    dbg("%s", line);
}

// PCG32 in place of arc4random: the game only needs uniform numbers, and
// seeding from the tick counter gives a different outbreak each run, as
// arc4random did.
struct Pcg32 {
    u64 state = 0;
    u64 inc = 0;

    void seed(u64 initState, u64 initSeq)
    {
        state = 0;
        inc = (initSeq << 1) | 1;
        next();
        state += initState;
        next();
    }

    u32 next()
    {
        u64 old = state;
        state = old * 6364136223846793005ULL + inc;
        u32 xorshifted = (u32)(((old >> 18) ^ old) >> 27);
        u32 rot = (u32)(old >> 59);
        return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
    }
};

Pcg32& rng()
{
    static Pcg32 r;
    static bool seeded = false;
    if (!seeded) {
        r.seed(svcGetSystemTick(), osGetTime());
        seeded = true;
    }
    return r;
}

} // namespace

void log(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlog(fmt, ap);
    va_end(ap);
}

void setVerboseLogging(bool verbose)
{
    s_verboseLogging = verbose;
    log("Verbose logging %s", verbose ? "enabled" : "disabled");
}

void verboseLog(const char* fmt, ...)
{
    if (!s_verboseLogging)
        return;
    va_list ap;
    va_start(ap, fmt);
    vlog(fmt, ap);
    va_end(ap);
}

void loadTextFile(const std::string& path, std::string& out)
{
    out = cocos2d::FileUtils::getInstance()->getStringFromFile(path);
    if (out.empty())
        throw std::runtime_error("Failed to load file: " + path);
}

void loadBinaryFile(const std::string& path, std::vector<unsigned char>& out)
{
    out = cocos2d::FileUtils::getInstance()->getDataFromFile(path);
    if (out.empty())
        throw std::runtime_error("Failed to load file: " + path);
}

void decompressData(std::vector<unsigned char>& in, int size, std::vector<unsigned char>& out)
{
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    zs.next_in = in.data();
    zs.avail_in = (uInt)in.size();
    if (inflateInit(&zs) != Z_OK)
        throw std::runtime_error("Failed to initialize inflate");
    out.resize(size);
    zs.next_out = out.data();
    zs.avail_out = (uInt)out.size();
    int status = inflate(&zs, Z_FINISH);
    int availOut = (int)zs.avail_out;
    if (inflateEnd(&zs) != Z_OK)
        throw std::runtime_error("Failed to shut down inflate");
    if (status != Z_STREAM_END || availOut != 0)
        throw std::runtime_error("Failed to inflate data: (status " + intToString(status) + ", avail_out " +
                                 intToString(availOut) + ")");
}

std::string intToString(int value)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%d", value);
    return buf;
}

std::string floatToString(float value, int precision)
{
    char fmt[64];
    char buf[64];
    snprintf(fmt, sizeof(fmt), "%%.%df", precision);
    snprintf(buf, sizeof(buf), fmt, (double)value);
    return buf;
}

bool isPowerOf2(int value)
{
    if (value <= 0)
        return false;
    while ((value & 1) == 0)
        value >>= 1;
    return value == 1;
}

int roundUpToPowerOf2(int value)
{
    int p = 1;
    while (p < value)
        p <<= 1;
    return p;
}

float getRandomDouble()
{
    return (float)((double)rng().next() * 2.3283064365386963e-10);
}

unsigned getRandomUInt()
{
    return rng().next();
}

void parseVarint(unsigned char* in, int& value, unsigned char*& next)
{
    int v = 0;
    unsigned shift = 0;
    unsigned char b;
    do {
        b = *in++;
        v += (b & 0x7f) << shift;
        shift += 7;
    } while (b & 0x80);
    value = v;
    next = in;
}

void writeInt(FILE* f, int value)
{
    fwrite(&value, 4, 1, f);
}

int readInt(FILE* f)
{
    int value = 0;
    fread(&value, 4, 1, f);
    return value;
}

void writeFloat(FILE* f, float value)
{
    fwrite(&value, 4, 1, f);
}

float readFloat(FILE* f)
{
    float value = 0;
    fread(&value, 4, 1, f);
    return value;
}

void writeBool(FILE* f, bool value)
{
    fputc(value ? 1 : 0, f);
}

bool readBool(FILE* f)
{
    char c = 0;
    fread(&c, 1, 1, f);
    return c != 0;
}

void writeString(FILE* f, const std::string& value)
{
    u32 n = (u32)value.size();
    fwrite(&n, 4, 1, f);
    fwrite(value.data(), 1, n, f);
}

std::string readString(FILE* f)
{
    u32 n = 0;
    fread(&n, 4, 1, f);
    std::string s(n, '\0');
    if (n)
        fread(&s[0], 1, n, f);
    return s;
}

void copyFile(const char* from, const char* to)
{
    FILE* in = fopen(from, "rb");
    if (!in)
        throw std::runtime_error(std::string("Failed to load file: ") + from);
    FILE* out = fopen(to, "wb");
    char buf[4096];
    size_t n;
    do {
        n = fread(buf, 1, sizeof(buf), in);
        if (out)
            fwrite(buf, 1, n, out);
    } while (n == sizeof(buf));
    fclose(in);
    if (out)
        fclose(out);
}

bool fileExists(const char* path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

} // namespace Utils
