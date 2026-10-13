#include "game/settingsmenu.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "game/gamenode.h"
#include "game/gameoptions.h"
#include "game/guiglobals.h"
#include "game/guiutils.h"

using namespace cocos2d;

SettingsMenu* SettingsMenu::create(GameNode* game, SettingsSelectedListener* listener, bool tutorialMode)
{
    SettingsMenu* menu = new SettingsMenu();
    if (!menu->init(game, listener, tutorialMode)) {
        delete menu;
        return nullptr;
    }
    menu->autorelease();
    return menu;
}

bool SettingsMenu::init(GameNode* game, SettingsSelectedListener* listener, bool tutorialMode)
{
    if (!Node::init())
        return false;
    _game = game;
    _listener = listener;
    _state = kStateNone;
    const float dp = GUIGlobals::dpToSize;
    _margin *= dp;
    _labelGap *= dp;
    _screenMargin *= dp;
    _sliderInset *= dp;

    _padding = _margin - GUIUtils::spriteSizeFromFrameCache("SettingsPanel_Top.png").height;
    // The buttons' row is as tall as the game's Unlock Settings button.
    const Size button = GUIUtils::spriteSizeFromFrameCache("Button_SettingsPanel_UnlockSettings.png");
    const Size slider = GUIUtils::spriteSizeFromFrameCache("SettingsPanel_Slider.png");
    _options = &game->getGameOptions();
    const int numOptions = _options->getNumOptions();

    _bar = createClippedBar(2.0f * _padding + button.height + (float)numOptions * slider.height, tutorialMode);
    const Size bar = _bar->getContentSize();

    Sprite* top = Sprite::createWithSpriteFrameName("SettingsPanel_Top.png");
    const float edge = top->getContentSize().height;
    top->setPosition(Vec2(bar.width * 0.5f, edge * 0.5f + bar.height));
    _bar->addChild(top);
    _bottom = Sprite::createWithSpriteFrameName("SettingsPanel_Bottom.png");
    _bottom->setPosition(Vec2(bar.width * 0.5f, edge * -0.5f));
    _bar->addChild(_bottom);

    createTitle(_bar, bar.height, button.height, _padding);
    _restart = createSettingsButton("Button_SettingsPanel_Restart.png", _bar, bar.width - button.width * 0.5f - _margin,
                                    bar.height - button.height * 0.5f - _padding);
    createButtonText("Restart", _restart, button.width * 0.725f, button.height * 0.5f);
    _touchable.insert(_restart);

    // The sliders' area, under the buttons' row, and its top.
    const float area = bar.height - _padding - button.height;
    const float areaTop = bar.height - 2.0f * _padding - button.height;

    _content = Node::create();
    if ((float)numOptions * slider.height <= area) {
        _content->setPositionY(_padding);
        _bar->addChild(_content);
    } else {
        _scroller = Sprite::createWithSpriteFrameName("SettingsPanel_Scroller.png");
        const Size scroller = _scroller->getContentSize();
        _scroller->setPosition(
            Vec2(bar.width - scroller.width * 0.5f - _margin, areaTop - scroller.height * 0.5f - _padding));
        _bar->addChild(_scroller);
        _scrollerMinY = scroller.height * 0.5f + _padding;
        _scrollerMaxY = area + _scrollerMinY - scroller.height - 2.0f * _padding;
        _clipper = GUIUtils::createClipper(bar.width, area - _padding);
        _clipper->setPositionY(_padding);
        _bar->addChild(_clipper);
        _clipper->addChild(_content);
    }

    // The values scroll with the sliders, clipped to the sliders' width.
    _valueClipper = GUIUtils::createClipper(slider.width - 1.0f, area - _padding);
    _valueClipper->setPositionX(_margin - _sliderInset);
    _valueClipper->setPositionY(_padding);
    _bar->addChild(_valueClipper);
    _values = Node::create();
    _valueClipper->addChild(_values);

    const float labelSize = (float)(int)(dp * 21.0f);
    _charSize = Size(GUIUtils::textDimensionsFromFontSize(GUIGlobals::kFont, dp * 24.0f).width, slider.height * 0.5f);
    const float firstY = areaTop - slider.height * 0.5f;
    _sliderHalfWidth = slider.width * 0.5f;
    const float sliderX = slider.width * 0.5f + _margin - _sliderInset;
    for (int i = 0; i < numOptions; i++) {
        GameOptions::Option* option = _options->getOption(i);
        const float y = firstY - (float)i * slider.height;

        Sprite* s = Sprite::createWithSpriteFrameName("SettingsPanel_Slider.png");
        s->setPosition(Vec2(sliderX, y));
        _content->addChild(s);
        _sliders.push_back(s);

        Node* row = Node::create();
        row->setPositionY(y - _charSize.height);
        _values->addChild(row);
        addOptionValues(_options, i, row, _charSize);

        Sprite* filter = Sprite::createWithSpriteFrameName("Panel_Settings_Slider_Filter.png");
        filter->setPositionX(filter->getContentSize().width * 0.5f);
        filter->setPositionY(y);
        _values->addChild(filter);
        _filters.push_back(filter);

        // The selected value under the slider's middle.
        row->setPositionX((float)(-option->getSelectedValueIndex() * (option->getLongestFormattedValueLength() + 1)) *
                              _charSize.width +
                          _sliderHalfWidth);
        _valueNodes.push_back(row);

        Label* label = Label::createWithTTF(option->getLabel(), GUIGlobals::kFont, labelSize);
        label->setAnchorPoint(Vec2(0.0f, 0.5f));
        label->setPosition(Vec2(_labelGap + slider.width * 0.5f + sliderX, y));
        _content->addChild(label);
        _labels.push_back(label);
    }
    // How far the content scrolls up: its last slider's bottom to the
    // area's.
    _scrollRange = -((firstY - slider.height * 0.5f) - (float)(numOptions - 1) * slider.height);
    _scrollerRatio = (_scrollerMaxY - _scrollerMinY) / _scrollRange;
    _pressed = nullptr;

    auto touch = EventListenerTouchOneByOne::create();
    touch->onTouchBegan = [this](Touch* t, Event* e) { return onTouchBegan(t, e); };
    touch->onTouchMoved = [this](Touch* t, Event* e) { onTouchMoved(t, e); };
    touch->onTouchEnded = [this](Touch* t, Event* e) { onTouchEnded(t, e); };
    getEventDispatcher()->addEventListenerWithSceneGraphPriority(touch, this);
    return true;
}

