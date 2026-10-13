#include "game/mapselectscene.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

#include "game/gamenode.h"
#include "game/guiglobals.h"
#include "game/guiscene.h"
#include "game/guiutils.h"
#include "game/homescene.h"
#include "game/hometopscreen.h"
#include "game/mapconfigfactory.h"
#include "game/placesearch.h"
#include "game/playermaps.h"
#include "game/utils.h"

using namespace cocos2d;

Scene* MapSelectScene::createScene(GameNode* game, bool tutorialMode)
{
    Scene* scene = Scene::create();
    scene->addChild(create(game, tutorialMode));
    return scene;
}

MapSelectScene* MapSelectScene::create(GameNode* game, bool tutorialMode)
{
    MapSelectScene* scene = new MapSelectScene();
    if (!scene->init(game, tutorialMode)) {
        delete scene;
        return nullptr;
    }
    scene->autorelease();
    return scene;
}

void MapSelectScene::startMap(const MapConfig* map, bool tutorialMode, GameNode* game)
{
    Utils::log("Starting %s map %d", MapConfig::mapTypeToString(map->type), map->id);
    game->startMap(map, tutorialMode);
}

MapSelectScene::~MapSelectScene()
{
    CC_SAFE_RELEASE(_topScreen);
    CC_SAFE_RELEASE(_playerTop);
}

