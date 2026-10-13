#include "game/actionsmenu.h"

#include "game/guiglobals.h"
#include "game/guiutils.h"
#include "game/weaponsmenu.h"

using namespace cocos2d;

namespace {

const Color3B kSoldiersColor(40, 177, 219);
const Color3B kWeaponsColor(240, 126, 26);
const Color3B kHelicoptersColor(121, 188, 49);

void setFrame(Sprite* sprite, const std::string& name)
{
    sprite->setSpriteFrame(SpriteFrameCache::getInstance()->getSpriteFrameByName(name));
}

} // namespace

ActionsMenu* ActionsMenu::create(ActionSelectedListener* listener, bool tutorialMode)
{
    ActionsMenu* menu = new ActionsMenu();
    if (!menu->init(listener, tutorialMode)) {
        delete menu;
        return nullptr;
    }
    menu->autorelease();
    return menu;
}

bool ActionsMenu::init(ActionSelectedListener* listener, bool tutorialMode)
{
    if (!Node::init())
        return false;
    _listener = listener;

    _panel = Sprite::createWithSpriteFrameName("Panel_Interact.png");
    const Size panel = _panel->getContentSize();
    addChild(_panel);

    // The tab sticks out of the panel's left edge, its touch area 60dp wide.
    _tab = Node::create();
    Sprite* tab = Sprite::createWithSpriteFrameName("Panel_Interact_Tab_Out.png");
    const Size tabSize = tab->getContentSize();
    const Size tabArea(GUIGlobals::dpToSize * 60.0f, tabSize.height);
    _tab->setContentSize(tabArea);
    _tab->setPosition(Vec2(-tabSize.width - (tabArea.width - tabSize.width) * 0.5f,
                           panel.height * 0.5f - tabSize.height * 0.5f));
    tab->setPosition(Vec2(tabArea.width * 0.5f, tabSize.height * 0.5f));
    _tab->addChild(tab);
    _panel->addChild(_tab);
    _touchable.insert(_tab);

    const float x = panel.width * 0.5f;
    _soldiers = GUIUtils::createButton(tutorialMode ? "Button_Soldiers_Disabled.png" : "Button_Soldiers.png",
                                       "Soldiers", _panel, x, panel.height * 0.8f, 0);
    if (!tutorialMode)
        _touchable.insert(_soldiers);
    _weapons = GUIUtils::createButton("Button_Weapons.png", "Weapons", _panel, x, panel.height * 0.5f, 1);
    _touchable.insert(_weapons);
    _helicopters = GUIUtils::createButton(
        tutorialMode ? "Button_Helicopters_Disabled.png" : "Button_Helicopters.png", "Helicopters", _panel, x,
        panel.height * 0.2f, 0);
    if (!tutorialMode)
        _touchable.insert(_helicopters);

    // Beside the weapons button, level with it.
    _weaponsMenu = WeaponsMenu::create(listener);
    const Size weaponsPanel = _weaponsMenu->getPanel()->getContentSize();
    _weaponsMenu->setPosition(Vec2(weaponsPanel.width * -0.5f, _weapons->getContentSize().height * 0.5f));
    _weapons->addChild(_weaponsMenu);
    _weaponsMenu->setVisible(false);

    auto touch = EventListenerTouchOneByOne::create();
    touch->onTouchBegan = [this](Touch* t, Event* e) { return onTouchBegan(t, e); };
    touch->onTouchMoved = [this](Touch* t, Event* e) { onTouchMoved(t, e); };
    touch->onTouchEnded = [this](Touch* t, Event* e) { onTouchEnded(t, e); };
    getEventDispatcher()->addEventListenerWithSceneGraphPriority(touch, this);
    _pressed = nullptr;
    return true;
}

