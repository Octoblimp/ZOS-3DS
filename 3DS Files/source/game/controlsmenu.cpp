#include "game/controlsmenu.h"

#include "game/gamenode.h"
#include "game/guiglobals.h"
#include "game/guiutils.h"
#include "game/mapselectscene.h"
#include "game/prefs.h"
#include "game/settingsmenu.h"
#include "game/utils.h"

using namespace cocos2d;

namespace {

const Color3B kChangeMapColor(82, 184, 160);
const Color3B kSettingsColor(210, 189, 64);

} // namespace

ControlsMenu* ControlsMenu::create(GameNode* game, ControlSelectedListener* listener, bool tutorialMode)
{
    ControlsMenu* menu = new ControlsMenu();
    if (!menu->init(game, listener, tutorialMode)) {
        delete menu;
        return nullptr;
    }
    menu->autorelease();
    return menu;
}

bool ControlsMenu::init(GameNode* game, ControlSelectedListener* listener, bool tutorialMode)
{
    if (!Node::init())
        return false;
    const Size visible = Director::getInstance()->getVisibleSize();
    _game = game;
    _listener = listener;

    _panel = Sprite::createWithSpriteFrameName("Panel_Options.png");
    const Size panel = _panel->getContentSize();
    addChild(_panel);

    // The tab sticks out of the panel's right edge, its touch area 60dp wide.
    _tab = Node::create();
    Sprite* tab = Sprite::createWithSpriteFrameName("Panel_Options_Tab.png");
    const Size tabSize = tab->getContentSize();
    const Size tabArea(GUIGlobals::dpToSize * 60.0f, tabSize.height);
    _tab->setPosition(
        Vec2(panel.width - (tabArea.width - tabSize.width) * 0.5f, panel.height * 0.5f - tabSize.height * 0.5f));
    tab->setPosition(Vec2(tabArea.width * 0.5f, tabSize.height * 0.5f));
    _tab->setContentSize(tabArea);
    _tab->addChild(tab);
    _panel->addChild(_tab);
    _touchable.insert(_tab);

    const float x = panel.width * 0.5f;
    _changeMap = GUIUtils::createButton(tutorialMode ? "Button_Map_Select_Disabled.png" : "Button_Map_Select.png",
                                        "Change map", _panel, x, panel.height * 0.7f, 0);
    if (!tutorialMode)
        _touchable.insert(_changeMap);
    _settings = GUIUtils::createButton("Button_Settings.png", "Settings", _panel, x, panel.height * 0.25f, 0);
    const Size settings = _settings->getContentSize();
    _touchable.insert(_settings);

    // The settings to the panel's right, the arrow on their left edge level
    // with the button; in the tutorial both 30dp up, clear of its bar.
    Sprite* arrow = Sprite::createWithSpriteFrameName("SettingsPanel_Arrow.png");
    const Size arrowSize = arrow->getContentSize();
    _settingsMenu = SettingsMenu::create(game, this, tutorialMode);
    const Size menu = _settingsMenu->getMenuSize();
    float y = panel.height * 0.5f;
    if (tutorialMode)
        y += GUIGlobals::dpToSize * 60.0f * 0.5f;
    _settingsMenu->setPosition(Vec2(arrowSize.width + x + settings.width * 0.5f + menu.width * 0.5f, y));
    _settingsMenu->setVisible(false);
    _panel->addChild(_settingsMenu);
    float arrowY = panel.height * -0.25f;
    if (tutorialMode)
        arrowY += GUIGlobals::dpToSize * -60.0f * 0.5f;
    arrow->setPosition(Vec2(menu.width * -0.5f + arrowSize.width * -0.5f, arrowY));
    _settingsMenu->addChild(arrow);
    // Kept 5dp inside the screen's right edge.
    if (visible.width < _settingsMenu->getPositionX() + menu.width * 0.5f + GUIGlobals::dpToSize * 5.0f)
        _settingsMenu->setPositionX(visible.width - menu.width * 0.5f - GUIGlobals::dpToSize * 5.0f);

    _pressed = nullptr;
    auto touch = EventListenerTouchOneByOne::create();
    touch->onTouchBegan = [this](Touch* t, Event* e) { return onTouchBegan(t, e); };
    touch->onTouchMoved = [this](Touch* t, Event* e) { onTouchMoved(t, e); };
    touch->onTouchEnded = [this](Touch* t, Event* e) { onTouchEnded(t, e); };
    getEventDispatcher()->addEventListenerWithSceneGraphPriority(touch, this);
    return true;
}

