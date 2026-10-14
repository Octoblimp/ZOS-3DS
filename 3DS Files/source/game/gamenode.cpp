#include "game/gamenode.h"

#include <3ds.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>

#include "game/audioenginewrapper.h"
#include "game/collisionlayer.h"
#include "game/collisionlayerrenderer.h"
#include "game/exteriorrenderer.h"
#include "game/gestureutils.h"
#include "game/guiglobals.h"
#include "game/maprenderer.h"
#include "game/playermaps.h"
#include "game/prefs.h"
#include "game/satellitetileloader.h"
#include "game/simulationrenderer.h"
#include "game/utils.h"
#include "net/http.h"
#include "paths.h"

using namespace cocos2d;

namespace {

// state.dat's version (7 lacked the outbreak settings flag) and its last
// int.
const int kSaveVersion = 8;
const int kSaveMarker = 0x12345678;

// The GUI's textures, loaded by loadStep (decomp: 0x0056cce4).
const char* const kTextures[] = {
    "ActionOverlay_Soldiers.png", "ActionOverlay_Weapons.png", "ActionOverlay_Helicopters.png",
    "GUI.plist",                  "MapSelect.plist",           "Popups.plist",
};

// The port's controls: the view moves this many screen pixels a second at
// the circle pad's full tilt or on the D-pad, and L/R zoom by this power of
// 2 a second.
const float kPanSpeed = 420.0f;
const float kZoomRate = 1.5f;
const int kCirclePadMax = 156;
const int kCirclePadDeadZone = 16;

// MapConfig's type to MapRenderer's (decomp: 0x00513700).
MapRenderer::MapType rendererMapType(MapConfig::MapType type)
{
    switch (type) {
    case MapConfig::MapType::Google:
        return MapRenderer::MapType::Online;
    case MapConfig::MapType::Embedded:
        return MapRenderer::MapType::Embedded;
    case MapConfig::MapType::Player:
        return MapRenderer::MapType::Online;
    }
    throw std::runtime_error("Invalid map type");
}

// An online map's imagery pack: a player map's beside its walls, a featured
// map's in the cache.
std::string imageryPackPath(const MapConfig& config)
{
    if (config.type == MapConfig::MapType::Player)
        return PlayerMaps::packPath(config.id);
    char path[64];
    snprintf(path, sizeof(path), ZOS_DATA_ROOT "/cache/maps/featured%d.tiles", config.id);
    return path;
}

} // namespace

GameNode* GameNode::create(float density, const Size& screen, const std::string& persistentDir)
{
    GameNode* node = new GameNode();
    node->init(density, screen, persistentDir);
    node->autorelease();
    return node;
}

GameNode::GameNode() = default;

GameNode::~GameNode()
{
    delete _mapRenderer;
    _mapRenderer = nullptr;
    delete _collisionLayerRenderer;
    _collisionLayerRenderer = nullptr;
    delete _simulationRenderer;
    _simulationRenderer = nullptr;
    delete _exteriorRenderer;
    _exteriorRenderer = nullptr;
    delete _helicopterOverlay;
    _helicopterOverlay = nullptr;
    // (The game left the simulation and the collision layer to stopMap.)
    delete _simulation;
    _simulation = nullptr;
    delete _collisionLayer;
    _collisionLayer = nullptr;
}

