#include "game/homescene.h"

#include <3ds.h>

#include <stdexcept>

#include "game/creditsscene.h"
#include "game/gamenode.h"
#include "game/guiglobals.h"
#include "game/guiscene.h"
#include "game/guiutils.h"
#include "game/hometopscreen.h"
#include "game/mapconfigfactory.h"
#include "game/mapselectscene.h"
#include "game/prefs.h"
#include "game/utils.h"

using namespace cocos2d;

namespace {

// A button with its label above it, as Tutorial and Credits.
Sprite* createLabelledButton(const char* frame, const char* text)
{
    Sprite* button = Sprite::createWithSpriteFrameName(frame);
    GUIUtils::createButtonData(button, frame, Color3B(255, 255, 255));
    Label* label = Label::createWithTTF(text, GUIGlobals::kFont, GUIGlobals::dpToSize * 24.0f);
    const Size size = button->getContentSize();
    label->setPosition(Vec2(size.width * 0.5f, size.height + label->getContentSize().height * 0.5f));
    button->addChild(label);
    return button;
}

} // namespace

Scene* HomeScene::createScene(GameNode* game)
{
    Scene* scene = Scene::create();
    scene->addChild(create(game));
    return scene;
}

HomeScene* HomeScene::create(GameNode* game)
{
    HomeScene* scene = new HomeScene();
    if (!scene->init(game)) {
        delete scene;
        return nullptr;
    }
    scene->autorelease();
    return scene;
}

HomeScene::~HomeScene()
{
    CC_SAFE_RELEASE(_topScreen);
}

bool HomeScene::init(GameNode* game)
{
    if (!Layer::init())
        return false;
    _game = game;
    const Size visible = Director::getInstance()->getVisibleSize();
    const float dp = GUIGlobals::dpToSize;

    Sprite* background = Sprite::create("Background_HomeScreen.png");
    const Size backgroundSize = background->getContentSize();
    background->setAnchorPoint(Vec2(0.5f, 0.65f));
    background->setPosition(Vec2(visible.width * 0.5f, visible.height * 0.7f));
    background->setScale(visible.width / backgroundSize.width);
    addChild(background);

    _play = Sprite::createWithSpriteFrameName("Button_TitleScreen_Play.png");
    const Size playSize = _play->getContentSize();
    GUIUtils::createButtonData(_play, "Button_TitleScreen_Play.png", Color3B(255, 255, 255));
    _play->setPosition(Vec2(visible.width * 0.5f, visible.height * 0.22f));
    addChild(_play, 1);
    _touchable.insert(_play);

    _tutorial = createLabelledButton("Button_Tutorial.png", "Tutorial");
    _tutorial->setPosition(Vec2(visible.width * 0.225f, _play->getPositionY()));
    addChild(_tutorial, 1);
    _touchable.insert(_tutorial);

    // Level with Play's top; Share was under it, level with its bottom.
    _credits = createLabelledButton("Button_Credits.png", "Credits");
    _credits->setPosition(Vec2(visible.width * 0.775f, _play->getPositionY() + playSize.height * 0.5f -
                                                           _credits->getContentSize().height * 0.2f));
    addChild(_credits, 1);
    _touchable.insert(_credits);

    _topScreen = HomeTopScreen::create(_game);
    _topScreen->retain();

    if (!_game->finishedLoading()) {
        _play->setVisible(false);
        _tutorial->setVisible(false);
        _credits->setVisible(false);
        _loadingLabel = Label::createWithTTF("loading", GUIGlobals::kFont, dp * 24.0f);
        _loadingLabel->setPosition(Vec2(visible.width * 0.5f, visible.height * 0.35f));
        addChild(_loadingLabel);

        // The bar's end grows across its start, from the left.
        _loadingBarStart = Sprite::createWithSpriteFrameName("Loading_Bar_Start.png");
        const float barWidth = _loadingBarStart->getContentSize().width;
        _loadingBarStart->setAnchorPoint(Vec2(0.0f, 0.5f));
        const Vec2 bar((float)(int)((visible.width - barWidth) * 0.5f), (float)(int)(visible.height * 0.28f));
        _loadingBarStart->setPosition(bar);
        addChild(_loadingBarStart, 1);
        _loadingBarEnd = Sprite::createWithSpriteFrameName("Loading_Bar_End.png");
        Rect rect = _loadingBarEnd->getTextureRect();
        rect.size.width = 0.0f;
        _loadingBarEnd->setTextureRect(rect);
        _loadingBarEnd->setAnchorPoint(Vec2(0.0f, 0.5f));
        _loadingBarEnd->setPosition(bar);
        addChild(_loadingBarEnd, 1);

        _binarySpaceLogo = Sprite::createWithSpriteFrameName("Loading_Logo_Binary_Space.png");
        _binarySpaceLogo->setAnchorPoint(Vec2(0.0f, 0.0f));
        _binarySpaceLogo->setPosition(Vec2(dp * 5.0f, dp * 3.0f));
        addChild(_binarySpaceLogo);
        _screenAustraliaLogo = Sprite::createWithSpriteFrameName("Loading_Logo_Screen_Australia.png");
        _screenAustraliaLogo->setAnchorPoint(Vec2(1.0f, 0.0f));
        _screenAustraliaLogo->setPosition(Vec2(visible.width - dp * 5.0f, dp * 3.0f));
        addChild(_screenAustraliaLogo);
        scheduleUpdate();
    }

    auto touch = EventListenerTouchOneByOne::create();
    touch->onTouchBegan = [this](Touch* t, Event* e) { return onTouchBegan(t, e); };
    touch->onTouchEnded = [this](Touch* t, Event* e) { onTouchEnded(t, e); };
    getEventDispatcher()->addEventListenerWithSceneGraphPriority(touch, this);
    return true;
}

