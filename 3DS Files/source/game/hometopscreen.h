// The top screen on the home and credits screens: the port's, the game
// having had one screen. It does not repeat the bottom's background: it is
// the game's people crossing
// Background_General, civilians running with zombies shambling after them,
// drawn from the person sheets once GameNode has loaded them. MapSelectScene,
// whose bottom is Background_General, has them cross
// Background_CreditsScreen.
#pragma once

#include <string>
#include <vector>

#include "cocos/director.h"

class GameNode;
class PersonType;

class HomeTopScreen : public cocos2d::Node {
public:
    static const char* const kGeneralBackground;

    static HomeTopScreen* create(GameNode* game, const std::string& background = kGeneralBackground);

    bool init(GameNode* game, const std::string& background);
    void update(float dt) override;
    void draw(cocos2d::Renderer* renderer, const cocos2d::Mat4& transform, u32 flags) override;

private:
    struct Walker {
        const PersonType* type = nullptr;
        bool zombie = false;
        float x = 0.0f, y = 0.0f;
        float speed = 0.0f; // pixels a second, rightwards
        int ms = 0;         // into its animation
    };

    // Puts w at a random lane, off the left edge, or anywhere across when
    // the crowd first appears.
    void spawn(Walker& w, bool anywhere);

    GameNode* _game = nullptr;
    std::vector<Walker> _walkers;
};