bool MapSelectScene::init(GameNode* game, bool tutorialMode)
{
    if (!Layer::init())
        return false;
    _game = game;
    _visible = Director::getInstance()->getVisibleSize();
    const float dp = GUIGlobals::dpToSize;
    _panelHeight = GUIUtils::spriteSizeFromFrameCache("PanelTop_MapSelect_OfflineMaps.png").height;
    const Size tabSize = GUIUtils::spriteSizeFromFrameCache("Button_MapSelect_OnlineMaps.png");
    _margin = dp * 8.0f;
    _labelMargin = dp * 4.0f;

    // Background_General's right part, as much of its 70% as the screen's
    // shape takes, as wide as the screen. Its left part is zombie hands on
    // white: the piece off the screen's left.
    Sprite* background = Sprite::create("Background_General.png");
    const Size backgroundSize = background->getContentSize();
    const float shown = (float)(int)(backgroundSize.width * 0.7f);
    const float rows = (float)(int)backgroundSize.height;
    const float aspect = _visible.width / _visible.height;
    Rect rect = background->getTextureRect();
    rect.origin.x = backgroundSize.width - shown;
    rect.size.width = (float)(aspect < shown / rows ? (int)(rows * aspect) : (int)shown);
    background->setTextureRect(rect);
    background->setScale(_visible.width / rect.size.width);
    background->setAnchorPoint(Vec2(0.0f, 0.5f));
    background->setPosition(Vec2(0.0f, _visible.height * 0.5f));

    // The map: inside the margin and the panels, under the tabs, at most
    // 1.7 times as wide as it is high.
    const float bottomHeight = GUIUtils::spriteSizeFromFrameCache("PanelBottom_MapSelect_OnlineMaps.png").height;
    _mapRect = Rect(_margin + _panelHeight, _margin + bottomHeight,
                    _visible.width - (_margin + _margin) - (_panelHeight + _panelHeight),
                    _visible.height - (_margin + _margin) - (_panelHeight + _panelHeight) - tabSize.height);
    if (_mapRect.size.width / _mapRect.size.height < 1.7f) {
        const float cut = _mapRect.size.height - floorf(_mapRect.size.width / 1.7f * 0.5f) * 2.0f;
        _mapRect.size.height -= cut;
        _mapRect.origin.y += cut * 0.5f;
    }

    // The background with the grey of the panels' sides, and the hole over
    // the map.
    Node* backdrop = Node::create();
    addChild(backdrop);
    backdrop->addChild(background);
    LayerColor* sides = LayerColor::create(Color4B(198, 193, 193, 255), _mapRect.size.width + _panelHeight * 2.0f,
                                           _mapRect.size.height);
    sides->setPosition(Vec2(_mapRect.origin.x - _panelHeight, _mapRect.origin.y));
    backdrop->addChild(sides);
    LayerColor* hole = LayerColor::create(Color4B(0, 0, 0, 255), _mapRect.size.width, _mapRect.size.height);
    hole->setPosition(_mapRect.origin);
    backdrop->addChild(hole);

    // PlayerMapNode, clipped to the map (the game's showed through the
    // background's hole). Its space is the screen's.
    _playerMaps = GUIUtils::createClipper(_mapRect.size.width, _mapRect.size.height);
    _playerMaps->setPosition(_mapRect.origin);
    _playerMapNode = PlayerMapNode::create(_game, _mapRect);
    _playerMapNode->setPosition(Vec2(-_mapRect.origin.x, -_mapRect.origin.y));
    _playerMapNode->setListener(this);
    _playerMaps->addChild(_playerMapNode);
    addChild(_playerMaps);
    _playerMaps->setVisible(false);

    Sprite* offScreen = Sprite::create("Background_General.png");
    Rect offScreenRect = offScreen->getTextureRect();
    offScreenRect.size.width = backgroundSize.width - shown;
    offScreen->setTextureRect(offScreenRect);
    offScreen->setAnchorPoint(Vec2(1.0f, 0.5f));
    offScreen->setPosition(Vec2(0.0f, _visible.height * 0.5f));
    offScreen->setScale(background->getScale());
    addChild(offScreen);
    _offScreenBackgroundWidth = (int)(offScreenRect.size.width * offScreen->getScale());

    _ui = Node::create();
    addChild(_ui);
    _mapTop = _mapRect.origin.y + _mapRect.size.height;

    // The tabs along the panel's top, from the left.
    const float panelTopHeight = GUIUtils::spriteSizeFromFrameCache("PanelTop_MapSelect_OfflineMaps.png").height;
    const float tabY = _mapTop + panelTopHeight + tabSize.height * 0.5f;
    struct TabSpec {
        Sprite** sprite;
        const char* frame;
        const char* text;
    };
    const TabSpec tabs[] = {{&_offlineTab, "Button_MapSelect_OfflineMaps.png", "Offline\nMaps"},
                            {&_onlineTab, "Button_MapSelect_OnlineMaps.png", "Online\nMaps"},
                            {&_playerTab, "Button_MapSelect_PlayerMaps.png", "Player\nMaps"}};
    for (int i = 0; i < 3; ++i) {
        Sprite* tab = Sprite::createWithSpriteFrameName(tabs[i].frame);
        tab->setPosition(Vec2(_margin + tabSize.width * (0.5f + (float)i), tabY));
        Label* label = Label::createWithTTF(tabs[i].text, GUIGlobals::kFont, dp * 24.0f);
        label->setPosition(Vec2(tabSize.width * 0.5f, tabSize.height * 0.5f - dp * 3.0f));
        label->setAlignment(TextHAlignment::CENTER);
        tab->addChild(label, 0, 1);
        _ui->addChild(tab);
        _buttons.insert(tab);
        *tabs[i].sprite = tab;
    }

    _home = Sprite::createWithSpriteFrameName("Button_MapSelect_Home.png");
    const Size homeSize = _home->getContentSize();
    GUIUtils::createButtonData(_home, "Button_MapSelect_Home.png", Color3B(255, 255, 255));
    _home->setPosition(Vec2(_visible.width - homeSize.width * 0.5f, _visible.height - homeSize.height * 0.5f));
    _ui->addChild(_home);
    _buttons.insert(_home);

    // The black bar along the bottom, behind the unlock buttons in the game:
    // the tutorial's, here. The game tiled its 1-pixel slice; one sprite,
    // stretched.
    _blackGradient = Node::create();
    Sprite* gradient = Sprite::createWithSpriteFrameName("MapSelect_BlackGradient.png");
    const Size gradientSize = gradient->getContentSize();
    gradient->setAnchorPoint(Vec2(0.0f, 0.5f));
    gradient->setPosition(Vec2(-gradientSize.width * 0.5f, gradientSize.height * 0.5f));
    gradient->setScaleX((float)(int)(_visible.width / gradientSize.width));
    _blackGradient->addChild(gradient);
    _ui->addChild(_blackGradient, 10);
    _blackGradient->setVisible(false);
    _blackGradient->setContentSize(Size(_visible.width, gradientSize.height));

    // The world maps, covering the map and clipped to it.
    _onlineWorldMap = Sprite::create("WorldMap.jpg");
    Size worldMapSize = _onlineWorldMap->getContentSize();
    _offlineWorldMap = Sprite::create("WorldMap_Offline.jpg");
    const float scale =
        std::max(_mapRect.size.width / worldMapSize.width, _mapRect.size.height / worldMapSize.height);
    _onlineWorldMap->setScale(scale);
    _offlineWorldMap->setScale(scale);
    worldMapSize = worldMapSize * scale;
    ClippingNode* clipper =
        ClippingNode::create(GUIUtils::createDrawRectangle(_mapRect.size.width, _mapRect.size.height,
                                                           Color4F(0.0f, 0.0f, 0.0f, 1.0f)));
    clipper->setPosition(Vec2(_margin + _panelHeight, _mapRect.origin.y));
    const Size overhang((worldMapSize.width - _mapRect.size.width) * 0.5f,
                        (worldMapSize.height - _mapRect.size.height) * 0.5f);
    const Vec2 centre(worldMapSize.width * 0.5f - overhang.width, worldMapSize.height * 0.5f - overhang.height);
    _onlineWorldMap->setPosition(centre);
    _offlineWorldMap->setPosition(centre);
    clipper->addChild(_onlineWorldMap);
    clipper->addChild(_offlineWorldMap);
    _onlineWorldMap->setVisible(false);
    _ui->addChild(clipper);
    _worldMapRect = Rect(_margin + _panelHeight - overhang.width, _mapRect.origin.y - overhang.height,
                         worldMapSize.width, worldMapSize.height);

    _tab = Offline;
    loadMap(true);

    auto touch = EventListenerTouchOneByOne::create();
    touch->onTouchBegan = [this](Touch* t, Event* e) { return onTouchBegan(t, e); };
    touch->onTouchMoved = [this](Touch* t, Event* e) { onTouchMoved(t, e); };
    touch->onTouchEnded = [this](Touch* t, Event* e) { onTouchEnded(t, e); };
    getEventDispatcher()->addEventListenerWithSceneGraphPriority(touch, this);
    schedule(CC_SCHEDULE_SELECTOR(MapSelectScene::rotateMapMarkers), 0.05f);

    // The game's second line, "Purchase the upgrade to enable this map", is
    // gone with IAP.
    _unavailablePopup = ErrorPopup::create("The selected map is unavailable", 18, "Button_Confirm.png", "");
    _unavailablePopup->setPosition(Vec2(_visible.width * 0.5f, _visible.height * 0.5f));
    _unavailablePopup->setVisible(false);
    _ui->addChild(_unavailablePopup, 5);

    initPlayerTab();
    if (tutorialMode)
        setupTutorial();

    _topScreen = HomeTopScreen::create(_game, "Background_CreditsScreen.png");
    _topScreen->retain();
    scheduleUpdate();
    return true;
}