Size SettingsMenu::getMenuSize() const
{
    const Size bar = _bar->getContentSize();
    return Size(bar.width, bar.height + 2.0f * _padding);
}

Sprite* SettingsMenu::createClippedBar(float height, bool tutorialMode)
{
    SpriteFrame* frame = SpriteFrameCache::getInstance()->getSpriteFrameByName("SettingsPanel_Body.png");
    const Size visible = Director::getInstance()->getVisibleSize();
    Rect rect = frame->getRect();
    float fits = visible.height - 2.0f * _screenMargin - 2.0f * _padding;
    if (tutorialMode)
        fits -= GUIGlobals::dpToSize * 60.0f;
    rect.size.height = std::min(rect.size.height, std::min(height, fits));
    Sprite* bar = Sprite::createWithTexture(frame->getTexture(), rect);
    addChild(bar);
    return bar;
}

void SettingsMenu::createTitle(Node* bar, float barHeight, float buttonHeight, float padding)
{
    Sprite* title = Sprite::createWithSpriteFrameName("SettingsPanel_Title.png");
    const Size size = title->getContentSize();
    // Level with the buttons.
    title->setPosition(Vec2(size.width * 0.5f + _margin,
                            barHeight - padding - size.height * 0.5f - (buttonHeight - size.height) * 0.5f));
    bar->addChild(title);
}

Sprite* SettingsMenu::createSettingsButton(const std::string& frame, Node* bar, float x, float y)
{
    Sprite* button = Sprite::createWithSpriteFrameName(frame);
    GUIUtils::createButtonData(button, frame, Color3B(255, 255, 255));
    button->setPosition(Vec2(x, y));
    bar->addChild(button, 1);
    return button;
}

Label* SettingsMenu::createButtonText(const std::string& text, Node* button, float x, float y)
{
    Label* label = Label::createWithTTF(text, GUIGlobals::kFont, GUIGlobals::dpToSize * 16.0f);
    label->setPosition(Vec2(x, y));
    button->addChild(label, 0, 1);
    return label;
}

