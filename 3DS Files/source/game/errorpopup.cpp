#include "game/errorpopup.h"

#include <algorithm>
#include <cmath>

#include "game/guiglobals.h"
#include "game/guiutils.h"

using namespace cocos2d;

ErrorPopup* ErrorPopup::create(const std::string& text, int size, const std::string& button,
                               const std::string& buttonLabel, const std::string& cancelButton)
{
    ErrorPopup* popup = new ErrorPopup();
    if (!popup->init(text, size, button, buttonLabel, cancelButton)) {
        delete popup;
        return nullptr;
    }
    popup->autorelease();
    return popup;
}

bool ErrorPopup::init(const std::string& text, int size, const std::string& button, const std::string& buttonLabel,
                      const std::string& cancelButton)
{
    if (!Node::init())
        return false;
    const Size panelTop = GUIUtils::spriteSizeFromFrameCache("Popups_PanelTop.png");
    const Size buttonSize = GUIUtils::spriteSizeFromFrameCache(button);
    const float margin = GUIGlobals::dpToSize * 12.0f;
    Label* label = Label::createWithTTF(text, GUIGlobals::kFont, (float)size * GUIGlobals::dpToSize);
    label->setAlignment(TextHAlignment::CENTER);
    const Size labelSize = label->getContentSize();

    // The box: the text's width and a tenth, centred on the popup (the
    // port's: or two buttons' and their margins).
    float w = labelSize.width * 1.1f;
    if (!cancelButton.empty())
        w = std::max(w, buttonSize.width * 2.0f + margin * 3.0f);
    float h = margin * 3.0f + panelTop.height + buttonSize.height + labelSize.height;
    if (!buttonLabel.empty())
        h += GUIGlobals::dpToSize * 5.0f;
    _box = DrawNode::create(2.0f);
    const Vec2 corners[4] = {Vec2(-w * 0.5f, -h * 0.5f), Vec2(w * 0.5f, -h * 0.5f), Vec2(w * 0.5f, h * 0.5f),
                             Vec2(-w * 0.5f, h * 0.5f)};
    const Color4F black(0.0f, 0.0f, 0.0f, 1.0f);
    _box->drawPolygon(corners, 4, black, 0.0f, black);
    _box->setContentSize(Size(w, h));
    addChild(_box, 1);

    // The panel top across it, the last piece cut to fit.
    const int tiles = (int)(w / panelTop.width);
    const Vec2 first(-w * 0.5f + panelTop.width * 0.5f, h * 0.5f - panelTop.height * 0.5f);
    for (int i = 0; i < tiles; ++i) {
        Sprite* tile = Sprite::createWithSpriteFrameName("Popups_PanelTop.png");
        tile->setPosition(Vec2(first.x + (float)i * panelTop.width, first.y));
        _box->addChild(tile, 2);
    }
    double whole;
    const float part = (float)std::modf((double)(w / panelTop.width), &whole);
    Sprite* last = Sprite::createWithSpriteFrameName("Popups_PanelTop.png");
    Rect rect = last->getTextureRect();
    rect.size.width *= part;
    last->setTextureRect(rect);
    last->setAnchorPoint(Vec2(0.0f, 0.5f));
    last->setPosition(Vec2((float)tiles * panelTop.width - w * 0.5f, first.y));
    _box->addChild(last, 2);

    label->setPosition(Vec2(0.0f, h * 0.5f - labelSize.height * 0.5f - panelTop.height - margin));
    _box->addChild(label, 1);
    _label = label;

    _button = Sprite::createWithSpriteFrameName(button);
    GUIUtils::createButtonData(_button, button, Color3B(255, 255, 255));
    _button->setPosition(
        Vec2(0.0f, label->getPositionY() - labelSize.height * 0.5f - margin - buttonSize.height * 0.5f));
    _box->addChild(_button, 1);
    if (!cancelButton.empty()) {
        const float apart = (buttonSize.width + margin) * 0.5f;
        _button->setPositionX(-apart);
        _cancel = Sprite::createWithSpriteFrameName(cancelButton);
        GUIUtils::createButtonData(_cancel, cancelButton, Color3B(255, 255, 255));
        _cancel->setPosition(Vec2(apart, _button->getPositionY()));
        _box->addChild(_cancel, 1);
    }
    if (!buttonLabel.empty()) {
        Label* under = Label::createWithTTF(buttonLabel, GUIGlobals::kFont, GUIGlobals::dpToSize * 18.0f);
        under->setPosition(Vec2(0.0f, _button->getPositionY() - _button->getContentSize().height * 0.5f -
                                          under->getContentSize().height * 0.5f));
        _box->addChild(under);
    }

    auto touch = EventListenerTouchOneByOne::create();
    touch->onTouchBegan = [this](Touch* t, Event* e) { return onTouchBegan(t, e); };
    touch->onTouchEnded = [this](Touch* t, Event* e) { onTouchEnded(t, e); };
    getEventDispatcher()->addEventListenerWithSceneGraphPriority(touch, this);
    return true;
}

void ErrorPopup::setText(const std::string& text)
{
    _label->setString(text);
}

bool ErrorPopup::onTouchBegan(Touch* touch, Event*)
{
    if (!isVisible())
        return false;
    if (GUIUtils::checkTouch(_button, touch->getLocation())) {
        GUIUtils::setPressed(_button);
        _pressed = _button;
    } else if (_cancel && GUIUtils::checkTouch(_cancel, touch->getLocation())) {
        GUIUtils::setPressed(_cancel);
        _pressed = _cancel;
    }
    return true;
}

void ErrorPopup::onTouchEnded(Touch* touch, Event*)
{
    if (!isVisible())
        return;
    Node* pressed = _pressed;
    const bool closed = pressed && GUIUtils::checkTouch(pressed, touch->getLocation());
    // The game reset the button only when it closed the popup.
    GUIUtils::resetInteraction(_pressed);
    _pressed = nullptr;
    if (closed)
        close(pressed == _button);
}

void ErrorPopup::close(bool confirmed)
{
    if (_pressed) {
        GUIUtils::resetInteraction(_pressed);
        _pressed = nullptr;
    }
    _confirmed = confirmed;
    setVisible(false);
    if (_listener)
        _listener->onErrorPopupClosed(this);
}
