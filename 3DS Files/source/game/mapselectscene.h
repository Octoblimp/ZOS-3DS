// MapSelectScene (decomp/classes/MapSelectScene.c): the map select. Three
// tabs over a world map, Offline (the 5 maps on the card), Online (the 15
// "google" maps) and Player, a marker on each map (spinning while it can be
// played: tap it to play) and Home back to HomeScene. In tutorial mode (after
// the tutorial's Change map) a Next button walks through the tabs first.
//
// The game's layout (the screens ported, dropping only what cannot work). Dropped with IAP: the unlock buttons and the Everything
// popup; every map is available, so the locked marker and its popup are kept
// but never show. The tutorial's texts about buying are reworded.
//
// The Player tab is PlayerMapNode, as the game's, over Esri's imagery with
// the player's own maps (PlayerMaps) where the game had
// Google Maps with class3outbreak.com's. The black bar along the bottom,
// which held the tab's unlock buttons, holds the port's: New map, Delete (then
// a marker, and Confirm in the game's popup), Search (a place's name on the
// 3DS's keyboard, PlaceSearch; X too), and while picking an area, its size,
// Cancel and Download. Download brings up the
// game's download popup (its progress on the second line) and, if the map
// cannot be made, its error popup. The top screen shows what to do, the
// steps and errors, and the imagery's and OpenStreetMap's credits.
//
// The game's background was in an inverted ClippingNode, a hole over the map
// for PlayerMapNode under it; the port's ClippingNode only scissors, so the
// hole is drawn black over the background (the world maps and the Player
// tab cover it).
#pragma once

#include <set>
#include <vector>

#include <string>

#include "cocos/director.h"
#include "game/errorpopup.h"
#include "game/highresolutiontimer.h"
#include "game/playermapnode.h"

class GameNode;
class HomeTopScreen;
class MapConfig;

class MapSelectScene : public cocos2d::Layer, private ErrorPopup::IListener, private PlayerMapNode::IListener {
public:
    // The tabs: the game's MapType::type.
    enum Tab { Offline = 0, Online = 1, Player = 2, None = 3 };

    static cocos2d::Scene* createScene(GameNode* game, bool tutorialMode);
    static MapSelectScene* create(GameNode* game, bool tutorialMode);
    // Logs the map and starts it (the game's static startMap, HomeScene's
    // tutorial's too).
    static void startMap(const MapConfig* map, bool tutorialMode, GameNode* game);

    ~MapSelectScene() override;

    bool init(GameNode* game, bool tutorialMode);
    void onEnter() override;
    void onExit() override;
    void onEnterTransitionDidFinish() override;
    // The background left of the screen, which MapSelectTransition brings in.
    int getOffScreenBackgroundWidthPixels() const { return _offScreenBackgroundWidth; }
    // The port's: a map's build polled, the buttons' keys.
    void update(float dt) override;

private:
    bool onTouchBegan(cocos2d::Touch* touch, cocos2d::Event* event);
    void onTouchMoved(cocos2d::Touch* touch, cocos2d::Event* event);
    void onTouchEnded(cocos2d::Touch* touch, cocos2d::Event* event);
    // The download popup's Cancel.
    void onErrorPopupClosed(ErrorPopup* popup) override;
    // A player map's marker.
    void onPlayerMapSelected(int id) override;
    // The tab's panel and markers, if it changed (or force).
    void loadMap(bool force);
    void rotateMapMarkers(float dt);
    void mapTabSelected(Tab tab);
    void setupTutorial();
    void nextTutorialStep();
    void startMap(const MapConfig* map);
    // The port's Player tab.
    void initPlayerTab();
    cocos2d::Sprite* createBarButton(const char* frame, const char* text, float place);
    // Browsing the maps, or picking a new one's area.
    void setPicking(bool picking);
    // The bar's buttons for the tab, picking and deleting.
    void updateBarButtons();
    void startDownload();
    // The 3DS's keyboard for a place's name, then PlaceSearch.
    void openSearch();
    // The top screen's line under the title (none for ""), the status
    // centred below it.
    void setSearchText(const std::string& text);
    // The top screen's text for now.
    std::string playerStatus() const;
    bool popupShowing() const;