bool GameNode::init(float density, const Size& screen, const std::string& persistentDir)
{
    Node::init();
    Utils::log("GameNode screen size %f %f, scale %.2f", screen.width, screen.height, density);
    _density = density;
    _screen = screen;
    _persistentDir = persistentDir;
    while (!_persistentDir.empty() && _persistentDir.back() == '/')
        _persistentDir.pop_back();
    _viewX = 0.0;
    _viewY = 0.0;
    _mapScale = 1.0f;
    _mapConfig = nullptr;
    _tutorialMode = false;
    _collisionLayer = nullptr;
    _personTypeFactory.setupPersonTypes(Simulation::getStepInterval());
    _mapConfigFactory.updateMapAvailability(true);

    _mapRenderer = new MapRenderer(_density);
    _mapRenderer->onSurfaceCreated();
    _mapRenderer->onSurfaceChanged((int)screen.width, (int)screen.height);
    _collisionLayerRenderer = new CollisionLayerRenderer(_density);
    _collisionLayerRenderer->onSurfaceCreated();
    _collisionLayerRenderer->onSurfaceChanged((int)screen.width, (int)screen.height);
    _exteriorRenderer = new ExteriorRenderer(_density, screen);
    _helicopterOverlay = new CollisionOverlay("data/helicopter.co", 0x28, 10);
    _simulation = nullptr;
    _simulationRenderer = nullptr;

    _messageLabel = Label::createWithTTF(" ", GUIGlobals::kFont, density * 24.0f);
    _messageLabel->setPosition(Vec2(screen.width * 0.5f, screen.height * 0.25f));
    _messageLabel->setVisible(false);
    addChild(_messageLabel);

    _messageTime = -1;
    _loadState = 0;
    _loadTexture = 0;
    _outbreakSettingsUnlocked = false;
    _soundManager.setPositional(Prefs::getInstance().getPositionalSound());
    scheduleUpdate();
    return true;
}

void GameNode::setPositionalSound(bool on)
{
    _soundManager.setPositional(on);
    Prefs::getInstance().setPositionalSound(on);
}

void GameNode::draw(Renderer* renderer, const Mat4& transform, u32 flags)
{
    Node::draw(renderer, transform, flags);
    if (_mapConfig == nullptr || _simulation == nullptr || _simulationRenderer == nullptr)
        return;
    const float pixels = _mapScale * _density;
    _simulation->update(Rect((float)_viewX, (float)_viewY, _screen.width / pixels, _screen.height / pixels),
                        _mapScale);
    _simulation->getStats(_stats);
    gfx::blendPremultiplied();
    _mapRenderer->render((double)_mapConfig->worldX + _viewX, (double)_mapConfig->worldY + _viewY, _mapScale,
                         transform);
    _collisionLayerRenderer->render((float)_viewX, (float)_viewY, _mapScale, transform);
    _simulationRenderer->prepareRender((float)_viewX, (float)_viewY, _mapScale);
    _simulationRenderer->doRender(transform);
    _exteriorRenderer->render((float)_viewX, (float)_viewY, _mapScale, transform);
    if (_messageLabel->isVisible() && _messageTime + 5000 < _stats.gameTime)
        _messageLabel->setVisible(false);
}

void GameNode::onEnter()
{
    Node::onEnter();
    // The port's: the scene GameNode leaves cleans it up as its child, which
    // unschedules its update (the buttons) for good; the next scene's must
    // have it back.
    scheduleUpdate();
}

void GameNode::applicationDidEnterBackground()
{
    saveSimulation();
    // What the online map's download has gathered, onto the card too.
    if (isCurrentGoogleMap())
        SatelliteTileLoader::getInstance()->flushMap();
}

void GameNode::update(float dt)
{
    if (_mapConfig == nullptr || !_controlsEnabled)
        return;
    const u32 held = Director::getInstance()->buttonsHeld();
    // L and R: zoom about the screen's middle.
    float scale = _mapScale;
    if (held & KEY_R)
        scale *= std::pow(2.0f, dt * kZoomRate);
    if (held & KEY_L)
        scale /= std::pow(2.0f, dt * kZoomRate);
    bool moved = false;
    if (scale != _mapScale) {
        scale = std::max(std::min(scale, 1.0f), minimumMapScale());
        zoomTo(scale, _screen.width * 0.5f, _screen.height * 0.5f);
        moved = true;
    }
    // The circle pad and the D-pad: pan, in screen pixels (y down).
    circlePosition pad;
    hidCircleRead(&pad);
    float dx = 0.0f, dy = 0.0f;
    if (std::abs(pad.dx) > kCirclePadDeadZone || std::abs(pad.dy) > kCirclePadDeadZone) {
        dx = std::max(-1.0f, std::min(1.0f, (float)pad.dx / kCirclePadMax));
        dy = -std::max(-1.0f, std::min(1.0f, (float)pad.dy / kCirclePadMax));
    }
    if (held & KEY_DLEFT)
        dx = -1.0f;
    if (held & KEY_DRIGHT)
        dx = 1.0f;
    if (held & KEY_DUP)
        dy = -1.0f;
    if (held & KEY_DDOWN)
        dy = 1.0f;
    if (dx != 0.0f || dy != 0.0f) {
        const float pixels = _mapScale * _density;
        _viewX += (double)(dx * kPanSpeed * dt / pixels);
        _viewY += (double)(dy * kPanSpeed * dt / pixels);
        moved = true;
    }
    if (moved)
        clampVisibleArea(false);
}

