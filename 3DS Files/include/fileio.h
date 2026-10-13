// The port's: a whole file in one read (open, fstat, read, close), with no
// FILE between.
#pragma once

#include <vector>

#include <3ds/types.h>

// The file at path into data. False if it will not open or read; an empty
// file reads true and empty.
bool readWholeFile(const char* path, std::vector<u8>& data);
