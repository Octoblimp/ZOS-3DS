// CreditsScene (decomp/classes/CreditsScene.c): the credits, scrolled with a
// drag, and a Home button back to HomeScene. The game's two texts were its
// credits and Android's software licenses; the port keeps the first
// (kGameCredits, game/credits_data.cpp, generated from the game) and lists
// its own libraries in place of the second. The top screen is HomeScene's
// people.
#pragma once

#include <set>

#include "cocos/director.h"

class GameNode;
class HomeTopScreen;

class CreditsScene : public cocos2d::Layer {
public:
    static const char* const kGameCredits;

    static cocos2d::Scene* createScene(GameNode* game);
    static CreditsScene* create(GameNode* game);

    ~CreditsScene() override;

    bool init(GameNode* game);
    void onEnter() override;
    void onExit() override;
    // The port's: the Circle Pad and D-pad scroll too, and B goes home.
    void update(float dt) override;

private:
    bool onTouchBegan(cocos2d::Touch* touch, cocos2d::Event* event);
    void onTouchMoved(cocos2d::Touch* touch, cocos2d::Event* event);
    void onTouchEnded(cocos2d::Touch* touch, cocos2d::Event* event);
    void scrollBy(float dy);
    void goHome();

    cocos2d::Sprite* _home = nullptr;     // 0x258
    cocos2d::Node* _content = nullptr;    // 0x25c
    std::set<cocos2d::Node*> _touchable;  // 0x260
    float _maxScroll = 0.0f;              // 0x278
    cocos2d::Node* _pressed = nullptr;    // 0x27c
    GameNode* _game = nullptr;            // 0x284
    // The port's.
    HomeTopScreen* _topScreen = nullptr;
};
