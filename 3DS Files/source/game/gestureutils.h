// GestureUtils (decomp/classes/GestureUtils.c): a drag pans the map and a
// pinch zooms it about the fingers' middle. The view is x, y (the map pixel
// at the screen's top left) and scale; touches are in the screen's
// coordinates, y up.
#pragma once

#include <vector>

#include "cocos/director.h"

namespace GestureUtils {

// touches[0]'s move.
void handlePan(const std::vector<cocos2d::Touch*>& touches, float density, float scale, double& x, double& y);
// touches[0] and [1]: scale by the change in their distance, kept within
// minScale..1, with the point under their middle following it.
void handleZoom(const std::vector<cocos2d::Touch*>& touches, const cocos2d::Size& screen, float density,
                float minScale, float& scale, double& x, double& y);

} // namespace GestureUtils
