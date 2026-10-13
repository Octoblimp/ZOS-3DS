// PlayerMapNode (decomp/classes/PlayerMapNode.c): MapSelectScene's Player
// tab, the world's imagery under the player maps' markers. The game's was
// Google Maps with class3outbreak.com's thousands of maps, clustered by
// PlayerMapMarkerManager; the port's are the player's own (PlayerMaps), few
// enough to show each, over Esri's imagery through GameNode's MapRenderer
// (free while MapSelectScene shows: GUIScene stops the map on its way out).
//
// As the game's: the view starts with the whole world across, centred on the
// Atlantic; a drag pans, clamped to the world; a map's square shows (a 10%
// fill and a half-alpha outline in the unlocked red) once it is 8 dp across,
// its marker spinning at its middle; a tap within 24 dp of one marker plays
// it, of two zooms in on them (0.2 s). The port's, the 3DS having one touch
// and buttons: L/R zoom about the middle, the circle pad and D-pad pan, and
// a tap off the markers zooms in twice, bringing the spot to the middle.
//
// The port's picking: while picking, a square of the new map's size stays in
// the middle, for the player to move the world under it.
//
// The node's space is the screen's: MapSelectScene puts it in a ClippingNode
// over the map's rectangle, at minus its corner.
#pragma once

#include <vector>

#include "cocos/director.h"
#include "game/highresolutiontimer.h"

class GameNode;

class PlayerMapNode : public cocos2d::Node {
public:
    class IListener {
    public:
        virtual ~IListener() = default;
        // A marker tapped: the map to play.
        virtual void onPlayerMapSelected(int id) = 0;
    };

    // rect: the map's place on the screen.
    static PlayerMapNode* create(GameNode* game, const cocos2d::Rect& rect);

    bool init(GameNode* game, const cocos2d::Rect& rect);
    void draw(cocos2d::Renderer* renderer, const cocos2d::Mat4& transform, u32 flags) override;
    void update(float dt) override;
    void onExit() override;

    void setListener(IListener* listener) { _listener = listener; }
    // Frees the imagery (MapSelectScene's static startMap, and leaving the
    // tab); the next draw sets it up again.
    void clear();

    // From MapSelectScene's touch listener, in the screen's space.
    void touchBegan(cocos2d::Touch* touch);
    void touchMoved(cocos2d::Touch* touch);
    void touchEnded(cocos2d::Touch* touch);

    // The port's: picking a new map's area of size world pixels, or not.
    void setPicking(bool picking, int size);
    bool isPicking() const { return _picking; }
    // The world pixel (zoom 20) under the square's middle.
    void getPickCentre(int& x, int& y) const;
    // The scale at which the square is half the map's height: the picker
    // zooms in to it, if further out, when picking starts.
    float pickScale(int size) const;
    // Whether the square is big enough on screen to see where it is.
    bool pickVisible() const;
    // The port's search: the place (zoom-20 world pixels, its middle and
    // extent) in the middle, all of it in view but no nearer than a large
    // map's square, or, picking, at the square's scale.
    void showPlace(double x, double y, double width, double height);

private:
    void clampVisibleArea();
    // Zooms to scale over 0.2 s, the world pixel worldX, worldY moving from
    // where it is on the screen to to (screen pixels from the top left).
    void startAnimatedZoom(double worldX, double worldY, float scale, const cocos2d::Vec2& to);
    void updateAnimatedZoom();
    void zoomAbout(float scale, const cocos2d::Vec2& about);
    // Markers and squares to the maps and the view.
    void updateMarkers();
    // The screen point (from the top left) of a world pixel.
    cocos2d::Vec2 toScreen(double worldX, double worldY) const;

    GameNode* _game = nullptr;
    cocos2d::Rect _rect;           // y up, as the screen
    cocos2d::Size _screen;
    bool _setUp = false;           // 0x278: the renderer has the world
    // 0x260, 0x268: the world pixel at the screen's top left.
    double _viewX = 0.0;
    double _viewY = 0.0;
    float _scale = 0.0f;           // 0x270: screen pixels a world pixel
    float _minScale = 0.0f;        // 0x274
    // 0x324, 0x328: the touch's moves, summed.
    float _movedX = 0.0f;
    float _movedY = 0.0f;
    // 0x330..0x34c: the animated zoom.
    bool _animating = false;
    float _animFrom = 0.0f;
    float _animTo = 0.0f;
    double _animX = 0.0, _animY = 0.0;
    cocos2d::Vec2 _animStart, _animEnd;
    HighResolutionTimer _animTimer;
    IListener* _listener = nullptr;  // 0x32c
    HighResolutionTimer _timer;      // 0x354, the markers' spin

    cocos2d::DrawNode* _squares = nullptr;
    std::vector<cocos2d::Sprite*> _markers;
    bool _picking = false;
    int _pickSize = 0;
};
