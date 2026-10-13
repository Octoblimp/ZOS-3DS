#include "game/creditsscene.h"

#include <3ds.h>

#include <algorithm>
#include <cstdlib>

#include "game/gamenode.h"
#include "game/guiglobals.h"
#include "game/guiutils.h"
#include "game/homescene.h"
#include "game/hometopscreen.h"

using namespace cocos2d;

namespace {

// In place of the game's second text, Android's software licenses: the
// libraries the 3DS build links.
const char* const kPortCredits =
    "Nintendo 3DS port\n"
    "\n"
    "Software Licenses\n"
    "This product includes libctru and citro3d from devkitPro, under the zlib license\n"
    "This product includes zlib from Jean-loup Gailly and Mark Adler\n"
    "Copyright (C) 1995-2023 Jean-loup Gailly and Mark Adler\n"
    "This product includes libcurl 8.4.0, under the curl license\n"
    "Copyright (C) Daniel Stenberg, <daniel@haxx.se>, and many contributors\n"
    "This product includes Mbed TLS 2.28.8, under the Apache License 2.0\n"
    "Copyright The Mbed TLS Contributors\n";

// The Circle Pad's and D-pad's scroll, in canvas pixels a second.
const float kScrollSpeed = 480.0f;
const int kCirclePadMax = 156;
const int kCirclePadDeadZone = 16;

} // namespace

Scene* CreditsScene::createScene(GameNode* game)
{
    Scene* scene = Scene::create();
    scene->addChild(create(game));
    return scene;
}

CreditsScene* CreditsScene::create(GameNode* game)
{
    CreditsScene* scene = new CreditsScene();
    if (!scene->init(game)) {
        delete scene;
        return nullptr;
    }
    scene->autorelease();
    return scene;
}

CreditsScene::~CreditsScene()
{
    CC_SAFE_RELEASE(_topScreen);
}

bool CreditsScene::init(GameNode* game)
{
    if (!Layer::init())
        return false;
    _game = game;
    const Size visible = Director::getInstance()->getVisibleSize();
    const float dp = GUIGlobals::dpToSize;

    Sprite* background = Sprite::create("Background_CreditsScreen.png");
    background->setAnchorPoint(Vec2(0.5f, 0.65f));
    const Size backgroundSize = background->getContentSize();
    background->setPosition(Vec2(visible.width * 0.5f, visible.height * 0.7f));
    background->setScale(visible.width / backgroundSize.width);
    addChild(background);

    _home = Sprite::createWithSpriteFrameName("Button_Home.png");
    const Size homeSize = _home->getContentSize();
    GUIUtils::createButtonData(_home, "Button_Home.png", Color3B(255, 255, 255));
    _home->setPosition(Vec2(visible.width - homeSize.width * 0.5f, visible.height - homeSize.height * 0.5f));
    addChild(_home, 2);
    _touchable.insert(_home);

    _content = Node::create();
    addChild(_content, 1);
    Sprite* logos = Sprite::createWithSpriteFrameName("Credits_Logos.png");
    const Size logosSize = logos->getContentSize();
    logos->setPosition(Vec2(visible.width * 0.5f, (visible.height - homeSize.height) - logosSize.height * 0.5f));
    _content->addChild(logos);

    // The texts one under the other, below the logos.
    const Size dimensions(visible.width - dp * 20.0f - dp * 20.0f, 0.0f);
    const char* const texts[] = {kGameCredits, kPortCredits};
    int height = 0;
    for (const char* text : texts) {
        Label* label = Label::createWithTTF(text, "fonts/Open Sans.ttf", dp * 16.0f, dimensions,
                                            TextHAlignment::CENTER, TextVAlignment::TOP);
        const Size size = label->getContentSize();
        label->setPosition(Vec2(visible.width * 0.5f,
                                (visible.height - homeSize.height - logosSize.height) - size.height * 0.5f - (float)height));
        _content->addChild(label);
        height = (int)((float)height + size.height);
    }
    _maxScroll = (float)height - visible.height * 0.5f;

    _topScreen = HomeTopScreen::create(_game);
    _topScreen->retain();

    auto touch = EventListenerTouchOneByOne::create();
    touch->onTouchBegan = [this](Touch* t, Event* e) { return onTouchBegan(t, e); };
    touch->onTouchMoved = [this](Touch* t, Event* e) { onTouchMoved(t, e); };
    touch->onTouchEnded = [this](Touch* t, Event* e) { onTouchEnded(t, e); };
    getEventDispatcher()->addEventListenerWithSceneGraphPriority(touch, this);
    scheduleUpdate();
    return true;
}

void CreditsScene::onEnter()
{
    Layer::onEnter();
    Director::getInstance()->setTopScreenNode(_topScreen);
}

void CreditsScene::onExit()
{
    Layer::onExit();
    Director* director = Director::getInstance();
    if (director->getTopScreenNode() == _topScreen)
        director->setTopScreenNode(nullptr);
}

void CreditsScene::update(float dt)
{
    Director* director = Director::getInstance();
    if (director->buttonsDown() & KEY_B) {
        goHome();
        return;
    }
    const u32 held = director->buttonsHeld();
    float dy = 0.0f;
    circlePosition pad;
    hidCircleRead(&pad);
    if (std::abs(pad.dy) > kCirclePadDeadZone)
        dy = -std::max(-1.0f, std::min(1.0f, (float)pad.dy / kCirclePadMax));
    if (held & KEY_DDOWN)
        dy = 1.0f;
    else if (held & KEY_DUP)
        dy = -1.0f;
    if (dy != 0.0f)
        scrollBy(dy * kScrollSpeed * dt);
}

void CreditsScene::scrollBy(float dy)
{
    _content->setPositionY(std::min(_maxScroll, std::max(0.0f, _content->getPositionY() + dy)));
}

void CreditsScene::goHome()
{
    unscheduleUpdate();
    getEventDispatcher()->removeAllEventListeners();
    Director::getInstance()->replaceScene(HomeScene::createScene(_game));
}

bool CreditsScene::onTouchBegan(Touch* touch, Event*)
{
    if (Node* hit = GUIUtils::touchedNode(_touchable, touch->getLocation())) {
        GUIUtils::setPressed(hit);
        _pressed = hit;
    }
    return true;
}

void CreditsScene::onTouchMoved(Touch* touch, Event*)
{
    scrollBy(touch->getDelta().y);
}

void CreditsScene::onTouchEnded(Touch* touch, Event*)
{
    Node* hit = GUIUtils::touchedNode(_touchable, touch->getLocation());
    GUIUtils::resetInteraction(_pressed);
    if (hit && hit == _pressed && hit == _home) {
        _pressed = nullptr;
        goHome();
        return;
    }
    _pressed = nullptr;
}
