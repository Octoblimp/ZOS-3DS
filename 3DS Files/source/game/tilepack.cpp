#include "game/tilepack.h"

#include <cstdio>
#include <cstring>
#include <sys/stat.h>

#include "debuglog.h"
#include "fileio.h"

namespace {

const u32 kRecordMagic = 0x5250545a; // "ZTPR"
const u32 kIndexMagic = 0x4950545a;  // "ZTPI"
const u32 kIndexVersion = 1;
const u32 kRecordWords = 5;          // magic, zoom, x, y, size
const u32 kRecordBytes = kRecordWords * 4;
const u32 kIndexHeaderBytes = 16;    // magic, version, count, covered
// No tile comes near this: a bigger size is a torn record.
const u32 kMaxTile = 2 * 1024 * 1024;
// Gathered before a write: a few dozen tiles.
const size_t kWriteSize = 512 * 1024;

// The folders down to path's, each made if it is not there.
void makeParents(const std::string& path)
{
    const size_t root = path.find(":/");
    for (size_t slash = path.find('/', root == std::string::npos ? 0 : root + 2); slash != std::string::npos;
         slash = path.find('/', slash + 1))
        mkdir(path.substr(0, slash).c_str(), 0777);
}

} // namespace

TilePack* TilePack::open(const std::string& path)
{
    TilePack* pack = new TilePack(path);
    if (!pack->load()) {
        delete pack;
        return nullptr;
    }
    return pack;
}

TilePack::TilePack(const std::string& path) : _path(path)
{
    LightLock_Init(&_lock);
    LightLock_Init(&_fileLock);
}

TilePack::~TilePack()
{
    close();
}

bool TilePack::load()
{
    _file = fopen(_path.c_str(), "r+b");
    if (!_file) {
        makeParents(_path);
        _file = fopen(_path.c_str(), "w+b");
    }
    if (!_file) {
        dbg("imagery: no pack file");
        return false;
    }
    // The pack's reads and writes are whole tiles and half megabytes: stdio's
    // buffer would only copy them.
    setvbuf(_file, nullptr, _IONBF, 0);

    // The index, if it is whole and the pack still has what it covers.
    struct stat st;
    const u32 size = fstat(fileno(_file), &st) == 0 ? (u32)st.st_size : 0;
    std::vector<u8> data;
    u32 covered = 0;
    if (readWholeFile((_path + ".idx").c_str(), data) && data.size() >= kIndexHeaderBytes) {
        const u32* w = reinterpret_cast<const u32*>(data.data());
        const u32 count = w[2];
        if (w[0] == kIndexMagic && w[1] == kIndexVersion && w[3] <= size &&
            data.size() == kIndexHeaderBytes + (size_t)count * kRecordBytes) {
            covered = w[3];
            const u32* e = w + kIndexHeaderBytes / 4;
            for (u32 i = 0; i < count; i++, e += kRecordWords) {
                if (e[3] + e[4] > covered) {
                    // Not this pack's index after all.
                    _entries.clear();
                    covered = 0;
                    break;
                }
                _entries[key((int)e[0], (int)e[1], (int)e[2])] = Entry{e[3], e[4]};
            }
        }
    }
    scan(covered);
    return true;
}

void TilePack::scan(u32 from)
{
    struct stat st;
    const u32 size = fstat(fileno(_file), &st) == 0 ? (u32)st.st_size : 0;
    u32 pos = from;
    int found = 0;
    while (pos + kRecordBytes <= size) {
        u32 h[kRecordWords];
        if (fseek(_file, (long)pos, SEEK_SET) != 0 || fread(h, 1, kRecordBytes, _file) != kRecordBytes)
            break;
        if (h[0] != kRecordMagic || h[4] > kMaxTile || pos + kRecordBytes + h[4] > size)
            break;
        _entries[key((int)h[1], (int)h[2], (int)h[3])] = Entry{pos + kRecordBytes, h[4]};
        pos += kRecordBytes + h[4];
        found++;
    }
    // Anything after is a torn record: the next write goes over it.
    _written = pos;
    if (found)
        _indexDirty = true;
}

bool TilePack::has(int zoom, int x, int y)
{
    LightLock_Lock(&_lock);
    const bool found = _entries.count(key(zoom, x, y)) != 0;
    LightLock_Unlock(&_lock);
    return found;
}

bool TilePack::get(int zoom, int x, int y, std::vector<u8>& jpeg)
{
    jpeg.clear();
    LightLock_Lock(&_lock);
    auto it = _entries.find(key(zoom, x, y));
    if (it == _entries.end() || !_file) {
        LightLock_Unlock(&_lock);
        return false;
    }
    const Entry e = it->second;
    jpeg.resize(e.size);
    const u32 pendingAt = _written + (u32)_writing.size();
    bool onCard = false;
    if (e.size == 0) {
        // The imagery lacks it.
    } else if (e.offset >= pendingAt) {
        memcpy(jpeg.data(), _pending.data() + (e.offset - pendingAt), e.size);
    } else if (e.offset >= _written) {
        memcpy(jpeg.data(), _writing.data() + (e.offset - _written), e.size);
    } else {
        onCard = true;
    }
    LightLock_Unlock(&_lock);
    if (!onCard)
        return true;
    // What is written stays as it is: only this read waits for a write.
    LightLock_Lock(&_fileLock);
    const bool ok =
        _file && fseek(_file, (long)e.offset, SEEK_SET) == 0 && fread(jpeg.data(), 1, e.size, _file) == e.size;
    LightLock_Unlock(&_fileLock);
    if (!ok)
        jpeg.clear();
    return ok;
}