void MapSelectScene::onEnter()
{
    Layer::onEnter();
    Director::getInstance()->setTopScreenNode(_tab == Player ? _playerTop : _topScreen);
}

void MapSelectScene::onExit()
{
    Layer::onExit();
    Director* director = Director::getInstance();
    if (director->getTopScreenNode() == _topScreen || director->getTopScreenNode() == _playerTop)
        director->setTopScreenNode(nullptr);
}

void MapSelectScene::onEnterTransitionDidFinish()
{
    Layer::onEnterTransitionDidFinish();
    _transitionFinished = true;
}

void MapSelectScene::loadMap(bool force)
{
    if (!force && _tab == _loadedTab)
        return;
    _loadedTab = _tab;
    const std::string name = _tab == Online    ? "MapSelect_OnlineMaps.png"
                             : _tab == Offline ? "MapSelect_OfflineMaps.png"
                                               : "MapSelect_PlayerMaps.png";
    for (Node* node : _panelNodes)
        _ui->removeChild(node, true);
    _panelNodes.clear();

    // The tab's colour along the map's top, the last piece cut to fit, and
    // along its bottom.
    const float dp = GUIGlobals::dpToSize;
    const Size tile = GUIUtils::spriteSizeFromFrameCache("PanelTop_" + name);
    const float width = _mapRect.size.width + _panelHeight + _panelHeight;
    const int tiles = (int)(width / tile.width);
    const Vec2 first(dp * 8.0f + tile.width * 0.5f, tile.height * 0.5f + _mapTop);
    for (int i = 0; i < tiles; ++i) {
        Sprite* piece = Sprite::createWithSpriteFrameName("PanelTop_" + name);
        piece->setPosition(Vec2(first.x + (float)i * tile.width, first.y));
        _ui->addChild(piece);
        _panelNodes.insert(piece);
    }
    double whole;
    const float part = (float)std::modf((double)(width / tile.width), &whole);
    Sprite* last = Sprite::createWithSpriteFrameName("PanelTop_" + name);
    Rect rect = last->getTextureRect();
    rect.size.width *= part;
    last->setTextureRect(rect);
    last->setAnchorPoint(Vec2(0.0f, 0.5f));
    last->setPosition(Vec2(dp * 8.0f + (float)tiles * tile.width, first.y));
    _ui->addChild(last);
    _panelNodes.insert(last);
    Sprite* bottom = Sprite::createWithSpriteFrameName("PanelBottom_" + name);
    const Size bottomSize = bottom->getContentSize();
    bottom->setScaleX(width);
    bottom->setPosition(Vec2(_visible.width * 0.5f, _mapRect.origin.y - bottomSize.height * 0.5f));
    _ui->addChild(bottom);
    _panelNodes.insert(bottom);

    for (Node* marker : _markers)
        _ui->removeChild(marker, true);
    _markers.clear();
    _unlockedMarkers.clear();
    if (_tab == Player)
        return;
    // A marker at each of the tab's maps' pins.
    const MapConfig::MapType type = _tab == Online ? MapConfig::MapType::Google : MapConfig::MapType::Embedded;
    for (MapConfig* map : _game->getMapConfigFactory().getMaps()) {
        if (map->type != type)
            continue;
        Sprite* marker;
        if (!map->available) {
            marker = Sprite::createWithSpriteFrameName("LockedMapMarker.png");
        } else {
            marker = Sprite::createWithSpriteFrameName("UnlockedMapMarker.png");
            marker->setTag(map->id);
            _unlockedMarkers.push_back(marker);
        }
        _markers.insert(marker);
        marker->setPosition(Vec2(_worldMapRect.origin.x + _worldMapRect.size.width * map->pinX,
                                 _worldMapRect.origin.y + _worldMapRect.size.height * (1.0f - map->pinY)));
        _ui->addChild(marker);
    }
}

void MapSelectScene::rotateMapMarkers(float)
{
    // A quarter turn a second, anticlockwise, each a tenth of a turn on from
    // the last.
    const float angle = (float)fmod(_timer.getTime() * -90.0, 360.0);
    for (size_t i = 0; i < _unlockedMarkers.size(); ++i)
        _unlockedMarkers[i]->setRotation(angle + (float)i * 36.0f);
}

