// GUIGlobals: dpToSize, which AppDelegate sets from the screen's DPI, and
// the font every counter and menu label uses, a static std::string in a
// header the game's units each have a copy of (their static initialisers
// all set it to Visitor; decomp: the _GLOBAL__sub_I functions).
#pragma once

namespace GUIGlobals {

// Pixels a density-independent pixel. The port draws the MDPI atlases
// (tools/prepare_data.py), the bucket AppDelegate picks at 160 DPI or less: 1.
extern float dpToSize;

extern const char* const kFont;

} // namespace GUIGlobals
