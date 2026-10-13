#include "game/gestureutils.h"

#include <algorithm>
#include <cmath>

using cocos2d::Touch;
using cocos2d::Vec2;

namespace GestureUtils {

void handlePan(const std::vector<Touch*>& touches, float density, float scale, double& x, double& y)
{
    const Vec2 location = touches[0]->getLocation();
    const Vec2 previous = touches[0]->getPreviousLocation();
    x -= (double)((location.x - previous.x) / (density * scale));
    y += (double)((location.y - previous.y) / (density * scale));
}

void handleZoom(const std::vector<Touch*>& touches, const cocos2d::Size& screen, float density, float minScale,
                float& scale, double& x, double& y)
{
    const Vec2 a = touches[0]->getLocation();
    const Vec2 aPrevious = touches[0]->getPreviousLocation();
    const Vec2 b = touches[1]->getLocation();
    const Vec2 bPrevious = touches[1]->getPreviousLocation();
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float dxPrevious = bPrevious.x - aPrevious.x, dyPrevious = bPrevious.y - aPrevious.y;
    const float distance = std::sqrt(dx * dx + dy * dy);
    const float distancePrevious = std::sqrt(dxPrevious * dxPrevious + dyPrevious * dyPrevious);
    float newScale = std::min(distance / distancePrevious * scale, 1.0f);
    if (newScale < minScale)
        newScale = minScale;
    const float oldPixels = scale * density;
    const float newPixels = newScale * density;
    scale = newScale;
    // Zoom about the fingers' middle, then follow its move.
    const float midX = a.x + dx * 0.5f, midY = a.y + dy * 0.5f;
    x = x + (double)(midX / oldPixels) - (double)(midX / newPixels);
    y = y + (double)((screen.height - midY) / oldPixels) - (double)((screen.height - midY) / newPixels);
    const float midXPrevious = aPrevious.x + dxPrevious * 0.5f, midYPrevious = aPrevious.y + dyPrevious * 0.5f;
    x -= (double)((midX - midXPrevious) / newPixels);
    y += (double)((midY - midYPrevious) / newPixels);
}

} // namespace GestureUtils