void MapSelectScene::mapTabSelected(Tab tab)
{
    // The imagery's tiles go with the tab (the game cleared PlayerMapNode
    // only to start a map; on the 3DS the textures are wanted back).
    if (tab != Player && _tab == Player) {
        _playerMapNode->clear();
        setPicking(false);
        PlaceSearch::getInstance()->cancel();
        _searching = false;
        setSearchText("");
        _deleting = false;
    }
    _tab = tab;
    loadMap(false);
    _onlineWorldMap->setVisible(tab == Online);
    _offlineWorldMap->setVisible(tab == Offline);
    _playerMaps->setVisible(tab == Player);
    // Outside the tutorial the bar showed with the tab's unlock buttons; the
    // port's Player tab has its own there.
    const bool tutorial = _tutorialStep != 7 && _tutorialStep != 0;
    if (!tutorial)
        _blackGradient->setVisible(tab == Player);
    updateBarButtons();
    if (isRunning())
        Director::getInstance()->setTopScreenNode(tab == Player ? _playerTop : _topScreen);
}

void MapSelectScene::setupTutorial()
{
    _blackGradient->setVisible(true);
    _tutorialNext = Sprite::createWithSpriteFrameName("Button_Tutorial_Next.png");
    const Size nextSize = _tutorialNext->getContentSize();
    GUIUtils::createButtonData(_tutorialNext, "Button_Tutorial_Next.png", Color3B(255, 255, 255));
    _tutorialNext->setAnchorPoint(Vec2(1.0f, 0.0f));
    _tutorialNext->setPosition(Vec2(_visible.width, 0.0f));
    _ui->addChild(_tutorialNext, 11);
    _buttons.insert(_tutorialNext);
    _tutorialLabel = Label::createWithTTF(" ", GUIGlobals::kFont, (float)(int)(GUIGlobals::dpToSize * 20.0f));
    _tutorialLabel->setAlignment(TextHAlignment::CENTER);
    _tutorialLabel->setAnchorPoint(Vec2(0.5f, 0.5f));
    _tutorialLabel->setPosition(Vec2((_visible.width - nextSize.width) * 0.5f, nextSize.height * 0.5f));
    _ui->addChild(_tutorialLabel, 11);
    nextTutorialStep();
}

void MapSelectScene::nextTutorialStep()
{
    const char* text = nullptr;
    switch (_tutorialStep) {
    case 0:
        _tutorialStep = 1;
        text = "Offline maps can be played anywhere,\nwithout an Internet connection.";
        break;
    case 1:
        // The game's: 1 free, all 5 with the upgrade.
        _tutorialStep = 2;
        text = "You can play all 5 offline maps.";
        break;
    case 2:
        mapTabSelected(Online);
        // The game's: Google Maps. The imagery is Esri's now.
        _tutorialStep = 3;
        text = "Online maps use satellite imagery, and so\nrequire an active Internet connection.";
        break;
    case 3:
        // The game's: 1 free, all 15 with the upgrade.
        _tutorialStep = 4;
        text = "You can play all 15 online maps.";
        break;
    case 4:
        mapTabSelected(Player);
        // The game's: over 2,500 player maps from www.class3outbreak.com,
        // then "Player maps can be unlocked by region.", dropped.
        _tutorialStep = 6;
        text = "Player maps are areas you choose\nfrom OpenStreetMap, anywhere\nin the world.";
        break;
    case 6:
        _tutorialStep = 7;
        GUIUtils::resetInteraction(_pressed);
        _pressed = nullptr;
        _buttons.erase(_tutorialNext);
        _ui->removeChild(_tutorialNext, true);
        _tutorialNext = nullptr;
        _ui->removeChild(_tutorialLabel, true);
        _tutorialLabel = nullptr;
        mapTabSelected(Offline);
        return;
    default:
        return;
    }
    _tutorialLabel->setString(text);
}

void MapSelectScene::startMap(const MapConfig* map)
{
    // The game's static startMap: the imagery's renderer is the map's now.
    _playerMapNode->clear();
    _playerMapNode->setVisible(false);
    startMap(map, false, _game);
    getEventDispatcher()->removeAllEventListeners();
    Director::getInstance()->replaceScene(GUIScene::createScene(_game));
}

bool MapSelectScene::onTouchBegan(Touch* touch, Event*)
{
    // The port's: the popups take their own touches, and nothing else does
    // while one shows.
    if (popupShowing())
        return false;
    const Vec2 location = touch->getLocation();
    if (Node* hit = GUIUtils::touchedNode(_buttons, location)) {
        // The tabs have no pressed frames.
        if (GUIUtils::getButtonData(hit)) {
            GUIUtils::setPressed(hit);
            _pressed = hit;
        } else {
            _pressedTab = hit;
        }
        return true;
    }
    if (Node* hit = GUIUtils::touchedNode(_markers, location)) {
        _pressedMarker = hit;
        return true;
    }
    // touchesOnPlayerMapNode: on the map, off the buttons.
    if (_tab == Player && _mapRect.containsPoint(location)) {
        _mapTouch = true;
        _playerMapNode->touchBegan(touch);
        return true;
    }
    return false;
}