void SettingsMenu::addOptionValues(GameOptions* options, int index, Node* node, const Size& charSize)
{
    const GameOptions::Option* option = options->getOption(index);
    const float step = (float)(option->getLongestFormattedValueLength() + 1) * charSize.width;
    for (int i = 0; i < option->getNumValues(); i++) {
        Label* label =
            Label::createWithTTF(option->getValue(i).getFormattedValue(), GUIGlobals::kFont, GUIGlobals::dpToSize * 24.0f);
        label->setColor(Color3B(0, 0, 0));
        label->setPosition(Vec2(step * (float)i, charSize.height));
        node->addChild(label);
    }
}

int SettingsMenu::sliderValueIndex(int index) const
{
    const GameOptions::Option* option = _options->getOption(index);
    const float step = (float)(option->getLongestFormattedValueLength() + 1) * _charSize.width;
    return std::abs((int)roundf((_valueNodes[index]->getPositionX() - _sliderHalfWidth) / step));
}

bool SettingsMenu::onTouchBegan(Touch* touch, Event* event)
{
    if (!isVisible())
        return false;
    const Vec2 location = touch->getLocation();
    if (Node* hit = GUIUtils::touchedNode(_touchable, location)) {
        if (hit == _restart) {
            GUIUtils::setPressed(hit);
            _pressed = hit;
            _state = kStateButton;
        }
        event->stopPropagation();
        return true;
    }
    // The port's: a slider scrolled out of the area is not there to touch
    // (the game took touches on it over the title, or below the panel).
    bool inArea = true;
    if (_clipper) {
        const Size area = _clipper->getStencil()->getContentSize();
        inArea = Rect(0.0f, 0.0f, area.width, area.height).containsPoint(_clipper->convertToNodeSpace(location));
    }
    for (size_t i = 0; inArea && i < _sliders.size(); i++)
        if (GUIUtils::checkTouch(_sliders[i], location)) {
            _state = (int)i;
            event->stopPropagation();
            return true;
        }
    if (GUIUtils::checkTouch(_bar, location)) {
        event->stopPropagation();
        return true;
    }
    return false;
}

void SettingsMenu::onTouchMoved(Touch* touch, Event* event)
{
    if (!isVisible())
        return;
    const Vec2 delta = touch->getDelta();
    if (_state < 0) {
        if (_state != kStateNone || !_scroller)
            return;
        // Drag the panel: the content and values follow, the thumb goes the
        // other way.
        _scroller->setPositionY(
            std::max(_scrollerMinY, std::min(_scrollerMaxY, _scroller->getPositionY() - delta.y * _scrollerRatio)));
        const float y = std::max(0.0f, std::min(_scrollRange, _content->getPositionY() + delta.y));
        _content->setPositionY(y);
        _values->setPositionY(y);
    } else {
        // Drag the slider's values, its first to its last under the middle.
        const GameOptions::Option* option = _options->getOption(_state);
        const float last = _sliderHalfWidth + _charSize.width * (float)((option->getLongestFormattedValueLength() + 1) *
                                                                        (1 - option->getNumValues()));
        Node* row = _valueNodes[_state];
        row->setPositionX(std::max(last, std::min(_sliderHalfWidth, row->getPositionX() + delta.x)));
    }
    event->stopPropagation();
}

void SettingsMenu::onTouchEnded(Touch* touch, Event*)
{
    if (!isVisible())
        return;
    const Vec2 location = touch->getLocation();
    if (_state >= 0) {
        // Snap to the nearest value.
        GameOptions::Option* option = _options->getOption(_state);
        const int index = sliderValueIndex(_state);
        option->setSelectedValueIndex(index);
        Node* row = _valueNodes[_state];
        const float step = (float)(option->getLongestFormattedValueLength() + 1) * _charSize.width;
        row->runAction(MoveTo::create(_moveTime, Vec2((float)-index * step + _sliderHalfWidth, row->getPositionY())));
    }
    _state = kStateNone;

    Node* hit = GUIUtils::touchedNode(_touchable, location);
    if (hit && hit == _pressed && hit == _restart) {
        for (int i = 0; i < _options->getNumOptions(); i++)
            _options->getOption(i)->setSelectedValueIndex(sliderValueIndex(i));
        _game->restartSimulation();
        _listener->onSettingsSelected(SettingsSelectedListener::Restart);
        setVisible(false);
    }
    GUIUtils::resetInteraction(_pressed);
    _pressed = nullptr;
}
