// GameNode (decomp/classes/GameNode.c): the game screen. It owns the map,
// its collision layer, the simulation and the renderers, and draws them in
// that order each frame, stepping the simulation first. The view is
// viewX, viewY (the map pixel at the screen's top left) and mapScale.
//
// On the 3DS it is the bottom screen: its screen is the 480x360 canvas the
// Director draws at 2/3. The stats go on the top screen. The stylus pans as
// a finger did; there is one touch, so no pinch: L and R zoom and the circle
// pad and D-pad pan (the port's).
//
// Dropped with the Android-only classes: InAppPurchaseManager (everything is
// unlocked: every map, the outbreak settings, unlimited actions), AdManager,
// PlayerMapCache and the Google Maps SDK cache (OpenStreetMap's maps take
// their place).
#pragma once

#include <string>
#include <vector>

#include "cocos/director.h"
#include "cocos/node.h"
#include "game/actiontype.h"
#include "game/gameoptions.h"
#include "game/mapconfig.h"
#include "game/mapconfigfactory.h"
#include "game/persontype.h"
#include "game/simulation.h"
#include "game/soundmanager.h"

class CollisionLayer;
class CollisionLayerRenderer;
class CollisionOverlay;
class ExteriorRenderer;
class MapRenderer;
class SimulationRenderer;

class GameNode : public cocos2d::Node, public Simulation::IObserver {
public:
    // The port's create: new, init, autorelease. persistentDir is where
    // state.dat goes.
    static GameNode* create(float density, const cocos2d::Size& screen, const std::string& persistentDir);

    GameNode();
    ~GameNode() override;
    GameNode(const GameNode&) = delete;
    GameNode& operator=(const GameNode&) = delete;

    // The game's took the device model (an iPad showed Google's copyright),
    // the Google Maps SDK's cache, the player map cache and the store.
    bool init(float density, const cocos2d::Size& screen, const std::string& persistentDir);

    void draw(cocos2d::Renderer* renderer, const cocos2d::Mat4& transform, u32 flags) override;
    // The port's: L/R zoom, circle pad and D-pad pan, while controls are on.
    void update(float dt) override;

    // GUIScene passes its touches on: one pans, two pinch.
    void onTouchesBegan(const std::vector<cocos2d::Touch*>& touches, cocos2d::Event* event) {}
    void onTouchesMoved(const std::vector<cocos2d::Touch*>& touches, cocos2d::Event* event);
    void onTouchesEnded(const std::vector<cocos2d::Touch*>& touches, cocos2d::Event* event) {}
    // The port's: GUIScene turns the buttons off while a menu is up.
    void setControlsEnabled(bool on) { _controlsEnabled = on; }

    float getMapOffsetX() const { return (float)_viewX; }
    float getMapOffsetY() const { return (float)_viewY; }
    float getMapScale() const { return _mapScale; }
    MapConfigFactory& getMapConfigFactory() { return _mapConfigFactory; }
    // The port's: HomeScene's top screen draws the people once they load.
    const PersonTypeFactory& getPersonTypeFactory() const { return _personTypeFactory; }

    // A player map's config is copied, the rest are the factory's.
    void startMap(const MapConfig* config, bool tutorialMode);
    void restartSimulation();
    void stopMap();
    GameOptions& getGameOptions() { return _gameOptions; }
    bool getTutorialMode() const { return _tutorialMode; }
    // Google's or a player's map: imagery from online (Esri's, here).
    bool isCurrentGoogleMap() const;
    // The port's, for GUIScene's top screen: the online imagery's credit (on
    // two lines), or nullptr when the map's imagery is the game's own.
    const char* getImageryCredit() const;
    // The port's: how the online map's download goes ("Downloading map:
    // 40%"), or empty once it is all on the card.
    std::string getMapDownloadStatus() const;
    int getCurrentMapPlayingAreaPixelX() const { return _mapConfig ? _mapConfig->worldX : 0; }
    int getCurrentMapPlayingAreaPixelY() const { return _mapConfig ? _mapConfig->worldY : 0; }
    void toggleCollisionLayer();
    // False when there is no simulation.
    bool getLastSimulationStats(Simulation::Stats& stats) const;
    float getScreenDensityScale() const { return _density; }
    MapRenderer* getMapRenderer() { return _mapRenderer; }
    int getNumTilesLoaded() const;
    void onTrimMemory();
    // The port's: the running simulation, or nullptr (the top screen's stats).
    Simulation* getSimulation() { return _simulation; }
    const MapConfig* getMapConfig() const { return _mapConfig; }