void GameNode::onTouchesMoved(const std::vector<Touch*>& touches, Event*)
{
    if (_mapConfig == nullptr)
        return;
    if (touches.size() == 2)
        GestureUtils::handleZoom(touches, _screen, _density, minimumMapScale(), _mapScale, _viewX, _viewY);
    else if (touches.size() == 1)
        GestureUtils::handlePan(touches, _density, _mapScale, _viewX, _viewY);
    else
        return;
    clampVisibleArea(false);
}

float GameNode::minimumMapScale() const
{
    return (_screen.height / _density - 160.0f) / (float)_mapConfig->height;
}

void GameNode::zoomTo(float scale, float x, float y)
{
    const float oldPixels = _mapScale * _density;
    const float newPixels = scale * _density;
    _viewX += (double)(x / oldPixels) - (double)(x / newPixels);
    _viewY += (double)(y / oldPixels) - (double)(y / newPixels);
    _mapScale = scale;
}

void GameNode::clampVisibleArea(bool initial)
{
    const float pixels = _mapScale * _density;
    const float margin = initial ? -10.0f : 80.0f / _mapScale;
    const float viewWidth = _screen.width / pixels;
    const float viewHeight = _screen.height / pixels;
    const float width = (float)_mapConfig->width;
    const float height = (float)_mapConfig->height;
    if (_viewY < (double)-margin)
        _viewY = (double)-margin;
    if ((double)viewHeight + _viewY > (double)(margin + height))
        _viewY = (double)(margin + (height - viewHeight));
    if (viewWidth <= margin + margin + width) {
        if (_viewX < (double)-margin)
            _viewX = (double)-margin;
        if ((double)viewWidth + _viewX <= (double)(margin + width))
            return;
        _viewX = (double)(margin + (width - viewWidth));
    } else {
        _viewX = (double)((float)(_mapConfig->width / 2) + viewWidth * -0.5f);
    }
}

void GameNode::startMap(const MapConfig* config, bool tutorialMode)
{
    if (config->type == MapConfig::MapType::Player) {
        _playerMapConfig = *config;
        config = &_playerMapConfig;
    }
    _mapConfig = config;
    _tutorialMode = tutorialMode;
    // (A menu left open when the last map was left turned them off.)
    _controlsEnabled = true;
    // Half size, centred on the map's start.
    _mapScale = 0.5f;
    _viewX = (double)((float)config->startX + (_screen.width / _density) * -2.0f * 0.5f);
    const float viewHeight = _screen.height / _density;
    _viewY = (double)((float)config->startY - (viewHeight + viewHeight) * 0.5f);
    clampVisibleArea(true);
    _outbreakSettingsUnlocked = true;
    setupMap();
    _mapRenderer->preloadTiles((double)_mapConfig->worldX + _viewX, (double)_mapConfig->worldY + _viewY, _mapScale);
    startSimulation();
    // (setupMinimumActions topped up the bought actions; they are unlimited.)
}

