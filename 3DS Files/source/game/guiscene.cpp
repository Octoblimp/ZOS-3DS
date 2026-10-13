#include "game/guiscene.h"

#include <3ds.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "game/actionsmenu.h"
#include "game/controlsmenu.h"
#include "game/gamenode.h"
#include "game/guiglobals.h"
#include "game/guiutils.h"
#include "game/prefs.h"
#include "game/simulation.h"
#include "game/topscreenviews.h"

using namespace cocos2d;

namespace {

// The top screen, in its own pixels.
const float kTopWidth = 400.0f;
const float kTopHeight = 240.0f;
// The legend's squares beside the stats.
const float kLegendSize = 7.0f;

} // namespace

Scene* GUIScene::createScene(GameNode* game)
{
    Scene* scene = Scene::create();
    scene->addChild(create(game));
    return scene;
}

GUIScene* GUIScene::create(GameNode* game)
{
    GUIScene* scene = new GUIScene();
    if (!scene->init(game)) {
        delete scene;
        return nullptr;
    }
    scene->autorelease();
    return scene;
}

GUIScene::GUIScene() = default;

GUIScene::~GUIScene()
{
    CC_SAFE_RELEASE(_topScreen);
}

bool GUIScene::init(GameNode* game)
{
    if (!Layer::init())
        return false;
    _game = game;
    addChild(_game);
    _screen = Director::getInstance()->getVisibleSize();
    initStatsLabels();
    updateStats(0.0f);

    const bool tutorial = _game->getTutorialMode();
    float y = _screen.height * 0.5f;
    if (tutorial)
        y += GUIGlobals::dpToSize * 60.0f * 0.5f;
    _actionsMenu = ActionsMenu::create(this, tutorial);
    _actionsMenu->setPosition(Vec2(_screen.width + _actionsMenu->getPanel()->getContentSize().width * 0.5f, y));
    addChild(_actionsMenu, 1);
    _controlsMenu = ControlsMenu::create(_game, this, tutorial);
    _controlsMenu->setPosition(Vec2(_controlsMenu->getPanel()->getContentSize().width * -0.5f, _screen.height * 0.5f));
    addChild(_controlsMenu, 1);
    if (tutorial) {
        _controlsMenu->setVisible(false);
        _actionsMenu->setVisible(false);
    }

    initMisc();
    initOverlay(ActionType::Soldiers);
    initOverlay(ActionType::Mk81Bombs);
    initOverlay(ActionType::Mk82Bombs);
    initOverlay(ActionType::Helicopters);
    _tutorialStep = 0;
    if (tutorial)
        setupTutorial();

    schedule(CC_SCHEDULE_SELECTOR(GUIScene::updateStats), _statsInterval);
    scheduleUpdate();

    auto touch = EventListenerTouchOneByOne::create();
    touch->onTouchBegan = [this](Touch* t, Event* e) { return onTouchBegan(t, e); };
    touch->onTouchEnded = [this](Touch* t, Event* e) { onTouchEnded(t, e); };
    getEventDispatcher()->addEventListenerWithSceneGraphPriority(touch, this);
    auto touches = EventListenerTouchAllAtOnce::create();
    touches->onTouchesBegan = [this](const std::vector<Touch*>& t, Event* e) { onTouchesBegan(t, e); };
    touches->onTouchesMoved = [this](const std::vector<Touch*>& t, Event* e) { onTouchesMoved(t, e); };
    touches->onTouchesEnded = [this](const std::vector<Touch*>& t, Event* e) { onTouchesEnded(t, e); };
    getEventDispatcher()->addEventListenerWithSceneGraphPriority(touches, this);
    return true;
}

void GUIScene::onEnter()
{
    Layer::onEnter();
    Director::getInstance()->setTopScreenNode(_topScreen);
}

void GUIScene::onExit()
{
    Layer::onExit();
    _game->stopMap();
    Director* director = Director::getInstance();
    if (director->getTopScreenNode() == _topScreen)
        director->setTopScreenNode(nullptr);
}

// ------------------------------------------------------------------ stats

