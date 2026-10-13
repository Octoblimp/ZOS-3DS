#include "game/weaponsmenu.h"

#include "game/guiutils.h"

using namespace cocos2d;

namespace {

const Color3B kWeaponsColor(240, 126, 26);

} // namespace

WeaponsMenu* WeaponsMenu::create(ActionSelectedListener* listener)
{
    WeaponsMenu* menu = new WeaponsMenu();
    if (!menu->init(listener)) {
        delete menu;
        return nullptr;
    }
    menu->autorelease();
    return menu;
}

bool WeaponsMenu::init(ActionSelectedListener* listener)
{
    if (!Node::init())
        return false;
    _listener = listener;

    _panel = Sprite::createWithSpriteFrameName("Panel_Weapons.png");
    const Size panel = _panel->getContentSize();
    addChild(_panel);

    // The buttons sit in the panel's left 77%, its right edge the panel's
    // tab towards the weapons button.
    const float x = panel.width * 0.7723214f * 0.5f;
    _mk81 = Sprite::createWithSpriteFrameName("Button_MK81.png");
    GUIUtils::createButtonData(_mk81, "Button_MK81.png", kWeaponsColor);
    _mk81->setPosition(Vec2(x, panel.height * 0.765f));
    GUIUtils::createButtonLabel(_mk81, "MK81")->setColor(kWeaponsColor);
    _panel->addChild(_mk81);
    _touchable.insert(_mk81);

    _mk82 = Sprite::createWithSpriteFrameName("Button_MK82.png");
    GUIUtils::createButtonData(_mk82, "Button_MK82.png", kWeaponsColor);
    _mk82->setPosition(Vec2(x, panel.height * 0.285f));
    GUIUtils::createButtonLabel(_mk82, "MK82")->setColor(kWeaponsColor);
    _panel->addChild(_mk82);
    _touchable.insert(_mk82);

    auto touch = EventListenerTouchOneByOne::create();
    touch->onTouchBegan = [this](Touch* t, Event* e) { return onTouchBegan(t, e); };
    touch->onTouchMoved = [this](Touch* t, Event* e) { onTouchMoved(t, e); };
    touch->onTouchEnded = [this](Touch* t, Event* e) { onTouchEnded(t, e); };
    getEventDispatcher()->addEventListenerWithSceneGraphPriority(touch, this);
    _pressed = nullptr;
    return true;
}

bool WeaponsMenu::onTouchBegan(Touch* touch, Event*)
{
    if (!isVisible())
        return false;
    const Vec2 location = touch->getLocation();
    if (Node* button = GUIUtils::touchedNode(_touchable, location)) {
        GUIUtils::setPressed(button);
        _pressed = button;
        return true;
    }
    // The panel itself takes the touch from the map.
    return GUIUtils::checkTouch(_panel, location);
}

void WeaponsMenu::onTouchMoved(Touch*, Event* event)
{
    event->stopPropagation();
}

void WeaponsMenu::onTouchEnded(Touch* touch, Event* event)
{
    Node* button = GUIUtils::touchedNode(_touchable, touch->getLocation());
    if (button && button == _pressed) {
        if (button == _mk81)
            _listener->onActionSelected(ActionType::Mk81Bombs);
        else if (button == _mk82)
            _listener->onActionSelected(ActionType::Mk82Bombs);
    }
    GUIUtils::resetInteraction(_pressed);
    event->stopPropagation();
}