void GameNode::setupMap()
{
    const MapConfig* config = _mapConfig;
    _gameOptions.populateValues(config->width, config->height, (GameOptions::ZombieOutbreak)config->zombieOutbreak,
                                config->populationLimit);
    _gameOptions.updateValueAvailability(_outbreakSettingsUnlocked);
    if (config->hasPresetOptions) {
        _gameOptions.getOptionNumCivs().setSelectedValueClosestAvailable(config->presetNumCivs);
        _gameOptions.getOptionArmedCivs().setSelectedValueClosestAvailable(config->presetArmedCivs);
        _gameOptions.getOptionCivAccuracy().setSelectedValueClosestAvailable(config->presetCivAccuracy);
        _gameOptions.getOptionNumZombies().setSelectedValueClosestAvailable(config->presetNumZombies);
        _gameOptions.getOptionZombieSpeed().setSelectedValueClosestAvailable(config->presetZombieSpeed);
        _gameOptions.getOptionInfectionTime().setSelectedValueClosestAvailable(config->presetInfectionTime);
    }
    if (_tutorialMode)
        _gameOptions.removeUnavailableValues();
    _exteriorRenderer->setupMap(config->width, config->height);
    _mapRenderer->setupMap(rendererMapType(config->type), config->id, config->width, config->height);
    // The port's: an online map's imagery is kept in its pack, and what the
    // pack lacks downloads behind what is on screen, so it plays offline
    // afterwards. (A player map's came whole when it was made. Past its
    // edges ExteriorRenderer draws black.)
    if (rendererMapType(config->type) == MapRenderer::MapType::Online) {
        SatelliteTileLoader* loader = SatelliteTileLoader::getInstance();
        loader->openMap(imageryPackPath(*config), config->worldX, config->worldY, config->width, config->height);
        loader->prefetchMap(true);
    }
    // (The game's leaked the last map's when a map started without stopMap;
    // the simulation has its own copy.)
    delete _collisionLayer;
    _collisionLayer = new CollisionLayer(config->type, config->id, config->width, config->height, config->playerPath);
}

void GameNode::startSimulation()
{
    _simulation = new Simulation(_mapConfig->width, _mapConfig->height, _collisionLayer, _helicopterOverlay,
                                 &_personTypeFactory, &_soundManager, this, _gameOptions.getSnapshot(), _mapConfig->id);
    setupSimulation();
}

void GameNode::restartSimulation()
{
    delete _simulation;
    _simulation = nullptr;
    if (_simulationRenderer != nullptr)
        _simulationRenderer->setSimulation(nullptr);
    _messageLabel->setVisible(false);
    startSimulation();
}

void GameNode::stopMap()
{
    // The online map's download stops; its pack is written out behind.
    if (isCurrentGoogleMap())
        SatelliteTileLoader::getInstance()->closeMap();
    _mapConfig = nullptr;
    delete _collisionLayer;
    _collisionLayer = nullptr;
    _collisionLayerRenderer->setCollisionLayer(nullptr);
    _mapRenderer->clear();
    delete _simulation;
    _simulation = nullptr;
    if (_simulationRenderer != nullptr)
        _simulationRenderer->setSimulation(nullptr);
    _messageLabel->setVisible(false);
    AudioEngineWrapper::stopAllEffects();
}

bool GameNode::isCurrentGoogleMap() const
{
    return _mapConfig != nullptr &&
           (_mapConfig->type == MapConfig::MapType::Google || _mapConfig->type == MapConfig::MapType::Player);
}

const char* GameNode::getImageryCredit() const
{
    // Esri's terms want the credit wherever the imagery shows:
    // the service's copyrightText. A player map's walls are OpenStreetMap's,
    // whose licence wants its credit too.
    if (!isCurrentGoogleMap())
        return nullptr;
    if (_mapConfig->type == MapConfig::MapType::Player)
        return "Source: Esri, Vantor, Earthstar Geographics,\nand the GIS User Community\n"
               "Map data \xc2\xa9 OpenStreetMap contributors";
    return "Source: Esri, Vantor, Earthstar Geographics,\nand the GIS User Community";
}