void GUIScene::initStatsLabels()
{
    // The game's labels and font, on a screen of their own: the left column
    // from the top left, the right column right-aligned from the top right
    // (or under the left one, when they would overlap).
    _topScreen = Node::create();
    _topScreen->retain();
    _topScreen->setContentSize(Size(kTopWidth, kTopHeight));
    _topScreen->addChild(LayerColor::create(Color4B(24, 24, 24, 255), kTopWidth, kTopHeight), -1);

    const float size = GUIGlobals::dpToSize * 24.0f;
    _statsLineHeight = GUIUtils::textDimensionsFromFontSize(GUIGlobals::kFont, size).height;
    auto label = [&](const char* text) {
        Label* l = Label::createWithTTF(text, GUIGlobals::kFont, size);
        l->setAnchorPoint(Vec2(0.0f, 1.0f));
        _topScreen->addChild(l, 1);
        return l;
    };
    _civsLabel = label(" ");
    _zombiesLabel = label(" ");
    _deadLabel = label(" ");
    _timeLabel = label(" ");
    _zombiesKilledLabel = label("Zombies Killed: 0");
    _civsKilledLabel = label("Collateral Damage: 0");
    _civsRescuedLabel = label("Civs Rescued: 0");

    // The port's: the controls the game did not have.
    Label* help = Label::createWithTTF(
        "L/R: zoom    Circle Pad: pan    B: cancel\nSTART: save and quit    SELECT: positional sound",
        GUIGlobals::kFont, 16.0f);
    help->setAnchorPoint(Vec2(0.5f, 0.0f));
    help->setAlignment(TextHAlignment::CENTER);
    help->setPosition(Vec2(kTopWidth * 0.5f, 6.0f));
    help->setTextColor(Color4B(150, 150, 150, 255));
    _topScreen->addChild(help, 1);

    // The port's: an online map's imagery credit above them (the game had
    // Google's logo on the map; on the bottom screen's canvas, drawn at 2/3,
    // text this small blurs), and its download above that while it goes on.
    float y = help->getPositionY() + help->getContentSize().height + 10.0f;
    const char* credit = _game->getImageryCredit();
    if (credit != nullptr) {
        _creditLabel = Label::createWithTTF(credit, GUIGlobals::kFont, 16.0f);
        _creditLabel->setAnchorPoint(Vec2(0.5f, 0.0f));
        _creditLabel->setAlignment(TextHAlignment::CENTER);
        _creditLabel->setPosition(Vec2(kTopWidth * 0.5f, y));
        _creditLabel->setTextColor(Color4B(200, 200, 200, 255));
        _topScreen->addChild(_creditLabel, 1);
        y += _creditLabel->getContentSize().height + 10.0f;
    }
    _downloadLabel = Label::createWithTTF(" ", GUIGlobals::kFont, 16.0f);
    _downloadLabel->setAnchorPoint(Vec2(0.5f, 0.0f));
    _downloadLabel->setPosition(Vec2(kTopWidth * 0.5f, y));
    _downloadLabel->setVisible(false);
    _topScreen->addChild(_downloadLabel, 1);
    // The port's views above them (and above the download's line while it
    // shows).
    _viewsBottom = y + 4.0f;
    _graph = PopulationGraph::create();
    _topScreen->addChild(_graph, 1);
    _miniMap = MiniMap::create(_game);
    _topScreen->addChild(_miniMap, 1);
    _legend = DrawNode::create();
    _topScreen->addChild(_legend, 1);
}

