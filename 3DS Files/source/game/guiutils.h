// GUIUtils (decomp/classes/GUIUtils.c): the menus' buttons, labels,
// rectangles and clippers, and their hit tests.
//
// A button is a sprite whose frame swaps to its "_Press" frame while it is
// held, with an optional label (tag 1) tinted while held. The game kept the
// frame names and colour as the node's user data and never freed them; the
// port makes them the node's user object, released with the node.
#pragma once

#include <set>
#include <string>

#include "cocos/director.h"

namespace GUIUtils {

class ButtonData : public cocos2d::Ref {
public:
    std::string normal;
    std::string pressed;
    cocos2d::Color3B color;
};

// The node's ButtonData, or nullptr.
ButtonData* getButtonData(cocos2d::Node* node);

// The node and every parent visible.
bool hasVisibleParents(cocos2d::Node* node);
// Visible, with visible parents, and location (the screen's) inside its
// content size.
bool checkTouch(cocos2d::Node* node, const cocos2d::Vec2& location);
bool checkTouchAny(const std::set<cocos2d::Node*>& nodes, const cocos2d::Vec2& location);
// The first of nodes checkTouch passes, or nullptr: the menus' loops over
// their touchable nodes.
cocos2d::Node* touchedNode(const std::set<cocos2d::Node*>& nodes, const cocos2d::Vec2& location);

// name and name with ".png" made "_Press.png"; color, the label's.
void createButtonData(cocos2d::Node* node, const std::string& name, const cocos2d::Color3B& color);
// The pressed frame (the touch handlers' own code in the game).
void setPressed(cocos2d::Node* button);
// The normal frame, and the label's colour back.
void resetInteraction(cocos2d::Node* button);

// In the GUI's font at size dp, " " for "".
cocos2d::Label* createLabel(const std::string& text, float size);
// 18dp, centred under the sprite, tag 1.
cocos2d::Label* createButtonLabel(cocos2d::Sprite* sprite, const std::string& text);
// A button with its label under it, the pair centred at (x, y).
cocos2d::Sprite* createButton(const std::string& frame, const std::string& text, cocos2d::Node* parent, float x,
                              float y, int z);

// A filled w x h rectangle, its content size.
cocos2d::DrawNode* createDrawRectangle(float w, float h, const cocos2d::Color4F& color);
// Clips its children to (0, 0)..(w, h).
cocos2d::ClippingNode* createClipper(float w, float h);

// The frame's untrimmed size, or 0 x 0 (logged) if there is none.
cocos2d::Size spriteSizeFromFrameCache(const std::string& name);
// The size of an "x" in the font at size: its line height.
cocos2d::Size textDimensionsFromFontSize(const std::string& font, float size);

} // namespace GUIUtils
