// The port's: what GUIScene's top screen shows under the stats,
// the game having had only the stats (over its map).
//
// MiniMap: the whole playing area from GameNode's MapRenderer (a second, small
// view of the same imagery), the living on it as dots (civilians white, the
// armed blue, zombies red) and the bottom screen's view as a yellow frame.
//
// PopulationGraph: civilians, zombies and the dead over the game's time, in
// the stats' colours (the dead grey). A sample a game second, halved to every
// other one whenever there are too many, so the whole game always fits.
#pragma once

#include <vector>

#include "cocos/director.h"
#include "game/simulation.h"

class GameNode;

class MiniMap : public cocos2d::Node {
public:
    static MiniMap* create(GameNode* game);

    bool init(GameNode* game);
    void draw(cocos2d::Renderer* renderer, const cocos2d::Mat4& transform, u32 flags) override;

    // The largest box of the map's shape within width x height (top screen
    // pixels); the node's content size becomes it.
    void fitInto(float width, float height);
    // The people and the view, from the running simulation.
    void refresh();

private:
    GameNode* _game = nullptr;
    cocos2d::DrawNode* _dots = nullptr;
    float _pixels = 0.0f;   // top screen pixels a map pixel
};

class PopulationGraph : public cocos2d::Node {
public:
    static PopulationGraph* create();

    bool init() override;

    void setGraphSize(float width, float height);
    // From GUIScene's stats (ten a second): a sample each interval. A game
    // time going back (Restart) starts again.
    void addStats(const Simulation::Stats& stats);

private:
    struct Sample {
        int time;
        int civs;
        int zombies;
        int dead;
    };

    void redraw();

    std::vector<Sample> _samples;
    int _interval = 1000;   // game ms between samples
    cocos2d::DrawNode* _lines = nullptr;
};
