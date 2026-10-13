// ActionsMenu (decomp/classes/ActionsMenu.c): the panel that slides in from
// the screen's right edge by its tab, with the soldiers, weapons and
// helicopters buttons. Weapons opens WeaponsMenu beside it.
//
// The node sits a panel's width off the right edge; the panel slides left
// by its width to show. In the tutorial soldiers and helicopters are
// greyed out until the tutorial enables them. The game's quantity labels
// (tag 3) and IAP observer are gone: every action is unlimited.
#pragma once

#include <set>

#include "cocos/director.h"
#include "game/guilisteners.h"

class WeaponsMenu;

class ActionsMenu : public cocos2d::Node {
public:
    static ActionsMenu* create(ActionSelectedListener* listener, bool tutorialMode);

    bool init(ActionSelectedListener* listener, bool tutorialMode);

    cocos2d::Sprite* getPanel() const { return _panel; }
    bool isOpen() const { return _open; }

    // The tutorial's: the panel out with the weapons open, and back.
    void openWeaponsMenu();
    void closeWeaponsMenu();
    // Slides the panel out or back (0.25 s), and tells the listener.
    void toggleMenu();
    void enableSoldiersButton();
    void enableHelicoptersButton();

private:
    bool onTouchBegan(cocos2d::Touch* touch, cocos2d::Event* event);
    void onTouchMoved(cocos2d::Touch* touch, cocos2d::Event* event);
    void onTouchEnded(cocos2d::Touch* touch, cocos2d::Event* event);
    // The button's pressed frame, and its label in the action's colour.
    void press(cocos2d::Node* button);

    ActionSelectedListener* _listener = nullptr; // 0x244
    cocos2d::Sprite* _panel = nullptr;           // 0x248
    cocos2d::Node* _tab = nullptr;               // 0x24c
    cocos2d::Sprite* _soldiers = nullptr;        // 0x250
    cocos2d::Sprite* _weapons = nullptr;         // 0x254
    cocos2d::Sprite* _helicopters = nullptr;     // 0x258
    cocos2d::Node* _pressed = nullptr;           // 0x25c
    bool _tabPressed = false;                    // 0x260
    bool _open = false;                          // 0x261
    WeaponsMenu* _weaponsMenu = nullptr;         // 0x268
    std::set<cocos2d::Node*> _touchable;         // 0x270
};