    float _panelHeight = 0.0f;                     // 0x268
    float _margin = 0.0f;                          // 0x26c
    float _labelMargin = 0.0f;                     // 0x270
    Tab _tab = None;                               // 0x274
    Tab _loadedTab = None;                         // 0x278
    cocos2d::Sprite* _onlineWorldMap = nullptr;    // 0x27c
    cocos2d::Sprite* _offlineWorldMap = nullptr;   // 0x280
    cocos2d::Sprite* _offlineTab = nullptr;        // 0x284
    cocos2d::Sprite* _onlineTab = nullptr;         // 0x288
    cocos2d::Sprite* _playerTab = nullptr;         // 0x28c
    cocos2d::Sprite* _home = nullptr;              // 0x290
    cocos2d::Node* _blackGradient = nullptr;       // 0x2ac
    std::set<cocos2d::Node*> _buttons;             // 0x2b0
    std::set<cocos2d::Node*> _markers;             // 0x2c8
    std::set<cocos2d::Node*> _panelNodes;          // 0x2e0
    cocos2d::Node* _pressed = nullptr;             // 0x2f8
    cocos2d::Node* _pressedMarker = nullptr;       // 0x2fc
    cocos2d::Node* _pressedTab = nullptr;          // 0x300
    cocos2d::Node* _ui = nullptr;                  // 0x304
    cocos2d::Rect _mapRect;                        // 0x308
    cocos2d::Rect _worldMapRect;                   // 0x318
    cocos2d::Size _visible;                        // 0x328
    float _mapTop = 0.0f;                          // 0x338
    ErrorPopup* _unavailablePopup = nullptr;       // 0x340
    GameNode* _game = nullptr;                     // 0x34c
    std::vector<cocos2d::Sprite*> _unlockedMarkers; // 0x358, rotating
    HighResolutionTimer _timer;                    // 0x370
    cocos2d::Node* _playerMaps = nullptr;          // 0x378, PlayerMapNode's clipper
    bool _transitionFinished = false;              // 0x37c
    int _tutorialStep = 0;                         // 0x390
    cocos2d::Sprite* _tutorialNext = nullptr;      // 0x394
    cocos2d::Label* _tutorialLabel = nullptr;      // 0x398
    int _offScreenBackgroundWidth = 0;             // 0x39c
    // The port's.
    HomeTopScreen* _topScreen = nullptr;
    PlayerMapNode* _playerMapNode = nullptr;
    // The Player tab's buttons on the black bar.
    cocos2d::Sprite* _newMapButton = nullptr;
    cocos2d::Sprite* _sizeButton = nullptr;
    cocos2d::Sprite* _cancelButton = nullptr;
    cocos2d::Sprite* _downloadButton = nullptr;
    cocos2d::Sprite* _searchButton = nullptr;
    cocos2d::Sprite* _deleteButton = nullptr;
    ErrorPopup* _deletePopup = nullptr;
    // Delete pressed: a marker tapped asks to delete its map, not plays it.
    bool _deleting = false;
    int _deleteId = 0;
    ErrorPopup* _downloadPopup = nullptr;
    ErrorPopup* _errorPopup = nullptr;
    int _pickSize = 0;
    bool _downloading = false;
    // The touch is the map's (PlayerMapNode's).
    bool _mapTouch = false;
    // The Player tab's top screen.
    cocos2d::Node* _playerTop = nullptr;
    cocos2d::Label* _playerStatus = nullptr;
    std::string _playerStatusText;
    std::string _error;
    std::string _step;
    // A search under way; its line: under way, the place found, or why not.
    bool _searching = false;
    cocos2d::Label* _searchLabel = nullptr;
    float _statusTop = 0.0f;
    float _statusBottom = 0.0f;
};

// MapSelectTransition (decomp/classes/MapSelectTransition.c): the map select
// slides in from the right, the background left of it (Background_General's
// zombie hands) sweeping across first.
class MapSelectTransition : public cocos2d::TransitionMoveInL {
public:
    static MapSelectTransition* create(float t, cocos2d::Scene* scene);

protected:
    void initScenes() override;
};