bool ControlsMenu::isSettingsMenuVisible() const
{
    return _settingsMenu->isVisible();
}

void ControlsMenu::press(Node* button)
{
    GUIUtils::setPressed(button);
    Node* label = button->getChildByTag(1);
    if (!label)
        return;
    if (button == _changeMap)
        label->setColor(kChangeMapColor);
    else if (button == _settings)
        label->setColor(kSettingsColor);
}

bool ControlsMenu::onTouchBegan(Touch* touch, Event*)
{
    if (!isVisible())
        return false;
    const Vec2 location = touch->getLocation();
    if (Node* hit = GUIUtils::touchedNode(_touchable, location)) {
        if (hit == _changeMap || hit == _settings) {
            press(hit);
            _pressed = hit;
        } else if (hit == _tab) {
            _tabPressed = true;
        }
        return true;
    }
    return GUIUtils::checkTouch(_panel, location);
}

void ControlsMenu::onTouchMoved(Touch*, Event* event)
{
    event->stopPropagation();
}

void ControlsMenu::onTouchEnded(Touch* touch, Event* event)
{
    Node* hit = GUIUtils::touchedNode(_touchable, touch->getLocation());
    if (hit && hit == _pressed) {
        if (hit == _settings) {
            toggleSettingsMenu();
        } else if (hit == _changeMap) {
            getEventDispatcher()->removeAllEventListeners();
            Director::getInstance()->replaceScene(
                MapSelectTransition::create(0.5f, MapSelectScene::createScene(_game, _game->getTutorialMode())));
            if (_game->getTutorialMode())
                Prefs::getInstance().setTutorialViewed();
        }
    } else if (hit && _tabPressed && hit == _tab && !_settingsMenu->isVisible()) {
        toggleMenu();
    }
    GUIUtils::resetInteraction(_pressed);
    _tabPressed = false;
    event->stopPropagation();
}

void ControlsMenu::onSettingsSelected(SettingsType type)
{
    GUIUtils::resetInteraction(_settings);
    _listener->onSettingsSelected(false);
    if (type == Restart)
        _listener->onSettingsRestart();
}

void ControlsMenu::openSettingsMenu()
{
    setPositionX(_panel->getContentSize().width * 0.5f);
    _open = true;
    if (!_settingsMenu->isVisible()) {
        toggleSettingsMenu();
        press(_settings);
    }
}

void ControlsMenu::toggleSettingsMenu()
{
    _settingsMenu->setVisible(!_settingsMenu->isVisible());
    _listener->onSettingsSelected(_settingsMenu->isVisible());
    // Held down while the settings are up.
    _pressed = _settingsMenu->isVisible() ? nullptr : _settings;
}

void ControlsMenu::toggleMenu()
{
    const float direction = _open ? -1.0f : 1.0f;
    runAction(MoveTo::create(0.25f, Vec2(getPositionX() + direction * _panel->getContentSize().width, getPositionY())));
    _open = !_open;
}

void ControlsMenu::enableChangeMapButton()
{
    _changeMap->setSpriteFrame(SpriteFrameCache::getInstance()->getSpriteFrameByName("Button_Map_Select.png"));
    if (GUIUtils::ButtonData* data = GUIUtils::getButtonData(_changeMap)) {
        data->normal = "Button_Map_Select.png";
        data->pressed = "Button_Map_Select_Press.png";
    }
    _touchable.insert(_changeMap);
}