void GUIScene::updateStats(float)
{
    Simulation::Stats stats;
    if (!_civsLabel || !_game->getLastSimulationStats(stats))
        return;
    char text[128];
    snprintf(text, sizeof(text), "Civilians: %d", stats.numCivs);
    _civsLabel->setString(text);
    snprintf(text, sizeof(text), "Zombies: %d", stats.numZombies);
    _zombiesLabel->setString(text);
    snprintf(text, sizeof(text), "Dead: %d", stats.numDead);
    _deadLabel->setString(text);
    snprintf(text, sizeof(text), "Zombies Killed: %d", stats.numZombiesKilled);
    _zombiesKilledLabel->setString(text);
    snprintf(text, sizeof(text), "Collateral Damage: %d", stats.numCivsKilled);
    _civsKilledLabel->setString(text);
    snprintf(text, sizeof(text), "Civs rescued: %d", stats.numCivsRescued);
    _civsRescuedLabel->setString(text);
    snprintf(text, sizeof(text), "Time: %d:%02d", stats.gameTime / 60000, stats.gameTime % 60000 / 1000);
    _timeLabel->setString(text);

    const std::string download = _game->getMapDownloadStatus();
    if (download != _downloadText) {
        _downloadText = download;
        _downloadLabel->setString(download.empty() ? std::string(" ") : download);
        _downloadLabel->setVisible(!download.empty());
    }
    _graph->addStats(stats);
    _miniMap->refresh();

    const float margin = GUIGlobals::dpToSize * 4.0f;
    Label* left[] = {_civsLabel, _zombiesLabel, _deadLabel, _timeLabel};
    Label* right[] = {_zombiesKilledLabel, _civsKilledLabel, _civsRescuedLabel};
    float leftWidth = 0.0f, rightWidth = 0.0f;
    for (Label* l : left)
        leftWidth = std::max(leftWidth, l->getContentSize().width);
    // The legend's squares after the labels.
    leftWidth += kLegendSize + 6.0f;
    for (Label* l : right)
        rightWidth = std::max(rightWidth, l->getContentSize().width);
    float y = kTopHeight - GUIGlobals::dpToSize;
    for (Label* l : left) {
        l->setPosition(Vec2(margin, y));
        y -= _statsLineHeight;
    }
    // The game laid the right column level with the left; where the two
    // would meet it faded what they covered. Here it goes underneath.
    y = margin + leftWidth + 3.0f * margin + rightWidth + margin <= kTopWidth ? kTopHeight - GUIGlobals::dpToSize
                                                                              : y - _statsLineHeight * 0.5f;
    const float leftBottom = kTopHeight - GUIGlobals::dpToSize - 4.0f * _statsLineHeight;
    for (Label* l : right) {
        l->setPosition(Vec2(kTopWidth - l->getContentSize().width - margin, y));
        y -= _statsLineHeight;
    }
    layoutTopViews(leftBottom, y);
}

void GUIScene::layoutTopViews(float leftBottom, float rightBottom)
{
    // A square of each line's colour in the graph and the minimap, after
    // its label, half way down the line.
    _legend->clear();
    const std::pair<Label*, Color4F> keys[] = {{_civsLabel, Color4F(0.92f, 0.92f, 0.92f, 1.0f)},
                                               {_zombiesLabel, Color4F(0.95f, 0.25f, 0.2f, 1.0f)},
                                               {_deadLabel, Color4F(0.5f, 0.5f, 0.5f, 1.0f)}};
    for (const auto& key : keys) {
        const Label* l = key.first;
        const float x = l->getPositionX() + l->getContentSize().width + 6.0f;
        const float y = std::floor(l->getPositionY() - _statsLineHeight * 0.5f - kLegendSize * 0.5f);
        _legend->drawSolidRect(Vec2(x, y), Vec2(x + kLegendSize, y + kLegendSize), key.second);
    }

    // The minimap right, as tall as the space under the right column
    // allows (and no wider than 45% of the screen); the graph in what is
    // left under the left column (or under both, when the right one went
    // underneath). Both sit on the credit.
    const float margin = GUIGlobals::dpToSize * 4.0f;
    const float gap = 8.0f;
    float bottom = _viewsBottom;
    if (_downloadLabel->isVisible())
        bottom += _downloadLabel->getContentSize().height + 2.0f;
    _miniMap->fitInto(kTopWidth * 0.45f, rightBottom - gap - bottom);
    const Size map = _miniMap->getContentSize();
    _miniMap->setVisible(map.height >= 24.0f);
    _miniMap->setPosition(Vec2(kTopWidth - margin - map.width, bottom));
    const float graphRight = (_miniMap->isVisible() ? _miniMap->getPositionX() : kTopWidth - margin) - 4.0f * margin;
    const Size graph(graphRight - margin, std::min(leftBottom, rightBottom) - gap - bottom);
    _graph->setVisible(graph.width >= 60.0f && graph.height >= 24.0f);
    _graph->setPosition(Vec2(margin, bottom));
    if (_graph->isVisible())
        _graph->setGraphSize(graph.width, graph.height);
}

// -------------------------------------------------------------- the GUI

