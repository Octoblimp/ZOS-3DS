// TilePack: an online map's satellite imagery in one file on the card.
//
// Kept as a file a tile, the imagery was hundreds of small files a map. An SD
// card writes nothing worse: each one is a new directory entry, a FAT update
// and a cluster, all in different places. The 3DS's file system serves one
// request at a time, so everything else on the card (the music, the HOME
// Menu, the log) waited behind them, for seconds.
//
// A pack is one file the tiles are appended to, gathered in memory and written
// half a megabyte at a time. Each tile is a record: a header (kRecordMagic,
// then the zoom, x, y and the JPEG's size, little-endian 32-bit words) and the
// JPEG. A tile the imagery lacks has no JPEG (size 0). Beside it, <pack>.idx
// lists the records and how much of the pack it covers, so opening a pack
// reads one small file; records past what it covers (a power cut before the
// index was written) are found by reading their headers.
//
// Any thread may use a pack. A write is done outside the lock that finding a
// tile takes, so a tile still gathered, or not there at all, is answered at
// once; only one on the card waits for the write to end.
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <3ds.h>

class TilePack {
public:
    // Opens the pack at path, or makes it (and its folder). nullptr if the card
    // will not have it.
    static TilePack* open(const std::string& path);
    // Writes what is left, and the index.
    ~TilePack();

    TilePack(const TilePack&) = delete;
    TilePack& operator=(const TilePack&) = delete;

    const std::string& path() const { return _path; }

    // Whether the pack has the tile (a JPEG, or that the imagery lacks it).
    bool has(int zoom, int x, int y);
    // The tile's JPEG into jpeg (empty for a tile the imagery lacks). False if
    // the pack does not have it, or it could not be read.
    bool get(int zoom, int x, int y, std::vector<u8>& jpeg);
    // Adds a tile: its JPEG, or empty for one the imagery lacks. A tile it has
    // already is left as it is.
    void add(int zoom, int x, int y, const std::vector<u8>& jpeg);
    // Writes what is gathered, and the index if it changed.
    void flush();
    // Writes what is left and lets go of the file: after this the pack has
    // nothing, and takes nothing (for the app's exit).
    void close();
    // The tiles in it.
    int count();

    // Removes a pack and its index from the card.
    static void remove(const std::string& path);

private:
    struct Entry {
        u32 offset; // of the JPEG, in the file
        u32 size;
    };

    explicit TilePack(const std::string& path);
    bool load();
    void scan(u32 from);
    // With _lock held: the index of what is written, as its file's words.
    std::vector<u32> indexWords();
    static u64 key(int zoom, int x, int y) { return ((u64)zoom << 48) | ((u64)(u32)x << 24) | (u64)(u32)y; }

    std::string _path;
    // The entries and buffers. Taken after _fileLock when both are.
    LightLock _lock;
    // The file and its position: reads, writes, and the index's file.
    LightLock _fileLock;
    FILE* _file = nullptr; // set and cleared with both held
    std::unordered_map<u64, Entry> _entries;
    // The records written end here (and the file, past a torn one).
    u32 _written = 0;
    // Records being written, at _written, and those gathered after them.
    std::vector<u8> _writing;
    std::vector<u8> _pending;
    bool _indexDirty = false;
};