std::string GameNode::getMapDownloadStatus() const
{
    if (!isCurrentGoogleMap())
        return std::string();
    int done, total;
    SatelliteTileLoader::getInstance()->prefetchProgress(done, total);
    if (total <= 0 || done >= total)
        return std::string();
    char text[80];
    if (net::wifiConnected())
        snprintf(text, sizeof(text), "Downloading map: %d%%", done * 100 / total);
    else
        snprintf(text, sizeof(text), "Map download waiting for Wi-Fi (%d%%)", done * 100 / total);
    return text;
}

void GameNode::toggleCollisionLayer()
{
    _collisionLayerRenderer->toggleVisibility();
}

bool GameNode::getLastSimulationStats(Simulation::Stats& stats) const
{
    if (_simulation == nullptr)
        return false;
    stats = _stats;
    return true;
}

int GameNode::getNumTilesLoaded() const
{
    return _mapRenderer->getNumTilesLoaded();
}

void GameNode::onTrimMemory()
{
    _mapRenderer->onTrimMemory();
}

void GameNode::loadStep(bool& done, int& percent)
{
    const int state = _loadState;
    const u64 start = osGetTime();
    loadStepState(done, percent);
    if (state >= 0 && state < 4) {
        _loadMs[state] += osGetTime() - start;
        _loadItems[state]++;
    }
    if (done)
        Utils::log("load: renderer %d ms, people %d ms (%d), GUI %d ms (%d), sounds %d ms (%d)", (int)_loadMs[0],
                   (int)_loadMs[1], _loadItems[1], (int)_loadMs[2], _loadItems[2], (int)_loadMs[3], _loadItems[3]);
}

void GameNode::loadStepState(bool& done, int& percent)
{
    switch (_loadState) {
    case 0:
        _simulationRenderer = new SimulationRenderer(&_personTypeFactory, _density);
        _simulationRenderer->onSurfaceCreated();
        _simulationRenderer->onSurfaceChanged((int)_screen.width, (int)_screen.height);
        _loadState = 1;
        done = false;
        percent = 10;
        break;
    case 1: {
        bool typesDone = false;
        int typesPercent = 0;
        _personTypeFactory.loadStep(typesDone, typesPercent);
        if (typesDone)
            _loadState = 2;
        done = false;
        percent = typesPercent * 30 / 100 + 10;
        break;
    }
    case 2: {
        const char* name = kTextures[_loadTexture];
        if (strstr(name, ".png") != nullptr)
            Director::getInstance()->getTextureCache()->addImage(name);
        else if (strstr(name, ".plist") != nullptr)
            SpriteFrameCache::getInstance()->addSpriteFramesWithFile(name);
        else
            throw std::runtime_error(std::string("Unable to load texture ") + name);
        const int loaded = _loadTexture++;
        if (loaded > 4)
            _loadState = 3;
        done = false;
        percent = _loadTexture * 20 / 6 + 40;
        break;
    }
    case 3: {
        bool soundsDone = false;
        int soundsPercent = 0;
        _soundManager.loadStep(soundsDone, soundsPercent);
        if (soundsDone)
            _loadState = 4;
        done = soundsDone;
        percent = soundsPercent * 40 / 100 + 60;
        break;
    }
    default:
        throw std::runtime_error("GameNode::loadStep() has been called too many times");
    }
}

bool GameNode::performAction(ActionType::ActionType action, const Vec2& location)
{
    const float pixels = _mapScale * _density;
    const float x = (float)(_viewX + (double)(location.x / pixels));
    if (x < 0.0f)
        return false;
    const float y = (float)(_viewY + (double)((_screen.height - location.y) / pixels));
    if (y < 0.0f || !(x < (float)_mapConfig->width) || !(y < (float)_mapConfig->height))
        return false;
    switch (action) {
    case ActionType::Soldiers:
        _simulation->dropSoldier((int)x, (int)y);
        return true;
    case ActionType::Mk81Bombs:
        _simulation->dropBomb(BombTypeSmall, (int)x, (int)y);
        return true;
    case ActionType::Mk82Bombs:
        _simulation->dropBomb(BombTypeLarge, (int)x, (int)y);
        return true;
    case ActionType::Helicopters:
        return _simulation->deployHelicopter((int)x, (int)y);
    default:
        throw std::runtime_error("Invalid action type in performAction()");
    }
}