    // The loading screen's: one step a call (the simulation renderer, the
    // person types, the GUI's textures, the sounds). done after the last;
    // percent 10..100.
    void loadStep(bool& done, int& percent);
    bool finishedLoading() const { return _loadState == 4; }

    // A tap at location (the screen's, y up): true if it was on the map and
    // the action was carried out.
    bool performAction(ActionType::ActionType action, const cocos2d::Vec2& location);
    // The waiting helicopter nearest the screen's middle: its button's place
    // on screen (y up), and how many are aboard.
    bool getFocusedWaitingHelicopterPosition(cocos2d::Vec2& position, int& occupants);
    Effects::Helicopter* getFocusedWaitingHelicopter();
    void evacuateFocusedWaitingHelicopter();

    // The port's positional sound (SoundManager), kept in Prefs.
    bool getPositionalSound() const { return _soundManager.isPositional(); }
    void setPositionalSound(bool on);

    // The 3DS's: on leaving the app (HOME, sleep, quit).
    void applicationDidEnterBackground() { saveSimulation(); }
    // state.dat: the map, the simulation, the options and the view. Removed
    // instead when there is no map or it is the tutorial.
    void saveSimulation();
    std::string getSavedStateFilename() const;
    // The map and simulation from state.dat, if there is one that reads.
    bool restoreSavedSimulation();
    bool getOutbreakSettingsUnlocked() const { return _outbreakSettingsUnlocked; }

    // Simulation::IObserver: shown in the middle of the screen's lower half
    // for 5 s of game time.
    void displayMessage(const char* message) override;

private:
    // loadStep's body: one item of the current load state.
    void loadStepState(bool& done, int& percent);
    // Keeps the view on the map: within 80 screen pixels of its edges, or
    // (initial) 10 map pixels inside them. A map narrower than the view is
    // centred.
    void clampVisibleArea(bool initial);
    void setupMap();
    void startSimulation();
    void setupSimulation();
    // The port's: zoom to scale about the screen point (x, y), y down.
    void zoomTo(float scale, float x, float y);
    float minimumMapScale() const;

    cocos2d::Size _screen;                          // 0x24c
    float _density = 1.0f;                          // 0x254
    std::string _persistentDir;                     // 0x258
    // The map pixel at the screen's top left.
    double _viewX = 0.0;                            // 0x260
    double _viewY = 0.0;                            // 0x268
    float _mapScale = 1.0f;                         // 0x270
    MapConfigFactory _mapConfigFactory;             // 0x274
    const MapConfig* _mapConfig = nullptr;          // 0x280
    bool _tutorialMode = false;                     // 0x284
    GameOptions _gameOptions;                       // 0x288
    CollisionLayer* _collisionLayer = nullptr;      // 0x388
    MapRenderer* _mapRenderer = nullptr;            // 0x384
    CollisionLayerRenderer* _collisionLayerRenderer = nullptr; // 0x38c
    CollisionOverlay* _helicopterOverlay = nullptr; // 0x390
    PersonTypeFactory _personTypeFactory;           // 0x394
    SoundManager _soundManager;                     // 0x3d0
    Simulation* _simulation = nullptr;              // 0x3ec
    SimulationRenderer* _simulationRenderer = nullptr; // 0x3f0
    Simulation::Stats _stats = {};                  // 0x3f8
    ExteriorRenderer* _exteriorRenderer = nullptr;  // 0x428
    cocos2d::Label* _messageLabel = nullptr;        // 0x42c
    // (0x430, the game's copyright label, showed Google's on an iPad. The
    // port's credit for Esri's imagery is on the top screen, GUIScene's,
    // where it reads: on this canvas, drawn at 2/3, small text blurs.)
    int _messageTime = -1;                          // 0x434
    int _loadState = 0;                             // 0x440
    int _loadTexture = 0;                           // 0x444
    // The port's: each load state's time and items, for the log.
    u64 _loadMs[4] = {};
    int _loadItems[4] = {};
    MapConfig _playerMapConfig;                     // 0x448
    bool _outbreakSettingsUnlocked = false;         // 0x494
    bool _controlsEnabled = true;
};