void ActionsMenu::press(Node* button)
{
    GUIUtils::setPressed(button);
    Node* label = button->getChildByTag(1);
    if (!label)
        return;
    if (button == _soldiers)
        label->setColor(kSoldiersColor);
    else if (button == _weapons)
        label->setColor(kWeaponsColor);
    else if (button == _helicopters)
        label->setColor(kHelicoptersColor);
}

bool ActionsMenu::onTouchBegan(Touch* touch, Event*)
{
    if (!isVisible())
        return false;
    const Vec2 location = touch->getLocation();
    if (Node* hit = GUIUtils::touchedNode(_touchable, location)) {
        if (hit == _soldiers || hit == _weapons || hit == _helicopters) {
            press(hit);
            _pressed = hit;
        } else if (hit == _tab) {
            _tabPressed = true;
        }
        return true;
    }
    return GUIUtils::checkTouch(_panel, location);
}

void ActionsMenu::onTouchMoved(Touch*, Event* event)
{
    event->stopPropagation();
}

void ActionsMenu::onTouchEnded(Touch* touch, Event* event)
{
    Node* hit = GUIUtils::touchedNode(_touchable, touch->getLocation());
    if (hit && hit == _pressed) {
        if (hit == _weapons) {
            _weaponsMenu->setVisible(!_weaponsMenu->isVisible());
            // Held down while its menu is out.
            _pressed = _weaponsMenu->isVisible() ? nullptr : _weapons;
        } else if (hit == _soldiers) {
            GUIUtils::resetInteraction(_weapons);
            _weaponsMenu->setVisible(false);
            _listener->onActionSelected(ActionType::Soldiers);
        } else if (hit == _helicopters) {
            GUIUtils::resetInteraction(_weapons);
            _weaponsMenu->setVisible(false);
            _listener->onActionSelected(ActionType::Helicopters);
        }
    } else if (hit && _tabPressed && hit == _tab && !_weaponsMenu->isVisible()) {
        toggleMenu();
    }
    GUIUtils::resetInteraction(_pressed);
    _tabPressed = false;
    event->stopPropagation();
}

void ActionsMenu::openWeaponsMenu()
{
    _panel->setPositionX(-_panel->getContentSize().width);
    _open = true;
    if (!_weaponsMenu->isVisible()) {
        _weaponsMenu->setVisible(true);
        _pressed = _weapons;
        press(_weapons);
    }
}

void ActionsMenu::closeWeaponsMenu()
{
    if (_weaponsMenu->isVisible()) {
        _weaponsMenu->setVisible(false);
        GUIUtils::resetInteraction(_weapons);
        _pressed = nullptr;
    }
}

void ActionsMenu::toggleMenu()
{
    Sprite* tab = static_cast<Sprite*>(_tab->getChildren()[0]);
    const float y = _panel->getPositionY();
    if (!_open) {
        setFrame(tab, "Panel_Interact_Tab_In.png");
        _panel->runAction(MoveTo::create(0.25f, Vec2(-_panel->getContentSize().width, y)));
    } else {
        setFrame(tab, "Panel_Interact_Tab_Out.png");
        _panel->runAction(MoveTo::create(0.25f, Vec2(0.0f, y)));
    }
    _open = !_open;
    _listener->onActionsMenuToggled(_open);
}

void ActionsMenu::enableSoldiersButton()
{
    setFrame(_soldiers, "Button_Soldiers.png");
    if (GUIUtils::ButtonData* data = GUIUtils::getButtonData(_soldiers)) {
        data->normal = "Button_Soldiers.png";
        data->pressed = "Button_Soldiers_Press.png";
    }
    _touchable.insert(_soldiers);
}

void ActionsMenu::enableHelicoptersButton()
{
    setFrame(_helicopters, "Button_Helicopters.png");
    if (GUIUtils::ButtonData* data = GUIUtils::getButtonData(_helicopters)) {
        data->normal = "Button_Helicopters.png";
        data->pressed = "Button_Helicopters_Press.png";
    }
    _touchable.insert(_helicopters);
}