void GUIScene::initMisc()
{
    const float dp = GUIGlobals::dpToSize;
    const bool tutorial = _game->getTutorialMode();
    if (!tutorial) {
        _buttonBackground = Sprite::createWithSpriteFrameName("ActionOverlay_ButtonBackground.png");
        const Size size = _buttonBackground->getContentSize();
        _buttonBackground->setPosition(Vec2(_screen.width - size.width * 0.5f + dp + dp, size.height * 0.5f));
        _buttonBackground->setVisible(false);
        addChild(_buttonBackground, 1);
    }

    // Bottom right; in the tutorial above its bar.
    _cancel = Sprite::createWithSpriteFrameName("Button_Cancel.png");
    GUIUtils::createButtonData(_cancel, "Button_Cancel.png", Color3B(255, 255, 255));
    const Size cancel = _cancel->getContentSize();
    _cancel->setPosition(Vec2(_screen.width - cancel.width * 0.5f - (tutorial ? dp * 4.0f : dp * 16.0f),
                              (tutorial ? dp * 64.0f : dp * 16.0f) + cancel.height * 0.5f));
    _cancel->setVisible(false);
    addChild(_cancel, 1);
    _touchable.insert(_cancel);

    // Over the waiting helicopter: Evacuate, and how many are aboard.
    _helicopterPanel = Sprite::createWithSpriteFrameName("Panel_Helicopter_Evacuate.png");
    _helicopterPanel->setAnchorPoint(Vec2(0.0f, 1.0f));
    _helicopterPanel->setVisible(false);
    const Size panel = _helicopterPanel->getContentSize();
    addChild(_helicopterPanel);
    _evacuate = Sprite::createWithSpriteFrameName("Button_Helicopter_Evacuate.png");
    GUIUtils::createButtonData(_evacuate, "Button_Helicopter_Evacuate.png", Color3B(255, 255, 255));
    const Size button = _evacuate->getContentSize();
    _evacuate->setAnchorPoint(Vec2(0.0f, 0.0f));
    const float left = dp * 8.0f;
    const float bottom = (panel.height - dp * 10.0f - button.height) * 0.5f;
    _evacuate->setPosition(Vec2(left + (panel.width - left - button.width) * 0.5f, bottom));
    _helicopterPanel->addChild(_evacuate);
    _touchable.insert(_evacuate);
    Label* evacuate = Label::createWithTTF("Evacuate", GUIGlobals::kFont, dp * 16.0f);
    evacuate->setAnchorPoint(Vec2(1.0f, 0.0f));
    evacuate->setPosition(Vec2(button.width - dp * 10.0f, dp * 6.0f));
    _evacuate->addChild(evacuate);
    _helicopterCivs = 0;
    _helicopterCivsLabel = Label::createWithTTF("Civs: 0", GUIGlobals::kFont, dp * 20.0f);
    _helicopterCivsLabel->setAnchorPoint(Vec2(0.0f, 0.5f));
    _helicopterCivsLabel->setPosition(Vec2(left + (panel.width - left) * 0.52f, bottom + button.height * 0.75f));
    _helicopterPanel->addChild(_helicopterCivsLabel);
}

void GUIScene::initOverlay(ActionType::ActionType action)
{
    const char* image;
    const char* text;
    Color4F color;
    switch (action) {
    case ActionType::Soldiers:
        image = "ActionOverlay_Soldiers.png";
        text = "Tap to\ndeploy";
        color = Color4F(0.0f, 0.7137255f, 0.9098039f, 0.75f);
        break;
    case ActionType::Mk81Bombs:
    case ActionType::Mk82Bombs:
        image = "ActionOverlay_Weapons.png";
        text = "Tap to\ndrop";
        color = Color4F(0.9490196f, 0.50980395f, 0.26666668f, 0.75f);
        break;
    case ActionType::Helicopters:
        image = "ActionOverlay_Helicopters.png";
        text = "Tap to\ndeploy";
        color = Color4F(0.5411765f, 0.7647059f, 0.2509804f, 0.75f);
        break;
    default:
        return;
    }
    Node* overlay = Node::create();
    _overlays[action - 1] = overlay;

    // The frame round the screen, as wide as it.
    Sprite* frame = Sprite::create(image);
    frame->setPosition(Vec2(_screen.width * 0.5f, _screen.height * 0.5f));
    frame->setScale(_screen.width / frame->getContentSize().width);

    // "Tap to deploy" on a tinted box at the top right.
    Label* label = Label::createWithTTF(text, GUIGlobals::kFont, GUIGlobals::dpToSize * 24.0f);
    label->setAlignment(TextHAlignment::CENTER);
    const Size labelSize = label->getContentSize();
    DrawNode* box = GUIUtils::createDrawRectangle(labelSize.width + GUIGlobals::dpToSize * 16.0f,
                                                  labelSize.height + GUIGlobals::dpToSize * 16.0f, color);
    box->setAnchorPoint(Vec2(1.0f, 1.0f));
    box->setPosition(
        Vec2(_screen.width - GUIGlobals::dpToSize * 30.0f, _screen.height - GUIGlobals::dpToSize * 60.0f));
    const Size boxSize = box->getContentSize();
    label->setPosition(Vec2(boxSize.width * 0.5f, boxSize.height * 0.5f));
    box->addChild(label);

    overlay->addChild(box, 1);
    overlay->addChild(frame);
    overlay->setVisible(false);
    addChild(overlay);
}