void MapSelectScene::onTouchMoved(Touch* touch, Event*)
{
    if (_mapTouch)
        _playerMapNode->touchMoved(touch);
}

void MapSelectScene::onTouchEnded(Touch* touch, Event*)
{
    if (_mapTouch) {
        _mapTouch = false;
        _playerMapNode->touchEnded(touch);
        return;
    }
    const Vec2 location = touch->getLocation();
    if (Node* hit = GUIUtils::touchedNode(_buttons, location)) {
        if (hit == _pressedTab || hit == _pressed) {
            if (hit == _onlineTab) {
                mapTabSelected(Online);
            } else if (hit == _offlineTab) {
                mapTabSelected(Offline);
            } else if (hit == _playerTab && _transitionFinished) {
                mapTabSelected(Player);
            } else if (hit == _home) {
                getEventDispatcher()->removeAllEventListeners();
                Director::getInstance()->replaceScene(HomeScene::createScene(_game));
            } else if (_tutorialNext && hit == _tutorialNext) {
                nextTutorialStep();
            } else if (hit == _newMapButton) {
                setPicking(true);
            } else if (hit == _cancelButton) {
                setPicking(false);
            } else if (hit == _sizeButton) {
                _pickSize = _pickSize == PlayerMaps::kLargeSize ? PlayerMaps::kSmallSize : PlayerMaps::kLargeSize;
                setPicking(true);
            } else if (hit == _downloadButton) {
                startDownload();
            } else if (hit == _searchButton) {
                openSearch();
            } else if (hit == _deleteButton) {
                _deleting = !_deleting;
                updateBarButtons();
            }
        }
        _pressedTab = nullptr;
        GUIUtils::resetInteraction(_pressed);
        _pressed = nullptr;
        return;
    }
    _pressedTab = nullptr;
    if (Node* hit = GUIUtils::touchedNode(_markers, location)) {
        if (hit == _pressedMarker) {
            if (std::find(_unlockedMarkers.begin(), _unlockedMarkers.end(), hit) != _unlockedMarkers.end()) {
                MapConfig::MapType type;
                if (_tab == Offline)
                    type = MapConfig::MapType::Embedded;
                else if (_tab == Online)
                    type = MapConfig::MapType::Google;
                else
                    throw std::runtime_error("Unexpected current map type");
                MapConfig* map = _game->getMapConfigFactory().getMapByTypeAndId(type, hit->getTag());
                if (map == nullptr)
                    throw std::runtime_error("Unable to find map with specified type and ID");
                _pressedMarker = nullptr;
                startMap(map);
                return;
            }
            _unavailablePopup->setVisible(true);
        }
    }
    GUIUtils::resetInteraction(_pressed);
    _pressedMarker = nullptr;
}

// ------------------------------------------------------------ Player tab

namespace {

const float kTopWidth = 400.0f;
const float kTopHeight = 240.0f;
const char* const kDownloading = "   Downloading map   \n";

// A world pixel's latitude (Web Mercator, zoom 20).
double worldYToLat(double y)
{
    const double n = M_PI * (1.0 - 2.0 * y / 268435456.0);
    return std::atan(std::sinh(n)) * 180.0 / M_PI;
}

} // namespace

