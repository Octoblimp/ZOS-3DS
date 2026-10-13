// HomeScene (decomp/classes/HomeScene.c): the title screen, which loads
// GameNode a step a frame behind its loading bar. Loaded, a saved game goes
// straight back to GUIScene; otherwise Play shows, and goes to the map
// select, or the tutorial while it has not been played through.
//
// The game's layout: Play in the
// middle, Tutorial left of it and Credits right, the last two once the
// tutorial has been played. Share is gone with Android. The top screen is
// the port's, HomeTopScreen.
#pragma once

#include <set>

#include "cocos/director.h"

class GameNode;
class HomeTopScreen;

class HomeScene : public cocos2d::Layer {
public:
    static cocos2d::Scene* createScene(GameNode* game);
    static HomeScene* create(GameNode* game);

    ~HomeScene() override;

    bool init(GameNode* game);
    void onEnter() override;
    void onExit() override;
    // The loading: a GameNode step a frame.
    void update(float dt) override;

private:
    bool onTouchBegan(cocos2d::Touch* touch, cocos2d::Event* event);
    void onTouchEnded(cocos2d::Touch* touch, cocos2d::Event* event);
    void startTutorial();
    // The game's Play once the tutorial is done: the map select.
    void showMapSelect();

    GameNode* _game = nullptr;                      // 0x25c
    cocos2d::Sprite* _play = nullptr;               // 0x26c
    cocos2d::Sprite* _tutorial = nullptr;           // 0x270
    cocos2d::Sprite* _credits = nullptr;            // 0x274
    std::set<cocos2d::Node*> _touchable;            // 0x27c
    cocos2d::Node* _pressed = nullptr;              // 0x294
    cocos2d::Sprite* _loadingBarStart = nullptr;    // 0x298
    cocos2d::Sprite* _loadingBarEnd = nullptr;      // 0x29c
    cocos2d::Label* _loadingLabel = nullptr;        // 0x2a0
    cocos2d::Sprite* _binarySpaceLogo = nullptr;    // 0x2a4
    cocos2d::Sprite* _screenAustraliaLogo = nullptr; // 0x2a8
    // The port's.
    HomeTopScreen* _topScreen = nullptr;
};
