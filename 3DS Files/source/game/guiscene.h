// GUIScene (decomp/classes/GUIScene.c): the game screen. GameNode under
// the GUI: ActionsMenu on the right, ControlsMenu on the left, the action's
// overlay (a frame round the screen and "Tap to deploy") with its cancel
// button while an action is picked, the waiting helicopter's Evacuate
// panel, and in the tutorial its bar of texts with Next.
//
// A tap on the map that hardly moved performs the action; every other touch
// goes to GameNode (pan, pinch).
//
// The port's changes:
//   - the stats (civilians, zombies, the dead, time, kills, rescues) are on
//     the top screen, the bottom being the game's; the game drew them over
//     the map and faded those the menus covered.
//   - B cancels the action while the cancel button shows; Y evacuates while
//     Evacuate shows. GameNode's pad and L/R are off while the settings are
//     up.
//   - IAP and ads are gone: every action is unlimited (no "x%i" counts, no
//     ActionsPopup or EverythingPopup), and the tutorial's texts about
//     buying upgrades say so instead.
//   - the Google logo is not shown: the online maps' imagery is Esri's,
//     credited on the top screen under the stats, with the map's download.
//   - under the stats, a minimap and the population over time
//     (topscreenviews.h), with the stats' colours beside their labels.
#pragma once

#include <set>
#include <string>

#include "cocos/director.h"
#include "game/guilisteners.h"

class ActionsMenu;
class ControlsMenu;
class GameNode;
class MiniMap;
class PopulationGraph;

class GUIScene : public cocos2d::Layer, public ActionSelectedListener, public ControlSelectedListener {
public:
    // A Scene holding a GUIScene.
    static cocos2d::Scene* createScene(GameNode* game);
    static GUIScene* create(GameNode* game);

    GUIScene();
    ~GUIScene() override;

    bool init(GameNode* game);

    void onEnter() override;
    // Stops the map (the game's), and gives the top screen back.
    void onExit() override;
    void update(float dt) override;

    // ActionSelectedListener.
    void onActionsMenuToggled(bool open) override;
    void onActionSelected(ActionType::ActionType action) override;
    // ControlSelectedListener.
    void onSettingsSelected(bool visible) override;
    void onSettingsRestart() override;

private:
    void initStatsLabels();
    void updateStats(float dt);
    // The port's: the minimap under the right column, the graph under the
    // left (their bottoms given), and the colours beside the left labels.
    void layoutTopViews(float leftBottom, float rightBottom);
    void initMisc();
    void initOverlay(ActionType::ActionType action);
    void setupTutorial();
    void nextTutorialStep();
    // Back to no action: the overlays and cancel hidden, the menus shown.
    void exitAction();

    bool onTouchBegan(cocos2d::Touch* touch, cocos2d::Event* event);
    void onTouchEnded(cocos2d::Touch* touch, cocos2d::Event* event);
    void onTouchesBegan(const std::vector<cocos2d::Touch*>& touches, cocos2d::Event* event);
    void onTouchesMoved(const std::vector<cocos2d::Touch*>& touches, cocos2d::Event* event);
    void onTouchesEnded(const std::vector<cocos2d::Touch*>& touches, cocos2d::Event* event);

    ControlsMenu* _controlsMenu = nullptr;           // 0x26c
    ActionsMenu* _actionsMenu = nullptr;             // 0x270
    cocos2d::Node* _overlays[4] = {};                // 0x27c: soldiers, MK81, MK82, helicopters
    ActionType::ActionType _action = ActionType::None; // 0x29c
    cocos2d::Sprite* _cancel = nullptr;              // 0x2a0
    cocos2d::Sprite* _buttonBackground = nullptr;    // 0x2a4, not in the tutorial
    // The stats, on the top screen.
    cocos2d::Node* _topScreen = nullptr;
    cocos2d::Label* _civsLabel = nullptr;            // 0x2a8
    cocos2d::Label* _zombiesLabel = nullptr;         // 0x2ac
    cocos2d::Label* _deadLabel = nullptr;            // 0x2b0
    cocos2d::Label* _timeLabel = nullptr;            // 0x2b4
    cocos2d::Label* _zombiesKilledLabel = nullptr;   // 0x2b8
    cocos2d::Label* _civsKilledLabel = nullptr;      // 0x2bc
    cocos2d::Label* _civsRescuedLabel = nullptr;     // 0x2c0
    float _statsLineHeight = 0.0f;
    // The port's: the imagery's credit, and the online map's download.
    cocos2d::Label* _creditLabel = nullptr;
    cocos2d::Label* _downloadLabel = nullptr;
    std::string _downloadText;
    // The port's: what the top screen shows between the stats and the
    // credit (topscreenviews.h).
    MiniMap* _miniMap = nullptr;
    PopulationGraph* _graph = nullptr;
    cocos2d::DrawNode* _legend = nullptr;
    float _viewsBottom = 0.0f;
    std::set<cocos2d::Node*> _touchable;             // 0x2d4
    cocos2d::Size _screen;                           // 0x2ec
    cocos2d::Node* _pressed = nullptr;               // 0x2f4
    GameNode* _game = nullptr;                       // 0x2fc
    float _statsInterval = 0.1f;                     // 0x304
    float _touchMoveX = 0.0f;                        // 0x30c
    float _touchMoveY = 0.0f;                        // 0x310
    cocos2d::Sprite* _helicopterPanel = nullptr;     // 0x314
    cocos2d::Sprite* _evacuate = nullptr;            // 0x318
    cocos2d::Label* _helicopterCivsLabel = nullptr;  // 0x31c
    int _helicopterCivs = 0;                         // 0x320
    int _tutorialStep = 0;                           // 0x324: the next text's
    cocos2d::DrawNode* _tutorialBar = nullptr;       // 0x328
    cocos2d::Sprite* _tutorialNext = nullptr;        // 0x32c
    cocos2d::Label* _tutorialLabel = nullptr;        // 0x330
};