void TilePack::add(int zoom, int x, int y, const std::vector<u8>& jpeg)
{
    LightLock_Lock(&_lock);
    const u64 k = key(zoom, x, y);
    bool full = false;
    if (_file && !_entries.count(k) && jpeg.size() <= kMaxTile) {
        const u32 h[kRecordWords] = {kRecordMagic, (u32)zoom, (u32)x, (u32)y, (u32)jpeg.size()};
        const size_t at = _pending.size();
        _pending.resize(at + kRecordBytes + jpeg.size());
        memcpy(_pending.data() + at, h, kRecordBytes);
        if (!jpeg.empty())
            memcpy(_pending.data() + at + kRecordBytes, jpeg.data(), jpeg.size());
        _entries[k] = Entry{_written + (u32)_writing.size() + (u32)at + kRecordBytes, (u32)jpeg.size()};
        _indexDirty = true;
        // (A write in progress on another thread leaves these to the next.)
        full = _pending.size() >= kWriteSize && _writing.empty();
    }
    LightLock_Unlock(&_lock);
    if (full)
        flush();
}

std::vector<u32> TilePack::indexWords()
{
    // Only what is written: the gathered tiles are not in the file yet.
    std::vector<u32> words;
    words.reserve(kIndexHeaderBytes / 4 + _entries.size() * kRecordWords);
    words.push_back(kIndexMagic);
    words.push_back(kIndexVersion);
    words.push_back(0);
    words.push_back(_written);
    u32 count = 0;
    for (const auto& entry : _entries) {
        // (By where its record ends: a tile the imagery lacks, last, ends
        // where its JPEG would start.)
        if (entry.second.offset + entry.second.size > _written)
            continue;
        const u64 k = entry.first;
        words.push_back((u32)(k >> 48));
        words.push_back((u32)(k >> 24) & 0xffffff);
        words.push_back((u32)k & 0xffffff);
        words.push_back(entry.second.offset);
        words.push_back(entry.second.size);
        count++;
    }
    words[2] = count;
    return words;
}

void TilePack::flush()
{
    // One write at a time; tiles gathered meanwhile wait for the next.
    LightLock_Lock(&_fileLock);
    LightLock_Lock(&_lock);
    _writing.swap(_pending);
    const u32 at = _written;
    LightLock_Unlock(&_lock);

    bool ok = true;
    if (!_writing.empty())
        ok = _file && fseek(_file, (long)at, SEEK_SET) == 0 &&
             fwrite(_writing.data(), 1, _writing.size(), _file) == _writing.size();

    LightLock_Lock(&_lock);
    if (ok) {
        _written += (u32)_writing.size();
    } else {
        // The card is full or gone: what was gathered is forgotten, and
        // downloaded again another time.
        dbg("imagery: could not write %u KB to the pack", (unsigned)(_writing.size() / 1024));
        for (auto it = _entries.begin(); it != _entries.end();)
            it = it->second.offset - kRecordBytes >= at ? _entries.erase(it) : std::next(it);
        std::vector<u8>().swap(_pending);
    }
    std::vector<u8>().swap(_writing);
    std::vector<u32> words;
    if (_indexDirty && _file) {
        words = indexWords();
        _indexDirty = false;
    }
    LightLock_Unlock(&_lock);

    if (!words.empty()) {
        // Written over the old one. A power cut halfway leaves it the wrong
        // size, and the next open reads the pack's headers instead.
        FILE* f = fopen((_path + ".idx").c_str(), "wb");
        const size_t bytes = words.size() * 4;
        const bool written = f && fwrite(words.data(), 1, bytes, f) == bytes;
        if (f)
            fclose(f);
        if (!written) {
            LightLock_Lock(&_lock);
            _indexDirty = true;
            LightLock_Unlock(&_lock);
        }
    }
    LightLock_Unlock(&_fileLock);
}

void TilePack::close()
{
    flush();
    LightLock_Lock(&_fileLock);
    LightLock_Lock(&_lock);
    if (_file) {
        fclose(_file);
        _file = nullptr;
    }
    _entries.clear();
    std::vector<u8>().swap(_pending);
    LightLock_Unlock(&_lock);
    LightLock_Unlock(&_fileLock);
}

int TilePack::count()
{
    LightLock_Lock(&_lock);
    const int n = (int)_entries.size();
    LightLock_Unlock(&_lock);
    return n;
}

void TilePack::remove(const std::string& path)
{
    ::remove(path.c_str());
    ::remove((path + ".idx").c_str());
}
