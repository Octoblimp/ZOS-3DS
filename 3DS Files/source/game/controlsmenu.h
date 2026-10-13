// ControlsMenu (decomp/classes/ControlsMenu.c): the panel that slides in
// from the screen's left edge by its tab, with Change map and Settings.
// Settings opens SettingsMenu to the panel's right, an arrow pointing at the
// button.
//
// The whole node slides by the panel's width. In the tutorial Change map is
// greyed out until the tutorial's end. The game's EverythingPopup (the
// store's "buy everything") is gone.
#pragma once

#include <set>

#include "cocos/director.h"
#include "game/guilisteners.h"

class GameNode;
class SettingsMenu;

class ControlsMenu : public cocos2d::Node, public SettingsSelectedListener {
public:
    static ControlsMenu* create(GameNode* game, ControlSelectedListener* listener, bool tutorialMode);

    bool init(GameNode* game, ControlSelectedListener* listener, bool tutorialMode);

    cocos2d::Sprite* getPanel() const { return _panel; }
    bool isOpen() const { return _open; }
    bool isSettingsMenuVisible() const;

    // The tutorial's: the panel out with the settings open.
    void openSettingsMenu();
    void toggleSettingsMenu();
    // Slides the panel out or back (0.25 s).
    void toggleMenu();
    void enableChangeMapButton();

    // SettingsSelectedListener.
    void onSettingsSelected(SettingsType type) override;

private:
    bool onTouchBegan(cocos2d::Touch* touch, cocos2d::Event* event);
    void onTouchMoved(cocos2d::Touch* touch, cocos2d::Event* event);
    void onTouchEnded(cocos2d::Touch* touch, cocos2d::Event* event);
    // The button's pressed frame, and its label in its colour.
    void press(cocos2d::Node* button);

    GameNode* _game = nullptr;                    // 0x24c
    ControlSelectedListener* _listener = nullptr; // 0x254
    cocos2d::Sprite* _panel = nullptr;            // 0x258
    cocos2d::Sprite* _changeMap = nullptr;        // 0x25c
    cocos2d::Sprite* _settings = nullptr;         // 0x260
    cocos2d::Node* _tab = nullptr;                // 0x264
    cocos2d::Node* _pressed = nullptr;            // 0x268
    bool _tabPressed = false;                     // 0x26c
    bool _open = false;                           // 0x26d
    SettingsMenu* _settingsMenu = nullptr;        // 0x270
    std::set<cocos2d::Node*> _touchable;          // 0x284
};
