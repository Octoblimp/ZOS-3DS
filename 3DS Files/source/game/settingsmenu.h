// SettingsMenu (decomp/classes/SettingsMenu.c): the outbreak settings panel
// ControlsMenu opens. A slider for each of GameOptions' options, its values
// in a row dragged sideways under a filter, and Restart, which takes the
// sliders' values and restarts the simulation. When the sliders are taller
// than the panel fits on screen they scroll, with a scroll thumb.
//
// The panel (its body, a strip cut from SettingsPanel_Body as tall as it
// needs or the screen allows, and its top and bottom edges) is centred on
// the node.
//
// The game's locked settings (the white splat with Unlock Settings and
// Watch a Video, the crosses on values not in the free version, the
// disabled Restart and its error popups) are gone: every value is
// available in the port.
#pragma once

#include <set>
#include <vector>

#include "cocos/director.h"
#include "game/guilisteners.h"

class GameNode;
class GameOptions;

class SettingsMenu : public cocos2d::Node {
public:
    static SettingsMenu* create(GameNode* game, SettingsSelectedListener* listener, bool tutorialMode);

    bool init(GameNode* game, SettingsSelectedListener* listener, bool tutorialMode);

    // The panel's size, its edges included (the game's getContentSize,
    // which hid Node's).
    cocos2d::Size getMenuSize() const;

private:
    // The body: SettingsPanel_Body's top height pixels, at most what fits
    // on screen 5dp from its edges (60dp less in the tutorial, for its bar).
    cocos2d::Sprite* createClippedBar(float height, bool tutorialMode);
    void createTitle(cocos2d::Node* bar, float barHeight, float buttonHeight, float padding);
    cocos2d::Sprite* createSettingsButton(const std::string& frame, cocos2d::Node* bar, float x, float y);
    cocos2d::Label* createButtonText(const std::string& text, cocos2d::Node* button, float x, float y);
    // The option's values in a row, (longest + 1) characters apart.
    void addOptionValues(GameOptions* options, int option, cocos2d::Node* node, const cocos2d::Size& charSize);
    // The value the slider's row is nearest.
    int sliderValueIndex(int option) const;

    bool onTouchBegan(cocos2d::Touch* touch, cocos2d::Event* event);
    void onTouchMoved(cocos2d::Touch* touch, cocos2d::Event* event);
    void onTouchEnded(cocos2d::Touch* touch, cocos2d::Event* event);

    // _state: dragging a slider (its index), the panel (scrolling) or a
    // button.
    static const int kStateNone = -1;   // 0x2fc
    static const int kStateButton = -2; // 0x300

    GameNode* _game = nullptr;                    // 0x248
    SettingsSelectedListener* _listener = nullptr; // 0x24c
    cocos2d::Sprite* _bar = nullptr;              // 0x250
    cocos2d::Sprite* _restart = nullptr;          // 0x25c
    cocos2d::Node* _pressed = nullptr;            // 0x264
    cocos2d::ClippingNode* _clipper = nullptr;    // 0x268, when the sliders scroll
    cocos2d::ClippingNode* _valueClipper = nullptr; // 0x26c
    cocos2d::Sprite* _bottom = nullptr;           // 0x270
    std::set<cocos2d::Node*> _touchable;          // 0x278
    std::vector<cocos2d::Node*> _sliders;         // 0x28c
    std::vector<cocos2d::Node*> _labels;          // 0x298
    std::vector<cocos2d::Node*> _valueNodes;      // 0x2a4
    std::vector<cocos2d::Node*> _filters;         // 0x2b0
    cocos2d::Size _charSize;                      // 0x2bc: a character's width, half a slider's height
    float _sliderHalfWidth = 0.0f;                // 0x2c4
    float _scrollerMinY = 0.0f;                   // 0x2c8
    float _scrollerMaxY = 0.0f;                   // 0x2cc
    float _scrollRange = 0.0f;                    // 0x2d0
    float _scrollerRatio = 0.0f;                  // 0x2d4
    int _state = kStateNone;                      // 0x2d8
    cocos2d::Sprite* _scroller = nullptr;         // 0x2dc
    cocos2d::Node* _content = nullptr;            // 0x2e0: sliders and labels
    cocos2d::Node* _values = nullptr;             // 0x2e4: the value rows and filters
    float _margin = 10.0f;                        // 0x2e8
    float _padding = 0.0f;                        // 0x2ec: margin less the top edge's height
    float _labelGap = 6.0f;                       // 0x2f0
    float _sliderInset = 3.0f;                    // 0x2f4
    float _screenMargin = 5.0f;                   // 0x2f8
    float _moveTime = 0.25f;                      // 0x304
    GameOptions* _options = nullptr;              // 0x324
};