void MapSelectScene::initPlayerTab()
{
    _pickSize = PlayerMaps::kLargeSize;
    // On the black bar, where the game had the tab's unlock buttons: theirs
    // are the frames, in their places.
    _newMapButton = createBarButton("Button_MapSelect_PlayerMapsAmericas.png", "New\nmap", 0.0f);
    _sizeButton = createBarButton("Button_MapSelect_PlayerMapsEurAfr.png", "Large\narea", 0.0f);
    _cancelButton = createBarButton("Button_MapSelect_PlayerMapsAsiaAus.png", "Cancel", 1.0f / 3.0f);
    _downloadButton = createBarButton("Button_MapSelect_PlayerMapsAmericas.png", "Download", 1.0f);
    _searchButton = createBarButton("Button_MapSelect_PlayerMapsEurAfr.png", "Search", 2.0f / 3.0f);
    _deleteButton = createBarButton("Button_MapSelect_PlayerMapsAsiaAus.png", "Delete", 1.0f / 3.0f);

    // The game's two popups for a player map's download.
    _errorPopup = ErrorPopup::create("An error occurred while downloading this map", 18, "Button_Confirm.png", "");
    _errorPopup->setPosition(Vec2(_visible.width * 0.5f, _visible.height * 0.5f));
    _errorPopup->setVisible(false);
    _ui->addChild(_errorPopup, 5);
    _downloadPopup = ErrorPopup::create(kDownloading, 32, "Button_Cancel.png", "Cancel");
    _downloadPopup->setPosition(Vec2(_visible.width * 0.5f, _visible.height * 0.5f));
    _downloadPopup->setVisible(false);
    _downloadPopup->setListener(this);
    _ui->addChild(_downloadPopup, 5);
    _deletePopup = ErrorPopup::create("Delete this map?\nIt goes from the SD card for good.", 18,
                                      "Button_Confirm.png", "", "Button_Cancel.png");
    _deletePopup->setPosition(Vec2(_visible.width * 0.5f, _visible.height * 0.5f));
    _deletePopup->setVisible(false);
    _deletePopup->setListener(this);
    _ui->addChild(_deletePopup, 5);

    // The top screen, as GUIScene's: what to do, the help along the bottom
    // and the credits above it (Esri's for the imagery, OpenStreetMap's for
    // what the maps are made of).
    _playerTop = Node::create();
    _playerTop->retain();
    _playerTop->setContentSize(Size(kTopWidth, kTopHeight));
    _playerTop->addChild(LayerColor::create(Color4B(24, 24, 24, 255), kTopWidth, kTopHeight), -1);
    Label* title = Label::createWithTTF("Player Maps", GUIGlobals::kFont, 24.0f);
    title->setAnchorPoint(Vec2(0.5f, 1.0f));
    title->setPosition(Vec2(kTopWidth * 0.5f, kTopHeight - 6.0f));
    _playerTop->addChild(title, 1);
    Label* help = Label::createWithTTF("Stylus: drag to move, tap to zoom in\nL/R: zoom    Circle Pad: move",
                                       GUIGlobals::kFont, 16.0f);
    help->setAnchorPoint(Vec2(0.5f, 0.0f));
    help->setAlignment(TextHAlignment::CENTER);
    help->setPosition(Vec2(kTopWidth * 0.5f, 6.0f));
    help->setTextColor(Color4B(150, 150, 150, 255));
    _playerTop->addChild(help, 1);
    Label* credit = Label::createWithTTF("Source: Esri, Vantor, Earthstar Geographics,\nand the GIS User Community\n"
                                         "Map data \xc2\xa9 OpenStreetMap contributors",
                                         GUIGlobals::kFont, 16.0f);
    credit->setAnchorPoint(Vec2(0.5f, 0.0f));
    credit->setAlignment(TextHAlignment::CENTER);
    credit->setPosition(Vec2(kTopWidth * 0.5f, help->getPositionY() + help->getContentSize().height + 8.0f));
    credit->setTextColor(Color4B(200, 200, 200, 255));
    _playerTop->addChild(credit, 1);
    const float statusBottom = credit->getPositionY() + credit->getContentSize().height;
    const float statusTop = title->getPositionY() - title->getContentSize().height;
    _playerStatus = Label::createWithTTF(" ", GUIGlobals::kFont, 16.0f);
    _playerStatus->setAlignment(TextHAlignment::CENTER);
    _playerStatus->setPosition(Vec2(kTopWidth * 0.5f, (statusBottom + statusTop) * 0.5f));
    _playerStatus->setTextColor(Color4B(235, 235, 235, 255));
    _playerTop->addChild(_playerStatus, 1);
    _statusTop = statusTop;
    _statusBottom = statusBottom;
    _searchLabel = Label::createWithTTF(" ", GUIGlobals::kFont, 16.0f);
    _searchLabel->setAnchorPoint(Vec2(0.5f, 1.0f));
    _searchLabel->setPosition(Vec2(kTopWidth * 0.5f, statusTop - 2.0f));
    _searchLabel->setTextColor(Color4B(255, 214, 102, 255));
    _playerTop->addChild(_searchLabel, 1);
    setSearchText("");
    setPicking(false);
}

Sprite* MapSelectScene::createBarButton(const char* frame, const char* text, float place)
{
    // The game's: across the bar from the left, a 18 dp label (its prices
    // above it are gone).
    Sprite* button = Sprite::createWithSpriteFrameName(frame);
    GUIUtils::createButtonData(button, frame, Color3B(255, 255, 255));
    const Size size = button->getContentSize();
    button->setPosition(Vec2(size.width * 0.5f + place * (_visible.width - size.width), size.height * 0.5f));
    Label* label = Label::createWithTTF(text, GUIGlobals::kFont, GUIGlobals::dpToSize * 18.0f);
    label->setAlignment(TextHAlignment::CENTER);
    label->setPosition(Vec2(size.width * 0.5f, size.height * 0.5f));
    button->addChild(label, 0, 1);
    button->setVisible(false);
    _ui->addChild(button, 10);
    _buttons.insert(button);
    return button;
}

void MapSelectScene::setPicking(bool picking)
{
    _error.clear();
    if (picking)
        _deleting = false;
    _playerMapNode->setPicking(picking, _pickSize);
    updateBarButtons();
}

void MapSelectScene::updateBarButtons()
{
    const bool tutorial = _tutorialStep != 7 && _tutorialStep != 0;
    const bool shown = _tab == Player && !tutorial;
    const bool picking = _playerMapNode->isPicking();
    if (PlayerMaps::getInstance()->getMaps().empty())
        _deleting = false;
    _newMapButton->setVisible(shown && !picking && !_deleting);
    _deleteButton->setVisible(shown && !picking && !PlayerMaps::getInstance()->getMaps().empty());
    _searchButton->setVisible(shown);
    _sizeButton->setVisible(shown && picking);
    _cancelButton->setVisible(shown && picking);
    _downloadButton->setVisible(shown && picking);
    if (Label* label = dynamic_cast<Label*>(_sizeButton->getChildByTag(1)))
        label->setString(_pickSize == PlayerMaps::kLargeSize ? "Large\narea" : "Small\narea");
    if (Label* label = dynamic_cast<Label*>(_deleteButton->getChildByTag(1)))
        label->setString(_deleting ? "Done" : "Delete");
}

