// WeaponsMenu (decomp/classes/WeaponsMenu.c): the panel beside ActionsMenu's
// weapons button with the two bombs, MK81 and MK82.
//
// The game showed how many of each were left ("x%i", tag 3) until they
// were bought unlimited; the port has every action unlimited.
#pragma once

#include <set>

#include "cocos/director.h"
#include "game/guilisteners.h"

class WeaponsMenu : public cocos2d::Node {
public:
    static WeaponsMenu* create(ActionSelectedListener* listener);

    bool init(ActionSelectedListener* listener);

    // The panel, centred on the node.
    cocos2d::Sprite* getPanel() const { return _panel; }

private:
    bool onTouchBegan(cocos2d::Touch* touch, cocos2d::Event* event);
    void onTouchMoved(cocos2d::Touch* touch, cocos2d::Event* event);
    void onTouchEnded(cocos2d::Touch* touch, cocos2d::Event* event);

    ActionSelectedListener* _listener = nullptr; // 0x244
    cocos2d::Sprite* _panel = nullptr;           // 0x248
    cocos2d::Sprite* _mk81 = nullptr;            // 0x24c
    cocos2d::Sprite* _mk82 = nullptr;            // 0x250
    cocos2d::Node* _pressed = nullptr;           // 0x254
    std::set<cocos2d::Node*> _touchable;         // 0x25c
};
