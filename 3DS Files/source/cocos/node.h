// The cocos2d-x scene graph the game builds on: Node and the node types it
// creates (Scene, Layer, LayerColor, Sprite, Label, DrawNode,
// ClippingNode), with textures and sprite frames.
//
// Drawing is immediate: visit() walks the tree in cocos2d's order (children
// below z 0, the node, the rest) and each draw() appends to gfx's batch.
// cocos2d instead queued render commands and ran them after the visit,
// which comes to the same picture.
#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "base.h"
#include "../gfx/gfx.h"

namespace cocos2d {

class Action;
class ActionManager;
class Scheduler;
class EventDispatcher;
class Touch;
class Renderer {}; // draw()'s first parameter; drawing goes through gfx.

using SEL_SCHEDULE = void (Ref::*)(float);
#define CC_SCHEDULE_SELECTOR(_SELECTOR) static_cast<cocos2d::SEL_SCHEDULE>(&_SELECTOR)
#define CC_CALLBACK_0(__selector__, __target__, ...) std::bind(&__selector__, __target__, ##__VA_ARGS__)
#define CC_CALLBACK_1(__selector__, __target__, ...) \
    std::bind(&__selector__, __target__, std::placeholders::_1, ##__VA_ARGS__)
#define CC_CALLBACK_2(__selector__, __target__, ...) \
    std::bind(&__selector__, __target__, std::placeholders::_1, std::placeholders::_2, ##__VA_ARGS__)

class Node : public Ref {
public:
    static const int INVALID_TAG = -1;

    static Node* create();
    Node();
    ~Node() override;
    virtual bool init();

    virtual void setLocalZOrder(int z);
    int getLocalZOrder() const { return _localZOrder; }
    void setZOrder(int z) { setLocalZOrder(z); }
    void setGlobalZOrder(float z) { _globalZOrder = z; }
    float getGlobalZOrder() const { return _globalZOrder; }

    virtual void setPosition(const Vec2& p);
    void setPosition(float x, float y) { setPosition(Vec2(x, y)); }
    const Vec2& getPosition() const { return _position; }
    void setPositionX(float x) { setPosition(Vec2(x, _position.y)); }
    void setPositionY(float y) { setPosition(Vec2(_position.x, y)); }
    float getPositionX() const { return _position.x; }
    float getPositionY() const { return _position.y; }
    // A fraction of the parent's content size, resolved when drawn.
    void setNormalizedPosition(const Vec2& p);

    virtual void setScale(float s);
    void setScale(float sx, float sy);
    virtual void setScaleX(float s);
    virtual void setScaleY(float s);
    float getScale() const { return _scaleX; }
    float getScaleX() const { return _scaleX; }
    float getScaleY() const { return _scaleY; }
    virtual void setRotation(float degrees);
    float getRotation() const { return _rotation; }

    virtual void setAnchorPoint(const Vec2& p);
    const Vec2& getAnchorPoint() const { return _anchorPoint; }
    const Vec2& getAnchorPointInPoints() const { return _anchorPointInPoints; }
    void setIgnoreAnchorPointForPosition(bool ignore);
    virtual void setContentSize(const Size& s);
    virtual const Size& getContentSize() const { return _contentSize; }

    virtual void setVisible(bool v) { _visible = v; }
    bool isVisible() const { return _visible; }

    virtual void setColor(const Color3B& c);
    const Color3B& getColor() const { return _realColor; }
    const Color3B& getDisplayedColor() const { return _displayedColor; }
    virtual void setOpacity(u8 o);
    u8 getOpacity() const { return _realOpacity; }
    u8 getDisplayedOpacity() const { return _displayedOpacity; }
    void setCascadeOpacityEnabled(bool on);
    void setCascadeColorEnabled(bool on);
    bool isCascadeOpacityEnabled() const { return _cascadeOpacity; }
    virtual void updateDisplayedOpacity(u8 parentOpacity);
    virtual void updateDisplayedColor(const Color3B& parentColor);

    virtual void addChild(Node* child);
    virtual void addChild(Node* child, int localZOrder);
    virtual void addChild(Node* child, int localZOrder, int tag);
    Node* getChildByTag(int tag) const;
    Node* getChildByName(const std::string& name) const;
    const std::vector<Node*>& getChildren() const { return _children; }
    size_t getChildrenCount() const { return _children.size(); }
    Node* getParent() const { return _parent; }
    void setParent(Node* p) { _parent = p; }
    void removeFromParent() { removeFromParentAndCleanup(true); }
    void removeFromParentAndCleanup(bool cleanup);
    virtual void removeChild(Node* child, bool cleanup = true);
    void removeChildByTag(int tag, bool cleanup = true);
    void removeAllChildren() { removeAllChildrenWithCleanup(true); }
    virtual void removeAllChildrenWithCleanup(bool cleanup);
    void reorderChild(Node* child, int localZOrder);

    int getTag() const { return _tag; }
    void setTag(int t) { _tag = t; }
    const std::string& getName() const { return _name; }
    void setName(const std::string& n) { _name = n; }
    void* getUserData() const { return _userData; }
    void setUserData(void* d) { _userData = d; }
    // Retained, and released with the node.
    Ref* getUserObject() const { return _userObject; }
    void setUserObject(Ref* o);

    virtual void onEnter();
    virtual void onEnterTransitionDidFinish();
    virtual void onExitTransitionDidStart();
    virtual void onExit();
    virtual void cleanup();
    bool isRunning() const { return _running; }

    virtual void visit(Renderer* renderer, const Mat4& parentTransform, u32 parentFlags);
    virtual void draw(Renderer* renderer, const Mat4& transform, u32 flags) {}

    const Mat4& getNodeToParentTransform();
    Mat4 getNodeToWorldTransform();
    Mat4 getWorldToNodeTransform() { return getNodeToWorldTransform().inverse(); }
    Vec2 convertToNodeSpace(const Vec2& worldPoint);
    Vec2 convertToWorldSpace(const Vec2& nodePoint);
    Vec2 convertToNodeSpaceAR(const Vec2& worldPoint) { return convertToNodeSpace(worldPoint) - _anchorPointInPoints; }
    Vec2 convertToWorldSpaceAR(const Vec2& nodePoint) { return convertToWorldSpace(nodePoint + _anchorPointInPoints); }
    Vec2 convertTouchToNodeSpace(Touch* touch);
    Rect getBoundingBox();

    Action* runAction(Action* action);
    void stopAllActions();
    void stopAction(Action* action);
    void stopActionByTag(int tag);
    Action* getActionByTag(int tag);
    int getNumberOfRunningActions();
    ActionManager* getActionManager() const;

    void scheduleUpdate();
    void scheduleUpdateWithPriority(int priority);
    void unscheduleUpdate();
    virtual void update(float dt) {}
    void schedule(SEL_SCHEDULE selector);
    void schedule(SEL_SCHEDULE selector, float interval);
    void schedule(SEL_SCHEDULE selector, float interval, unsigned repeat, float delay);
    void scheduleOnce(SEL_SCHEDULE selector, float delay);
    void schedule(const std::function<void(float)>& fn, float interval, const std::string& key);
    void scheduleOnce(const std::function<void(float)>& fn, float delay, const std::string& key);
    void unschedule(SEL_SCHEDULE selector);
    void unschedule(const std::string& key);
    void unscheduleAllCallbacks();
    Scheduler* getScheduler() const;

    void pause();
    void resume();

    EventDispatcher* getEventDispatcher() const;

    // Order of the last event-priority walk (EventDispatcher).
    int _eventOrder = 0;

protected:
    void insertChild(Node* child, int z);
    void sortChildren();

    std::vector<Node*> _children;
    Node* _parent = nullptr;
    int _localZOrder = 0;
    unsigned _orderOfArrival = 0;
    float _globalZOrder = 0.0f;
    bool _reorderDirty = false;

    Vec2 _position;
    Vec2 _normalizedPosition;
    bool _usingNormalizedPosition = false;
    float _scaleX = 1.0f, _scaleY = 1.0f;
    float _rotation = 0.0f;
    Vec2 _anchorPoint;
    Vec2 _anchorPointInPoints;
    Size _contentSize;
    bool _ignoreAnchorPointForPosition = false;
    bool _transformDirty = true;
    Mat4 _transform;
    Mat4 _modelViewTransform;

    bool _visible = true;
    Color3B _realColor = Color3B::WHITE;
    Color3B _displayedColor = Color3B::WHITE;
    u8 _realOpacity = 255;
    u8 _displayedOpacity = 255;
    bool _cascadeOpacity = false;
    bool _cascadeColor = false;

    int _tag = INVALID_TAG;
    std::string _name;
    void* _userData = nullptr;
    Ref* _userObject = nullptr;
    bool _running = false;
};

class Scene : public Node {
public:
    static Scene* create();
    Scene();
};

class Layer : public Node {
public:
    static Layer* create();
    Layer();
    bool init() override;
};

class LayerColor : public Layer {
public:
    static LayerColor* create();
    static LayerColor* create(const Color4B& color);
    static LayerColor* create(const Color4B& color, float width, float height);
    bool initWithColor(const Color4B& color, float width, float height);
    void draw(Renderer* renderer, const Mat4& transform, u32 flags) override;
};

class Texture2D : public Ref {
public:
    Texture2D() = default;
    ~Texture2D() override;
    bool initWithFile(const std::string& fullPath);
    bool initWithRGBA(const u8* rgba, int width, int height);
    int getPixelsWide() const { return _tex.width; }
    int getPixelsHigh() const { return _tex.height; }
    Size getContentSize() const { return Size((float)_tex.width, (float)_tex.height); }
    gfx::Texture* gfxTexture() { return &_tex; }
    void setAntiAliasTexParameters() { gfx::setTextureFilter(&_tex, true); }
    void setAliasTexParameters() { gfx::setTextureFilter(&_tex, false); }

private:
    gfx::Texture _tex;
};

class TextureCache {
public:
    // A file the game names (images/.../x.png), found through FileUtils.
    Texture2D* addImage(const std::string& path);
    void removeTexture(Texture2D* texture);
    void removeUnusedTextures();
    Texture2D* getTextureForKey(const std::string& key) const;

private:
    std::map<std::string, Texture2D*> _textures;
};

class SpriteFrame : public Ref {
public:
    static SpriteFrame* createWithTexture(Texture2D* texture, const Rect& rect);
    static SpriteFrame* createWithTexture(Texture2D* texture, const Rect& rect, bool rotated,
                                          const Vec2& offset, const Size& originalSize);
    ~SpriteFrame() override;
    Texture2D* getTexture() const { return _texture; }
    const Rect& getRect() const { return _rect; }
    bool isRotated() const { return _rotated; }
    const Vec2& getOffset() const { return _offset; }
    const Size& getOriginalSize() const { return _originalSize; }

private:
    Texture2D* _texture = nullptr;
    Rect _rect;
    bool _rotated = false;
    Vec2 _offset;
    Size _originalSize;
};

class SpriteFrameCache {
public:
    static SpriteFrameCache* getInstance();
    // "GUI.plist": read as GUI.frames (the .plist as text, one frame a
    // line, from tools/prepare_data.py), found through FileUtils's search
    // paths.
    void addSpriteFramesWithFile(const std::string& plist);
    SpriteFrame* getSpriteFrameByName(const std::string& name) const;
    void addSpriteFrame(SpriteFrame* frame, const std::string& name);

private:
    std::map<std::string, SpriteFrame*> _frames;
    std::vector<std::string> _loadedFiles;
};

class Sprite : public Node {
public:
    static Sprite* create();
    static Sprite* create(const std::string& filename);
    static Sprite* createWithTexture(Texture2D* texture);
    static Sprite* createWithTexture(Texture2D* texture, const Rect& rect);
    static Sprite* createWithSpriteFrame(SpriteFrame* frame);
    static Sprite* createWithSpriteFrameName(const std::string& name);
    Sprite();
    ~Sprite() override;
    bool init() override;
    bool initWithTexture(Texture2D* texture, const Rect& rect);
    bool initWithSpriteFrame(SpriteFrame* frame);

    void setTexture(Texture2D* texture);
    Texture2D* getTexture() const { return _texture; }
    virtual void setTextureRect(const Rect& rect);
    void setTextureRect(const Rect& rect, bool rotated, const Size& untrimmedSize);
    const Rect& getTextureRect() const { return _rect; }
    virtual void setSpriteFrame(SpriteFrame* frame);
    void setSpriteFrame(const std::string& name);
    void setDisplayFrame(SpriteFrame* frame) { setSpriteFrame(frame); }
    SpriteFrame* getSpriteFrame() const { return _spriteFrame; }
    void setFlippedX(bool f) { _flippedX = f; }
    void setFlippedY(bool f) { _flippedY = f; }
    bool isFlippedX() const { return _flippedX; }
    bool isFlippedY() const { return _flippedY; }
    void setBlendFunc(const BlendFunc& b) { _blend = b; }
    const BlendFunc& getBlendFunc() const { return _blend; }

    void draw(Renderer* renderer, const Mat4& transform, u32 flags) override;

protected:
    Texture2D* _texture = nullptr;
    SpriteFrame* _spriteFrame = nullptr;
    Rect _rect;
    bool _rectRotated = false;
    Vec2 _offsetPosition;
    bool _flippedX = false, _flippedY = false;
    BlendFunc _blend = BlendFunc::ALPHA_PREMULTIPLIED;
};

// A .bcfnt, at the 24 px it was made at, shared by every Label using it.
struct FontAtlas {
    gfx::Font font;
    float lineFeed = 0.0f; // at 24 px
    float ascent = 0.0f;
    float baseline = 0.0f;
    static FontAtlas* get(const std::string& ttfPath);
};

class Label : public Node {
public:
    static Label* createWithTTF(const std::string& text, const std::string& fontFile, float fontSize,
                                const Size& dimensions = Size::ZERO,
                                TextHAlignment hAlign = TextHAlignment::LEFT,
                                TextVAlignment vAlign = TextVAlignment::TOP);
    Label();

    virtual void setString(const std::string& text);
    const std::string& getString() const { return _text; }
    void setAlignment(TextHAlignment h) { setAlignment(h, _vAlign); }
    void setAlignment(TextHAlignment h, TextVAlignment v);
    void setHorizontalAlignment(TextHAlignment h) { setAlignment(h, _vAlign); }
    void setVerticalAlignment(TextVAlignment v) { setAlignment(_hAlign, v); }
    TextHAlignment getHorizontalAlignment() const { return _hAlign; }
    TextVAlignment getVerticalAlignment() const { return _vAlign; }
    void setDimensions(float w, float h);
    void setWidth(float w) { setDimensions(w, _dimensions.height); }
    void setMaxLineWidth(float w);
    void setTextColor(const Color4B& c) { _textColor = c; }
    const Color4B& getTextColor() const { return _textColor; }
    void enableShadow(const Color4B& color = Color4B(0, 0, 0, 128), const Size& offset = Size(2, -2));
    void disableEffect();
    float getLineHeight() const;
    float getFontSize() const { return _fontSize; }
    void setSystemFontSize(float s) { _fontSize = s; _dirty = true; }

    const Size& getContentSize() const override;
    void draw(Renderer* renderer, const Mat4& transform, u32 flags) override;

private:
    struct Glyph {
        int sheet;
        float x0, y0, x1, y1; // label space, y up
        float u0, v0, u1, v1; // texture space
    };
    void layout() const;
    float lineWidth(const std::string& line) const;

    FontAtlas* _atlas = nullptr;
    std::string _text;
    float _fontSize = 12.0f;
    Size _dimensions;
    float _maxLineWidth = 0.0f;
    TextHAlignment _hAlign = TextHAlignment::LEFT;
    TextVAlignment _vAlign = TextVAlignment::TOP;
    Color4B _textColor = Color4B::WHITE;
    bool _shadow = false;
    Color4B _shadowColor;
    Size _shadowOffset;
    mutable bool _dirty = true;
    mutable std::vector<Glyph> _glyphs;
};

class DrawNode : public Node {
public:
    static DrawNode* create(float defaultLineWidth = 2.0f);
    void drawPolygon(const Vec2* verts, int count, const Color4F& fillColor, float borderWidth,
                     const Color4F& borderColor);
    void drawSolidRect(const Vec2& origin, const Vec2& destination, const Color4F& color);
    void drawRect(const Vec2& origin, const Vec2& destination, const Color4F& color);
    void drawSegment(const Vec2& from, const Vec2& to, float radius, const Color4F& color);
    void drawLine(const Vec2& from, const Vec2& to, const Color4F& color);
    void drawDot(const Vec2& pos, float radius, const Color4F& color);
    void drawSolidCircle(const Vec2& center, float radius, float angle, unsigned segments, const Color4F& color);
    void clear();
    // The extent of what has been drawn, in the node's space.
    Rect getDrawnBounds() const;

    void draw(Renderer* renderer, const Mat4& transform, u32 flags) override;

private:
    struct V {
        Vec2 p;
        u32 color;
    };
    void addTriangle(const Vec2& a, const Vec2& b, const Vec2& c, u32 color);
    std::vector<V> _tris;
    float _lineWidth = 2.0f;
};

// The game clips only to rectangles (GUIUtils::createClipper and
// MapSelectScene's, both a DrawNode rectangle, not inverted), so this is a
// scissor to the stencil's on-screen bounds.
class ClippingNode : public Node {
public:
    static ClippingNode* create();
    static ClippingNode* create(Node* stencil);
    ~ClippingNode() override;
    void setStencil(Node* stencil);
    Node* getStencil() const { return _stencil; }
    void setInverted(bool inverted) { _inverted = inverted; }
    bool isInverted() const { return _inverted; }
    void setAlphaThreshold(float t) {}

    void visit(Renderer* renderer, const Mat4& parentTransform, u32 parentFlags) override;
    void onEnter() override;
    void onExit() override;

private:
    Node* _stencil = nullptr;
    bool _inverted = false;
};

namespace utils {
Rect getCascadeBoundingBox(Node* node);
}

} // namespace cocos2d
