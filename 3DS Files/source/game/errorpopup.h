// ErrorPopup (decomp/classes/ErrorPopup.c): MapSelectScene's message box. A
// black box under the popups' panel top, its text, and one button (with an
// optional label under it) that hides the box and tells the listener. The
// popup is centred on its position.
//
// The port's: a second button beside the first (MapSelectScene's question
// before deleting a player map, Confirm and Cancel), and which one closed it.
#pragma once

#include <string>

#include "cocos/director.h"

class ErrorPopup : public cocos2d::Node {
public:
    class IListener {
    public:
        virtual ~IListener() = default;
        virtual void onErrorPopupClosed(ErrorPopup* popup) = 0;
    };

    // text at size dp in the GUI's font; button, the button's frame;
    // buttonLabel, 18 dp under it, none if empty; cancelButton, the port's
    // second button's frame, right of the first, none if empty.
    static ErrorPopup* create(const std::string& text, int size, const std::string& button,
                              const std::string& buttonLabel, const std::string& cancelButton = "");

    bool init(const std::string& text, int size, const std::string& button, const std::string& buttonLabel,
              const std::string& cancelButton = "");
    void setListener(IListener* listener) { _listener = listener; }
    // The port's: new text, as many lines as before (the box keeps its size).
    // MapSelectScene's download popup shows its progress on its empty second
    // line.
    void setText(const std::string& text);
    // Closed by the first button (or close(true)), not the second.
    bool wasConfirmed() const { return _confirmed; }
    // Hides it and tells the listener, as a button would (the 3DS's A and B).
    void close(bool confirmed);

private:
    bool onTouchBegan(cocos2d::Touch* touch, cocos2d::Event* event);
    void onTouchEnded(cocos2d::Touch* touch, cocos2d::Event* event);

    cocos2d::DrawNode* _box = nullptr;  // 0x240
    cocos2d::Sprite* _button = nullptr; // 0x244
    cocos2d::Node* _pressed = nullptr;  // 0x248
    IListener* _listener = nullptr;     // 0x24c
    cocos2d::Label* _label = nullptr;
    cocos2d::Sprite* _cancel = nullptr;
    bool _confirmed = false;
};