bool GameNode::getFocusedWaitingHelicopterPosition(Vec2& position, int& occupants)
{
    const Effects::Helicopter* helicopter = getFocusedWaitingHelicopter();
    if (helicopter == nullptr)
        return false;
    // The button's place: right of the helicopter and a little below.
    const double pixels = (double)(_mapScale * _density);
    position.x = (float)(int)(pixels * ((double)(helicopter->x + 0x4d) - _viewX));
    position.y = _screen.height - (float)(int)(pixels * ((double)(helicopter->y + 0x12) - _viewY));
    occupants = helicopter->occupants;
    return true;
}

Effects::Helicopter* GameNode::getFocusedWaitingHelicopter()
{
    if (_simulation == nullptr)
        return nullptr;
    const float pixels = _mapScale * _density;
    const float halfWidth = (float)(int)(_screen.width * 0.5f);
    const float halfHeight = (float)(int)(_screen.height * 0.5f);
    return _simulation->getEffects().getClosestWaitingHelicopter((int)(_viewX + (double)(halfWidth / pixels)),
                                                                  (int)(_viewY + (double)(halfHeight / pixels)));
}

void GameNode::evacuateFocusedWaitingHelicopter()
{
    const Effects::Helicopter* helicopter = getFocusedWaitingHelicopter();
    if (helicopter == nullptr)
        return;
    _simulation->evacuateHelicopter(helicopter);
}

std::string GameNode::getSavedStateFilename() const
{
    return _persistentDir + "/state.dat";
}

void GameNode::saveSimulation()
{
    const std::string path = getSavedStateFilename();
    if (_mapConfig == nullptr || _tutorialMode) {
        ::remove(path.c_str());
        return;
    }
    Utils::log("Saving game state");
    FILE* f = fopen(path.c_str(), "wb");
    if (f == nullptr) {
        Utils::log("Error opening file: %s", path.c_str());
        return;
    }
    Utils::writeInt(f, kSaveVersion);
    Utils::writeInt(f, _mapConfig->id);
    Utils::writeInt(f, (int)_mapConfig->type);
    if (_mapConfig->type == MapConfig::MapType::Player) {
        Utils::writeInt(f, _mapConfig->worldX);
        Utils::writeInt(f, _mapConfig->worldY);
        Utils::writeInt(f, _mapConfig->width);
        Utils::writeInt(f, _mapConfig->height);
        Utils::writeInt(f, _mapConfig->startX);
        Utils::writeInt(f, _mapConfig->startY);
        Utils::writeInt(f, _mapConfig->zombieOutbreak);
        Utils::writeInt(f, _mapConfig->populationLimit);
        Utils::writeBool(f, _mapConfig->hasPresetOptions);
        Utils::writeInt(f, _mapConfig->presetNumCivs);
        Utils::writeInt(f, _mapConfig->presetArmedCivs);
        Utils::writeInt(f, _mapConfig->presetCivAccuracy);
        Utils::writeInt(f, _mapConfig->presetNumZombies);
        Utils::writeInt(f, _mapConfig->presetZombieSpeed);
        Utils::writeInt(f, _mapConfig->presetInfectionTime);
        // The port's: the map's collision layer on the card (the game's was
        // PlayerMapCache's current one).
        Utils::writeString(f, _mapConfig->playerPath);
    }
    Utils::writeBool(f, _outbreakSettingsUnlocked);
    _simulation->writeToSaveFile(f);
    _gameOptions.writeToSaveFile(f);
    Utils::writeFloat(f, (float)_viewX);
    Utils::writeFloat(f, (float)_viewY);
    Utils::writeFloat(f, _mapScale);
    Utils::writeInt(f, kSaveMarker);
    fclose(f);
}