void GUIScene::setupTutorial()
{
    const float dp = GUIGlobals::dpToSize;
    const float width = dp * 400.0f;
    const float height = dp * 56.0f;
    _tutorialBar = GUIUtils::createDrawRectangle(width, height, Color4F(0.0f, 0.0f, 0.0f, 0.8f));
    _tutorialBar->setAnchorPoint(Vec2(1.0f, 0.0f));
    _tutorialBar->setPosition(Vec2(_screen.width - dp * 4.0f, dp * 4.0f));
    addChild(_tutorialBar, 1);

    _tutorialNext = Sprite::createWithSpriteFrameName("Button_Tutorial_Next.png");
    const Size next = _tutorialNext->getContentSize();
    GUIUtils::createButtonData(_tutorialNext, "Button_Tutorial_Next.png", Color3B(255, 255, 255));
    _tutorialNext->setAnchorPoint(Vec2(1.0f, 0.0f));
    _tutorialNext->setPosition(Vec2(width - dp * 4.0f, dp * 4.0f));
    _tutorialBar->addChild(_tutorialNext, 1);
    _touchable.insert(_tutorialNext);

    _tutorialLabel = Label::createWithTTF(" ", GUIGlobals::kFont, (float)(int)(dp * 20.0f));
    _tutorialLabel->setAlignment(TextHAlignment::CENTER);
    _tutorialLabel->setAnchorPoint(Vec2(0.5f, 0.5f));
    _tutorialLabel->setPosition(Vec2((width - next.width - dp * 4.0f) * 0.5f, height * 0.5f));
    _tutorialBar->addChild(_tutorialLabel);
    nextTutorialStep();
}