void MapSelectScene::startDownload()
{
    if (_downloading)
        return;
    _error.clear();
    // The square must show, to see what the map will be: zoom in to it.
    if (!_playerMapNode->pickVisible()) {
        _playerMapNode->setPicking(true, _pickSize);
        return;
    }
    int x, y;
    _playerMapNode->getPickCentre(x, y);
    PlayerMaps::getInstance()->build(x, y, _pickSize);
    _downloading = true;
    _step = "Starting";
    _downloadPopup->setText(kDownloading + _step);
    _downloadPopup->setVisible(true);
}

namespace {

// As much of text as label shows in width on one line: whole parts up to a
// comma (a place's name: "Paris, Ile-de-France, ..., France"), else cut, with
// "...".
std::string fitLine(Label* label, const std::string& text, float width)
{
    auto fits = [&](const std::string& s) {
        label->setString(s);
        return label->getContentSize().width <= width;
    };
    if (fits(text))
        return text;
    std::string line;
    for (size_t comma = text.find(", "); comma != std::string::npos; comma = text.find(", ", comma + 2)) {
        if (!fits(text.substr(0, comma)))
            break;
        line = text.substr(0, comma);
    }
    if (!line.empty())
        return line;
    line = text;
    while (!line.empty() && !fits(line + "...")) {
        // A whole UTF-8 character: its continuation bytes, then its first.
        while (!line.empty() && ((u8)line.back() & 0xc0) == 0x80)
            line.pop_back();
        if (!line.empty())
            line.pop_back();
    }
    return line + "...";
}

} // namespace

void MapSelectScene::openSearch()
{
    if (_downloading)
        return;
    // The 3DS's keyboard, over both screens until it closes (the music
    // pauses meanwhile: main.cpp's APT hook).
    PlaceSearch* search = PlaceSearch::getInstance();
    char text[128] = {};
    SwkbdState swkbd;
    swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, sizeof(text) - 1);
    swkbdSetHintText(&swkbd, "A town, a street, a landmark");
    swkbdSetInitialText(&swkbd, search->getQuery().c_str());
    swkbdSetButton(&swkbd, SWKBD_BUTTON_LEFT, "Cancel", false);
    swkbdSetButton(&swkbd, SWKBD_BUTTON_RIGHT, "Search", true);
    swkbdSetValidation(&swkbd, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
    if (swkbdInputText(&swkbd, text, sizeof(text)) != SWKBD_BUTTON_RIGHT)
        return;
    search->start(text);
    _searching = true;
    setSearchText(std::string("Searching for \"") + text + "\"...");
}

void MapSelectScene::setSearchText(const std::string& text)
{
    std::string line = text;
    std::replace(line.begin(), line.end(), '\n', ' ');
    _searchLabel->setVisible(!line.empty());
    if (!line.empty())
        _searchLabel->setString(fitLine(_searchLabel, line, kTopWidth - 16.0f));
    const float top = line.empty() ? _statusTop : _searchLabel->getPositionY() - _searchLabel->getContentSize().height;
    _playerStatus->setPosition(Vec2(kTopWidth * 0.5f, (_statusBottom + top) * 0.5f));
}

bool MapSelectScene::popupShowing() const
{
    return _unavailablePopup->isVisible() || _downloadPopup->isVisible() || _errorPopup->isVisible() ||
           _deletePopup->isVisible();
}

void MapSelectScene::onErrorPopupClosed(ErrorPopup* popup)
{
    // The download's Cancel.
    if (popup == _downloadPopup && _downloading) {
        PlayerMaps::getInstance()->cancel();
        _downloading = false;
    }
    // The question before deleting a map.
    if (popup == _deletePopup && popup->wasConfirmed()) {
        PlayerMaps::getInstance()->deleteMap(_deleteId);
        updateBarButtons();
    }
}

void MapSelectScene::onPlayerMapSelected(int id)
{
    PlayerMaps* maps = PlayerMaps::getInstance();
    const PlayerMaps::Map* map = maps->getMap(id);
    if (map == nullptr)
        return;
    if (_deleting) {
        _deleteId = id;
        _deletePopup->setVisible(true);
        return;
    }
    MapConfig config;
    maps->fillConfig(*map, config);
    startMap(&config);
}

