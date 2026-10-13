// The port's: a player map's collision layer from
// OpenStreetMap. The game's player maps came from class3outbreak.com, whose
// players drew them by hand in its editor. The port asks Overpass for the
// area's buildings, barriers, trees and water, and draws them into the game's
// own format (collisionlayer.h) in the editor's style, as the
// featured online maps (which are player maps) show it:
// - open ground 0x03, trees and woods 0x07, water 0x82 (with the sea from the
//   coastline), bridges and piers back to 0x03 over it;
// - walls and hedges 0x00, fences and railings 0x22 (sight passes), opened
//   at gates;
// - buildings 0x1f inside 0x00 walls, with 0x0f doors (at their mapped
//   entrances, or on the longest wall that faces open ground) and 0x16
//   windows along the walls that face out.
//
// Everything here is plain C++ (no 3DS calls), so it builds on the PC too.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <3ds/types.h>

namespace osm {

// A map's area in zoom-20 world pixels (Web Mercator, 2^28 around the
// world). The layer has a cell per 4 x 4 of them.
struct Area {
    int worldX = 0;
    int worldY = 0;
    int width = 0;
    int height = 0;
};

// The area's corners in degrees.
void areaBounds(const Area& area, double& south, double& west, double& north, double& east);

// Overpass QL for the area's features (the interpreter's "data" field).
std::string overpassQuery(const Area& area);

// Overpass's JSON answer (out geom) as the area's layer: width/4 x height/4
// cells, row by row from the top left. False, with error, if it does not
// parse.
bool buildCollisionLayer(const Area& area, const char* json, size_t size, std::vector<u8>& cells,
                         std::string& error);

// Trees from the imagery, as OpenStreetMap maps few outside parks and woods:
// rgba is the area's zoom-18 imagery, a pixel per cell (the layer's width x
// height). Open ground (0x03) where most of the 5 x 5 cells around are dark
// green becomes cover (0x07). Against the editor's 13021 (hand-drawn over
// the same place), that finds two thirds of its trees, and two thirds of
// what it marks are its trees.
void addImageryCover(const std::vector<u8>& rgba, int width, int height, std::vector<u8>& cells);

// The share of the cells that is open ground (0x03 or 0x07), where people
// start and wander.
float openShare(const std::vector<u8>& cells);

} // namespace osm