void GUIScene::nextTutorialStep()
{
    const char* text;
    switch (_tutorialStep) {
    case 0:
        text = "Uh oh, here come the zombies!";
        break;
    case 1:
        text = "This is a simulation of a zombie\noutbreak, in a real location\nin England.";
        break;
    case 2:
        text = "You can pan and zoom around\nthe map to watch the action.";
        break;
    case 3:
        text = "There is no win or lose.\nYou make the rules!";
        break;
    case 4:
        text = "Do you want to see the\ncivilians fight back against\nthe oncoming horde?";
        break;
    case 5:
        text = "Or would you rather watch\nthe end of humanity as the\nzombies take over?";
        break;
    case 6:
        text = "You can change the outbreak\nsettings, and then see what\nhappens.";
        break;
    case 7:
        _controlsMenu->setVisible(true);
        _controlsMenu->openSettingsMenu();
        _tutorialNext->setVisible(false);
        text = "Try increasing the number of zombies,\nor increasing the percent of armed civilians,\nthen hit "
               "restart.";
        break;
    case 8:
        _tutorialNext->setVisible(true);
        _controlsMenu->toggleMenu();
        // The game's offered the upgrade for more settings.
        text = "Every setting is unlocked,\nso try them all after\nthe tutorial.";
        break;
    case 9:
        text = "Too many zombies?\nLet's try dropping some\nbombs on them!";
        break;
    case 10:
        _actionsMenu->setVisible(true);
        _actionsMenu->openWeaponsMenu();
        _tutorialNext->setVisible(false);
        text = "Choose a type of bomb from the weapons\nmenu. The Mk82 bomb is more powerful\nthan the Mk81.";
        break;
    case 11:
        text = "Kill it!\nKill it with fire!";
        break;
    case 12:
        _tutorialNext->setVisible(true);
        // The game's: limited bombs, unlimited with the upgrade.
        text = "You have unlimited bombs,\nbut be careful where\nyou drop them.";
        break;
    case 13:
        _tutorialNext->setVisible(false);
        exitAction();
        _actionsMenu->closeWeaponsMenu();
        _actionsMenu->enableSoldiersButton();
        text = "Oops, did you kill some civs?\nA soldier can parachute into the map\nfor a more surgical strike.";
        break;
    case 14:
        text = "Deploy a soldier to a strategic location.";
        break;
    case 15:
        _tutorialNext->setVisible(true);
        text = "You have unlimited soldiers.\nDeploy as many as\nyou like.";
        break;
    case 16:
        _tutorialNext->setVisible(false);
        exitAction();
        _actionsMenu->enableHelicoptersButton();
        text = "Want to save a few helpless civilians?\nUse a helicopter to send in a rescue mission.";
        break;
    case 17:
        text = "Deploy a helicopter to a safe location.";
        break;
    case 18:
        text = "Wait for some civs to get on board,\nthen evacuate before they get overrun.";
        break;
    case 19:
        _tutorialNext->setVisible(true);
        exitAction();
        text = "You have unlimited helicopters.\nSend in as many rescue\nmissions as you like.";
        break;
    case 20:
        _tutorialNext->setVisible(false);
        if (_actionsMenu->isOpen())
            _actionsMenu->toggleMenu();
        if (!_controlsMenu->isOpen())
            _controlsMenu->toggleMenu();
        _controlsMenu->enableChangeMapButton();
        text = "You can change the map to create a\nzombie outbreak in other locations\naround the world.";
        break;
    default:
        return;
    }
    _tutorialStep++;
    _tutorialLabel->setString(text);
    // Centred in the bar, left of Next while it shows.
    const float width = _tutorialBar->getContentSize().width;
    if (_tutorialNext->isVisible())
        _tutorialLabel->setPositionX((width - _tutorialNext->getContentSize().width - GUIGlobals::dpToSize * 4.0f) *
                                     0.5f);
    else
        _tutorialLabel->setPositionX(width * 0.5f);
}

void GUIScene::exitAction()
{
    for (Node* overlay : _overlays)
        overlay->setVisible(false);
    _actionsMenu->setVisible(true);
    _controlsMenu->setVisible(true);
    _cancel->setVisible(false);
    if (_buttonBackground)
        _buttonBackground->setVisible(false);
    _action = ActionType::None;
}

void GUIScene::update(float)
{
    Vec2 position;
    int civs = 0;
    if (_game->getFocusedWaitingHelicopterPosition(position, civs)) {
        _helicopterPanel->setPosition(position);
        _helicopterPanel->setVisible(true);
        if (civs != _helicopterCivs) {
            char text[32];
            snprintf(text, sizeof(text), "Civs: %d", civs);
            _helicopterCivsLabel->setString(text);
            _helicopterCivs = civs;
        }
    } else {
        _helicopterPanel->setVisible(false);
    }

    // The port's: the buttons for what shows.
    const u32 down = Director::getInstance()->buttonsDown();
    if ((down & KEY_B) && _cancel->isVisible() && GUIUtils::hasVisibleParents(_cancel))
        exitAction();
    if ((down & KEY_Y) && _helicopterPanel->isVisible()) {
        _game->evacuateFocusedWaitingHelicopter();
        if (_tutorialStep == 19)
            nextTutorialStep();
    }
    // Positional sound (SoundManager), said on the map as the game's
    // messages are.
    if (down & KEY_SELECT) {
        const bool on = !_game->getPositionalSound();
        _game->setPositionalSound(on);
        _game->displayMessage(on ? "Positional sound on" : "Positional sound off");
    }
}

// ---------------------------------------------------------------- touches

