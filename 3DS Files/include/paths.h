// Where the game's files are. tools/prepare_data.py builds them from the APK
// (out/ZombieOutbreakSim/ on the PC). They are read from the build's RomFS
// when it carries them (tools/build_cia.ps1 puts them there unless -NoData),
// else from the SD card's folder. What the game writes (save/, cache/) is
// always on the card.
#pragma once

#define ZOS_DATA_ROOT "sdmc:/ZombieOutbreakSim"

// "romfs:" or ZOS_DATA_ROOT, without a trailing slash. main() sets it before
// anything is read.
const char* zosAssetRoot();
void zosSetAssetRoot(const char* root);