bool GameNode::restoreSavedSimulation()
{
    const std::string path = getSavedStateFilename();
    FILE* f = fopen(path.c_str(), "rb");
    if (f == nullptr) {
        Utils::log("Unable to read saved state as the file does not exist");
        return false;
    }
    const int version = Utils::readInt(f);
    if (version < 7 || version > kSaveVersion) {
        Utils::log("Unable to read saved state as the file is version %d, expected a version between %d and %d",
                   version, 7, kSaveVersion);
        fclose(f);
        return false;
    }
    fseek(f, -4, SEEK_END);
    const int marker = Utils::readInt(f);
    if (marker != kSaveMarker) {
        Utils::log("Unable to read saved state as the marker is 0x%x, expected 0x%x", marker, kSaveMarker);
        fclose(f);
        return false;
    }
    fseek(f, 4, SEEK_SET);
    Utils::log("Reading game state");
    const int mapId = Utils::readInt(f);
    const int mapType = Utils::readInt(f);
    if (mapType == (int)MapConfig::MapType::Google || mapType == (int)MapConfig::MapType::Embedded) {
        _mapConfig = _mapConfigFactory.getMapByTypeAndId((MapConfig::MapType)mapType, mapId);
        if (_mapConfig == nullptr) {
            Utils::log("Unable to read saved state as map %d does not exist", mapId);
            fclose(f);
            return false;
        }
    } else if (mapType == (int)MapConfig::MapType::Player) {
        MapConfig& config = _playerMapConfig;
        config = MapConfig();
        config.id = mapId;
        config.type = MapConfig::MapType::Player;
        config.worldX = Utils::readInt(f);
        config.worldY = Utils::readInt(f);
        config.width = Utils::readInt(f);
        config.height = Utils::readInt(f);
        config.startX = Utils::readInt(f);
        config.startY = Utils::readInt(f);
        config.zombieOutbreak = Utils::readInt(f);
        config.populationLimit = Utils::readInt(f);
        config.hasPresetOptions = Utils::readBool(f);
        config.presetNumCivs = Utils::readInt(f);
        config.presetArmedCivs = Utils::readInt(f);
        config.presetCivAccuracy = Utils::readInt(f);
        config.presetNumZombies = Utils::readInt(f);
        config.presetZombieSpeed = Utils::readInt(f);
        config.presetInfectionTime = Utils::readInt(f);
        config.playerPath = Utils::readString(f);
        if (config.playerPath.empty() || !FileUtils::getInstance()->isFileExist(config.playerPath)) {
            Utils::log("Unable to restore state due to missing current collision layer file");
            fclose(f);
            return false;
        }
        _mapConfig = &_playerMapConfig;
    } else {
        Utils::log("Unable to restore state due to invalid map type %d", mapType);
        fclose(f);
        return false;
    }

    _outbreakSettingsUnlocked = version < 8 ? true : Utils::readBool(f);
    setupMap();
    _simulation = new Simulation(_mapConfig->width, _mapConfig->height, _collisionLayer, _helicopterOverlay,
                                 &_personTypeFactory, &_soundManager, this, f, version);
    setupSimulation();
    _gameOptions.readFromSaveFile(f);
    _viewX = (double)Utils::readFloat(f);
    _viewY = (double)Utils::readFloat(f);
    _mapScale = Utils::readFloat(f);
    _mapRenderer->preloadTiles((double)_mapConfig->worldX + _viewX, (double)_mapConfig->worldY + _viewY, _mapScale);
    const int endMarker = Utils::readInt(f);
    fclose(f);
    if (endMarker != kSaveMarker) {
        Utils::log("Unable to read saved state as didn't find marker at end of file, found 0x%x, expected 0x%x",
                   endMarker, kSaveMarker);
        stopMap();
        return false;
    }
    return true;
}

void GameNode::setupSimulation()
{
    if (_simulationRenderer != nullptr)
        _simulationRenderer->setSimulation(_simulation);
    _collisionLayerRenderer->setCollisionLayer(&_simulation->getCollisionLayer());
    _simulation->getStats(_stats);
}

void GameNode::displayMessage(const char* message)
{
    _messageLabel->setString(message);
    _messageLabel->setVisible(true);
    _messageTime = _simulation != nullptr ? _simulation->getGameTime() : 0;
}