bool GUIScene::onTouchBegan(Touch* touch, Event*)
{
    Node* hit = GUIUtils::touchedNode(_touchable, touch->getLocation());
    if (!hit)
        return false;
    if (hit == _cancel || hit == _evacuate || (_tutorialNext && hit == _tutorialNext)) {
        GUIUtils::setPressed(hit);
        _pressed = hit;
    }
    return true;
}

void GUIScene::onTouchEnded(Touch* touch, Event* event)
{
    if (Node* hit = GUIUtils::touchedNode(_touchable, touch->getLocation())) {
        if (hit == _pressed) {
            if (hit == _cancel) {
                exitAction();
            } else if (hit == _evacuate) {
                _game->evacuateFocusedWaitingHelicopter();
                if (_tutorialStep == 19)
                    nextTutorialStep();
            } else if (_tutorialNext && hit == _tutorialNext) {
                nextTutorialStep();
            }
        }
        // Not a tap on the map.
        event->stopPropagation();
    }
    GUIUtils::resetInteraction(_pressed);
}

void GUIScene::onTouchesBegan(const std::vector<Touch*>& touches, Event* event)
{
    _game->onTouchesBegan(touches, event);
    _touchMoveX = 0.0f;
    _touchMoveY = 0.0f;
}

void GUIScene::onTouchesMoved(const std::vector<Touch*>& touches, Event* event)
{
    if (touches.size() != 1) {
        _game->onTouchesMoved(touches, event);
        return;
    }
    const Vec2 delta = touches[0]->getDelta();
    _touchMoveX += std::fabs(delta.x);
    _touchMoveY += std::fabs(delta.y);
    // A drag over the buttons does not pan.
    if (!GUIUtils::checkTouchAny(_touchable, touches[0]->getLocation()))
        _game->onTouchesMoved(touches, event);
}

void GUIScene::onTouchesEnded(const std::vector<Touch*>& touches, Event* event)
{
    _game->onTouchesEnded(touches, event);
    if (_action == ActionType::None || touches.size() != 1)
        return;
    // A tap: it ended near where it began, and hardly moved on the way.
    const Vec2 location = touches[0]->getLocation();
    const Vec2 start = touches[0]->getStartLocation();
    const float limit = GUIGlobals::dpToSize * 10.0f * GUIGlobals::dpToSize * 10.0f;
    if ((location - start).lengthSquared() > limit ||
        _touchMoveX * _touchMoveX + _touchMoveY * _touchMoveY > limit)
        return;
    _game->performAction(_action, location);
    switch (_tutorialStep) {
    case 12:
        if (_action == ActionType::Mk81Bombs || _action == ActionType::Mk82Bombs)
            nextTutorialStep();
        break;
    case 15:
        if (_action == ActionType::Soldiers)
            nextTutorialStep();
        break;
    case 18:
        if (_action == ActionType::Helicopters)
            nextTutorialStep();
        break;
    default:
        break;
    }
}

// -------------------------------------------------------------- listeners

void GUIScene::onActionsMenuToggled(bool)
{
    // The game faded the stats the panel covered; they are on the top
    // screen here.
}

void GUIScene::onActionSelected(ActionType::ActionType action)
{
    if (action < ActionType::Soldiers || action > ActionType::Helicopters)
        return;
    _overlays[action - 1]->setVisible(true);
    _cancel->setVisible(true);
    if (_buttonBackground)
        _buttonBackground->setVisible(true);
    _controlsMenu->setVisible(false);
    _actionsMenu->setVisible(false);
    _action = action;
    if (_tutorialStep == 0)
        return;
    // The tutorial's: the action it asked for moves it on, and there is no
    // cancelling it.
    if ((_tutorialStep == 11 && (action == ActionType::Mk81Bombs || action == ActionType::Mk82Bombs)) ||
        (_tutorialStep == 14 && action == ActionType::Soldiers) ||
        (_tutorialStep == 17 && action == ActionType::Helicopters)) {
        nextTutorialStep();
        _cancel->setVisible(false);
    }
}

void GUIScene::onSettingsSelected(bool visible)
{
    // The game faded the stats the panel covered. The port's: the map's
    // buttons are off while the settings are up.
    _game->setControlsEnabled(!visible);
}

void GUIScene::onSettingsRestart()
{
    if (_tutorialStep == 8)
        nextTutorialStep();
}
