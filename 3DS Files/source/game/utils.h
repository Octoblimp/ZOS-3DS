// Utils (decomp/classes/Utils.c): file loading, zlib, random numbers, the
// save file's binary I/O. Errors throw std::runtime_error, as the game's do.
// Dropped: jStringToStdString, logAnalyticsEvent, logCrittercismBreadcrumb
// (Android only).
#pragma once

#include <cstdio>
#include <string>
#include <vector>

namespace Utils {

void log(const char* fmt, ...);
void setVerboseLogging(bool verbose);
void verboseLog(const char* fmt, ...);

// Through FileUtils: the game's asset paths. Throw "Failed to load file: ".
void loadTextFile(const std::string& path, std::string& out);
void loadBinaryFile(const std::string& path, std::vector<unsigned char>& out);

// zlib data that must inflate to exactly size bytes.
void decompressData(std::vector<unsigned char>& in, int size, std::vector<unsigned char>& out);

std::string intToString(int value);
// "%.<precision>f".
std::string floatToString(float value, int precision);

bool isPowerOf2(int value);
int roundUpToPowerOf2(int value);

// [0, 1), from a uniform 32-bit random number (the game used arc4random).
float getRandomDouble();
// A uniform 32-bit random number, where the game called arc4random itself.
unsigned getRandomUInt();

void parseVarint(unsigned char* in, int& value, unsigned char*& next);

void writeInt(FILE* f, int value);
int readInt(FILE* f);
void writeFloat(FILE* f, float value);
float readFloat(FILE* f);
void writeBool(FILE* f, bool value);
bool readBool(FILE* f);
// A u32 length, then the bytes.
void writeString(FILE* f, const std::string& value);
std::string readString(FILE* f);

void copyFile(const char* from, const char* to);
bool fileExists(const char* path);

} // namespace Utils