std::string MapSelectScene::playerStatus() const
{
    char text[320];
    if (_downloading) {
        snprintf(text, sizeof(text),
                 "Making the map: %s\n\nOpenStreetMap's buildings, walls, trees\nand water, then the imagery's "
                 "trees.\nUsually under a minute.",
                 _step.c_str());
        return text;
    }
    if (!_error.empty())
        return "The map could not be made:\n" + _error;
    if (_deleting)
        return "Tap the marker of a map to delete it,\nthen Confirm (A) or Cancel (B).\n\nDone (B) when you have finished.";
    if (_playerMapNode->isPicking()) {
        if (!_playerMapNode->pickVisible())
            return "Zoom in (R, or tap the map)\nuntil the square shows: it is\nthe area your map will cover.";
        int x, y;
        _playerMapNode->getPickCentre(x, y);
        const double lat = worldYToLat(y);
        const double lon = (double)x / 268435456.0 * 360.0 - 180.0;
        // 40,075,016.686 m around the equator, 2^28 pixels.
        const int metres = (int)(_pickSize * 0.14929107 * std::cos(lat * M_PI / 180.0) + 0.5);
        snprintf(text, sizeof(text),
                 "Move the world under the square,\nthen Download (A), or Cancel (B).\n\n"
                 "%d x %d m around %.3f\xc2\xb0 %c, %.3f\xc2\xb0 %c",
                 metres, metres, std::fabs(lat), lat < 0.0 ? 'S' : 'N', std::fabs(lon), lon < 0.0 ? 'W' : 'E');
        return text;
    }
    const int count = (int)PlayerMaps::getInstance()->getMaps().size();
    if (count == 0)
        return "Make a map of anywhere in the world:\npress New map and pick its area,\nor Search (X) for a place first.";
    snprintf(text, sizeof(text), "Your maps: %d\nTap a red marker to play one,\nor press New map to make another.",
             count);
    return text;
}

void MapSelectScene::update(float)
{
    if (_tab != Player)
        return;
    PlayerMaps* maps = PlayerMaps::getInstance();
    if (_downloading) {
        std::string text;
        int id = 0;
        switch (maps->poll(text, id)) {
        case PlayerMaps::State::Working:
            if (text != _step) {
                _step = text;
                _downloadPopup->setText(kDownloading + _step);
            }
            break;
        case PlayerMaps::State::Done: {
            _downloading = false;
            _downloadPopup->setVisible(false);
            maps->acknowledge();
            onPlayerMapSelected(id);
            return;
        }
        case PlayerMaps::State::Failed:
            _downloading = false;
            _downloadPopup->setVisible(false);
            maps->acknowledge();
            _error = text;
            _errorPopup->setVisible(true);
            break;
        case PlayerMaps::State::Idle:
            _downloading = false;
            _downloadPopup->setVisible(false);
            break;
        }
    } else {
        maps->update();
    }

    // The search's answer: the place in view.
    if (_searching) {
        PlaceSearch::Place place;
        std::string error;
        switch (PlaceSearch::getInstance()->poll(place, error)) {
        case PlaceSearch::State::Done: {
            _searching = false;
            setSearchText(place.name);
            // Degrees to zoom-20 world pixels (Web Mercator).
            const double world = 268435456.0;
            auto toX = [&](double lon) { return (lon + 180.0) / 360.0 * world; };
            auto toY = [&](double lat) {
                lat = std::max(-85.05, std::min(85.05, lat)) * M_PI / 180.0;
                return (1.0 - std::log(std::tan(lat) + 1.0 / std::cos(lat)) / M_PI) * 0.5 * world;
            };
            _playerMapNode->showPlace(toX(place.lon), toY(place.lat), toX(place.east) - toX(place.west),
                                      toY(place.south) - toY(place.north));
            break;
        }
        case PlaceSearch::State::Failed:
            _searching = false;
            setSearchText(error);
            break;
        default:
            break;
        }
    }

    // The port's keys: X searches; while picking, A downloads and B cancels.
    if (!popupShowing() && !_downloading && _searchButton->isVisible() &&
        (Director::getInstance()->buttonsDown() & KEY_X)) {
        openSearch();
        return;
    }
    if (_deletePopup->isVisible()) {
        const u32 down = Director::getInstance()->buttonsDown();
        if (down & KEY_A)
            _deletePopup->close(true);
        else if (down & KEY_B)
            _deletePopup->close(false);
    } else if (_deleting && !popupShowing() && (Director::getInstance()->buttonsDown() & KEY_B)) {
        _deleting = false;
        updateBarButtons();
    }
    if (_playerMapNode->isPicking() && !popupShowing()) {
        const u32 down = Director::getInstance()->buttonsDown();
        if (down & KEY_A)
            startDownload();
        else if (down & KEY_B)
            setPicking(false);
    }

    const std::string status = playerStatus();
    if (status != _playerStatusText) {
        _playerStatusText = status;
        _playerStatus->setString(status);
    }
}

// ---------------------------------------------------------------------------

MapSelectTransition* MapSelectTransition::create(float t, Scene* scene)
{
    MapSelectTransition* transition = new MapSelectTransition();
    transition->initWithDuration(t, scene);
    transition->autorelease();
    return transition;
}

void MapSelectTransition::initScenes()
{
    for (Node* child : _inScene->getChildren()) {
        if (MapSelectScene* scene = dynamic_cast<MapSelectScene*>(child)) {
            // In from the right, with the background left of it.
            _inScene->setPosition(
                Director::getInstance()->getVisibleSize().width + (float)scene->getOffScreenBackgroundWidthPixels(),
                0.0f);
            return;
        }
    }
    throw std::runtime_error("Unable to find map select scene");
}