void HomeScene::onEnter()
{
    Layer::onEnter();
    Director::getInstance()->setTopScreenNode(_topScreen);
}

void HomeScene::onExit()
{
    Layer::onExit();
    Director* director = Director::getInstance();
    if (director->getTopScreenNode() == _topScreen)
        director->setTopScreenNode(nullptr);
}

void HomeScene::update(float)
{
    bool done = false;
    int percent = 0;
    _game->loadStep(done, percent);
    if (!done) {
        Rect rect = _loadingBarEnd->getTextureRect();
        rect.size.width = _loadingBarStart->getContentSize().width * (float)percent / 100.0f;
        _loadingBarEnd->setTextureRect(rect);
        return;
    }
    unscheduleUpdate();
    const u64 start = osGetTime();
    const bool restored = _game->restoreSavedSimulation();
    Utils::log("restore took %d ms", (int)(osGetTime() - start));
    if (restored) {
        getEventDispatcher()->removeAllEventListeners();
        Director::getInstance()->replaceScene(GUIScene::createScene(_game));
        return;
    }
    _loadingBarStart->setVisible(false);
    _loadingBarEnd->setVisible(false);
    _loadingLabel->setVisible(false);
    _binarySpaceLogo->setVisible(false);
    _screenAustraliaLogo->setVisible(false);
    _play->setVisible(true);
    if (Prefs::getInstance().getTutorialViewed()) {
        _tutorial->setVisible(true);
        _credits->setVisible(true);
    }
}

bool HomeScene::onTouchBegan(Touch* touch, Event*)
{
    if (Node* hit = GUIUtils::touchedNode(_touchable, touch->getLocation())) {
        GUIUtils::setPressed(hit);
        _pressed = hit;
    }
    return true;
}

void HomeScene::onTouchEnded(Touch* touch, Event*)
{
    Node* hit = GUIUtils::touchedNode(_touchable, touch->getLocation());
    if (hit && hit == _pressed) {
        if (hit == _play) {
            if (Prefs::getInstance().getTutorialViewed())
                showMapSelect();
            else
                startTutorial();
        } else if (hit == _tutorial) {
            startTutorial();
        } else if (hit == _credits) {
            getEventDispatcher()->removeAllEventListeners();
            Director::getInstance()->replaceScene(CreditsScene::createScene(_game));
        }
    }
    GUIUtils::resetInteraction(_pressed);
    _pressed = nullptr;
}

void HomeScene::startTutorial()
{
    MapConfig* map = _game->getMapConfigFactory().getMapByTypeAndId(MapConfig::MapType::Embedded, 1);
    if (map == nullptr)
        throw std::runtime_error("Unable to find tutorial map");
    MapSelectScene::startMap(map, true, _game);
    getEventDispatcher()->removeAllEventListeners();
    Director::getInstance()->replaceScene(GUIScene::createScene(_game));
}

void HomeScene::showMapSelect()
{
    getEventDispatcher()->removeAllEventListeners();
    Director::getInstance()->replaceScene(
        MapSelectTransition::create(0.5f, MapSelectScene::createScene(_game, false)));
}
