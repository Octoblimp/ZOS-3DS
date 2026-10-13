// SimulationRenderer (decomp/classes/SimulationRenderer.c): the people, drawn
// over the map. Zoomed in (map scale above 0.3) each is a frame of its
// type's animation for its state, turned to face the way it heads, the
// living with a shadow; under a roof (collision bit 4) the "hidden" outline
// frames instead. Zoomed out (below 0.4) they are coloured squares instead:
// zombies red, civilians green, police blue, soldiers dark green. The two
// cross-fade over 0.3..0.4.
//
// The game's shaders (Person, PersonShadow, PersonMarker) only multiply a
// premultiplied texel or a colour by an alpha, which the port does with the
// vertex colour. Positions are screen pixels from the top left, as the
// game's; doRender turns them into its node's space (y up).
#pragma once

#include <vector>

#include "cocos/base.h"
#include "game/effectsrenderer.h"
#include "gfx/gfx.h"

class AnimationPair;
class PersonType;
class PersonTypeFactory;
class Simulation;

class SimulationRenderer {
public:
    // density is the screen's (the game's content scale), which sizes the
    // people and the markers: 1 on the 3DS.
    SimulationRenderer(PersonTypeFactory* personTypes, float density);
    ~SimulationRenderer();

    SimulationRenderer(const SimulationRenderer&) = delete;
    SimulationRenderer& operator=(const SimulationRenderer&) = delete;

    // Loads the shadow.
    void onSurfaceCreated();
    // The screen, in pixels.
    void onSurfaceChanged(int width, int height);
    void setSimulation(Simulation* simulation);

    // viewX, viewY is the map pixel at the screen's top left; mapScale the
    // zoom, screen pixels a map pixel before density.
    void prepareRender(float viewX, float viewY, float mapScale);
    // transform is the node's (its space is the screen's, y up).
    void doRender(const cocos2d::Mat4& transform);

    float getOpenGLViewOffsetX() const { return _viewX; }
    float getOpenGLViewOffsetY() const { return _viewY; }
    float getMapScale() const { return _mapScale; }

private:
    // 0x2c bytes: a person as drawn, taken at prepareRender.
    struct PersonRenderInfo {
        float x, y;             // 0x00
        float dirX, dirY;       // 0x08
        int state;              // 0x10
        int animTime;           // 0x14
        int collisionValue;     // 0x18
        int idleAnim;           // 0x1c
        int dieAnim;            // 0x20
        // On screen.
        float screenX, screenY; // 0x24
    };

    // On screen.
    struct MarkerRenderInfo {
        float x, y;
    };

    // By person type (PersonType::_index).
    using PeopleToRender = std::vector<std::vector<PersonRenderInfo>>;

    void allocatePeopleToRender(PeopleToRender& people, int reserve);
    static void clearPeopleToRender(PeopleToRender& people);
    // Those in view with a state flag (LiveZombie, LiveHuman).
    void prepareRenderPeopleMarkers(std::vector<MarkerRenderInfo>& markers, int stateFlag);
    void prepareRenderPoliceMarkers();
    void prepareRenderSoldierMarkers();
    // Everyone within 40 px of the screen, the living and the dead apart.
    void prepareRenderPeople();
    void addMarker(std::vector<MarkerRenderInfo>& markers, float x, float y);

    void doRenderPeople(const cocos2d::Mat4& transform);
    // colour is 0xrrggbb; size in pixels (before density).
    void doRenderPeopleMarkers(const cocos2d::Mat4& transform, const std::vector<MarkerRenderInfo>& markers,
                               int colour, float size);
    void renderPeopleToRender(const cocos2d::Mat4& transform, const PeopleToRender& people, float alpha);
    void renderPeopleShadows(const cocos2d::Mat4& transform, float alpha);
    // The pose for the person's state; throws "Invalid person state N" and
    // "Invalid zombie die animation N".
    static const AnimationPair* getAnimationPair(const PersonRenderInfo& person, const PersonType* type);
    void populatePersonVerticesAndTexCoords(const cocos2d::Mat4& transform, const PersonRenderInfo& person,
                                            const PersonType* type, u32 colour);
    // A screen point (y down) in the node's space.
    cocos2d::Vec2 toNode(const cocos2d::Mat4& transform, float x, float y) const;

    float _density;                     // 0x00
    // The map, in map pixels.
    int _width = 0;                     // 0x04
    int _height = 0;                    // 0x08
    cocos2d::Size _screen;              // 0x0c
    PersonTypeFactory* _personTypes;    // 0x1c
    std::vector<PersonType*> _types;    // 0x20
    gfx::Texture _shadow;               // 0x54
    int _shadowWidth = 0;               // 0x58
    int _shadowHeight = 0;              // 0x5c
    Simulation* _simulation = nullptr;  // 0x1334
    float _viewX = 0;                   // 0x1338
    float _viewY = 0;                   // 0x133c
    float _mapScale = 1;                // 0x1340
    // _mapScale * _density: screen pixels a map pixel.
    float _scale = 1;                   // 0x1344
    PeopleToRender _people;             // 0x1348
    PeopleToRender _corpses;            // 0x1354
    std::vector<MarkerRenderInfo> _zombieMarkers;   // 0x1360
    std::vector<MarkerRenderInfo> _humanMarkers;    // 0x136c
    std::vector<MarkerRenderInfo> _policeMarkers;   // 0x1378
    std::vector<MarkerRenderInfo> _soldierMarkers;  // 0x1384
    int _gameTime = 0;                  // 0x1390
    EffectsRenderer _effects;           // 0x1394
};
