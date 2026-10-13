#include "node.h"

#include <cstdio>
#include <cstring>

#include "debuglog.h"
#include "director.h"

namespace cocos2d {

const Vec2 Vec2::ZERO(0.0f, 0.0f);
const Vec2 Vec2::ANCHOR_MIDDLE(0.5f, 0.5f);
const Vec2 Vec2::ANCHOR_BOTTOM_LEFT(0.0f, 0.0f);
const Size Size::ZERO(0.0f, 0.0f);
const Rect Rect::ZERO(0.0f, 0.0f, 0.0f, 0.0f);
const Color3B Color3B::WHITE(255, 255, 255);
const Color3B Color3B::BLACK(0, 0, 0);
const Color4B Color4B::WHITE(255, 255, 255, 255);
const Color4B Color4B::BLACK(0, 0, 0, 255);
const Mat4 Mat4::IDENTITY;
const BlendFunc BlendFunc::ALPHA_PREMULTIPLIED = {GPU_ONE, GPU_ONE_MINUS_SRC_ALPHA};
const BlendFunc BlendFunc::ALPHA_NON_PREMULTIPLIED = {GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA};
const BlendFunc BlendFunc::ADDITIVE = {GPU_SRC_ALPHA, GPU_ONE};

// ------------------------------------------------------------------- Ref

namespace {
std::vector<Ref*> s_pool;
unsigned s_orderOfArrival = 1;

// Nested ClippingNode scissors, in bottom-screen pixels.
struct ScissorRect {
    int x0, y0, x1, y1;
};
std::vector<ScissorRect> s_scissors;

void applyScissor()
{
    if (s_scissors.empty()) {
        gfx::scissorOff();
        return;
    }
    const ScissorRect& r = s_scissors.back();
    gfx::scissor(r.x0, r.y0, r.x1 - r.x0, r.y1 - r.y0);
}

// The colour a node draws its texels with: premultiplied by its opacity.
u32 nodeColor(const Color3B& c, u8 opacity)
{
    return gfx::rgba(c.r * opacity / 255, c.g * opacity / 255, c.b * opacity / 255, opacity);
}

u32 premulColor(const Color4F& c, float alpha)
{
    float a = c.a * alpha;
    auto ch = [](float v) { return (u8)(v <= 0.0f ? 0 : v >= 1.0f ? 255 : v * 255.0f + 0.5f); };
    return gfx::rgba(ch(c.r * a), ch(c.g * a), ch(c.b * a), ch(a));
}
} // namespace

Ref::~Ref() {}

void Ref::release()
{
    if (--_referenceCount == 0)
        delete this;
}

Ref* Ref::autorelease()
{
    s_pool.push_back(this);
    return this;
}

void drainAutoreleasePool()
{
    // Releasing can autorelease more (destructors creating nothing is the
    // rule, but be safe): loop until the pool stays empty.
    while (!s_pool.empty()) {
        std::vector<Ref*> pool;
        pool.swap(s_pool);
        for (Ref* r : pool)
            r->release();
    }
}

// ------------------------------------------------------------------ Node

Node* Node::create()
{
    Node* n = new Node();
    n->init();
    n->autorelease();
    return n;
}

Node::Node() {}

Node::~Node()
{
    Director* d = Director::getInstance();
    d->getEventDispatcher()->removeEventListenersForTarget(this);
    d->getActionManager()->removeAllActionsFromTarget(this);
    d->getScheduler()->unscheduleAllForTarget(this);
    for (Node* c : _children) {
        c->_parent = nullptr;
        c->release();
    }
    if (_userObject)
        _userObject->release();
}

void Node::setUserObject(Ref* o)
{
    if (o)
        o->retain();
    if (_userObject)
        _userObject->release();
    _userObject = o;
}

bool Node::init()
{
    return true;
}

void Node::setLocalZOrder(int z)
{
    if (_parent)
        _parent->reorderChild(this, z);
    else
        _localZOrder = z;
}

void Node::reorderChild(Node* child, int z)
{
    child->_orderOfArrival = s_orderOfArrival++;
    child->_localZOrder = z;
    _reorderDirty = true;
}

void Node::setPosition(const Vec2& p)
{
    _position = p;
    _usingNormalizedPosition = false;
}

void Node::setNormalizedPosition(const Vec2& p)
{
    _normalizedPosition = p;
    _usingNormalizedPosition = true;
}

void Node::setScale(float s)
{
    _scaleX = _scaleY = s;
}

void Node::setScale(float sx, float sy)
{
    _scaleX = sx;
    _scaleY = sy;
}

void Node::setScaleX(float s)
{
    _scaleX = s;
}

void Node::setScaleY(float s)
{
    _scaleY = s;
}

void Node::setRotation(float degrees)
{
    _rotation = degrees;
}

void Node::setAnchorPoint(const Vec2& p)
{
    _anchorPoint = p;
    _anchorPointInPoints = Vec2(_contentSize.width * p.x, _contentSize.height * p.y);
}

void Node::setIgnoreAnchorPointForPosition(bool ignore)
{
    _ignoreAnchorPointForPosition = ignore;
}

void Node::setContentSize(const Size& s)
{
    _contentSize = s;
    _anchorPointInPoints = Vec2(s.width * _anchorPoint.x, s.height * _anchorPoint.y);
}

void Node::setColor(const Color3B& c)
{
    _realColor = c;
    Color3B parentColor = Color3B::WHITE;
    if (_parent && _parent->_cascadeColor)
        parentColor = _parent->_displayedColor;
    updateDisplayedColor(parentColor);
}

void Node::updateDisplayedColor(const Color3B& p)
{
    _displayedColor = Color3B(_realColor.r * p.r / 255, _realColor.g * p.g / 255, _realColor.b * p.b / 255);
    if (_cascadeColor)
        for (Node* c : _children)
            c->updateDisplayedColor(_displayedColor);
}

void Node::setOpacity(u8 o)
{
    _realOpacity = o;
    u8 parentOpacity = 255;
    if (_parent && _parent->_cascadeOpacity)
        parentOpacity = _parent->_displayedOpacity;
    updateDisplayedOpacity(parentOpacity);
}

void Node::updateDisplayedOpacity(u8 parentOpacity)
{
    _displayedOpacity = _realOpacity * parentOpacity / 255;
    if (_cascadeOpacity)
        for (Node* c : _children)
            c->updateDisplayedOpacity(_displayedOpacity);
}

void Node::setCascadeOpacityEnabled(bool on)
{
    _cascadeOpacity = on;
    for (Node* c : _children)
        c->updateDisplayedOpacity(on ? _displayedOpacity : 255);
}

void Node::setCascadeColorEnabled(bool on)
{
    _cascadeColor = on;
    for (Node* c : _children)
        c->updateDisplayedColor(on ? _displayedColor : Color3B::WHITE);
}

void Node::addChild(Node* child)
{
    addChild(child, child->_localZOrder, child->_tag);
}

void Node::addChild(Node* child, int z)
{
    addChild(child, z, child->_tag);
}

void Node::addChild(Node* child, int z, int tag)
{
    if (!child || child->_parent) {
        dbg("cocos: addChild of a null or parented node");
        return;
    }
    child->retain();
    child->_tag = tag;
    insertChild(child, z);
    child->_parent = this;
    if (_cascadeColor)
        child->updateDisplayedColor(_displayedColor);
    if (_cascadeOpacity)
        child->updateDisplayedOpacity(_displayedOpacity);
    if (_running) {
        child->onEnter();
        child->onEnterTransitionDidFinish();
    }
}

void Node::insertChild(Node* child, int z)
{
    _reorderDirty = true;
    child->_localZOrder = z;
    child->_orderOfArrival = s_orderOfArrival++;
    _children.push_back(child);
}

void Node::sortChildren()
{
    if (!_reorderDirty)
        return;
    std::stable_sort(_children.begin(), _children.end(), [](const Node* a, const Node* b) {
        if (a->_localZOrder != b->_localZOrder)
            return a->_localZOrder < b->_localZOrder;
        return a->_orderOfArrival < b->_orderOfArrival;
    });
    _reorderDirty = false;
}

Node* Node::getChildByTag(int tag) const
{
    for (Node* c : _children)
        if (c->_tag == tag)
            return c;
    return nullptr;
}

Node* Node::getChildByName(const std::string& name) const
{
    for (Node* c : _children)
        if (c->_name == name)
            return c;
    return nullptr;
}

void Node::removeFromParentAndCleanup(bool cleanup)
{
    if (_parent)
        _parent->removeChild(this, cleanup);
}

void Node::removeChild(Node* child, bool cleanup)
{
    auto it = std::find(_children.begin(), _children.end(), child);
    if (it == _children.end())
        return;
    if (_running) {
        child->onExitTransitionDidStart();
        child->onExit();
    }
    if (cleanup)
        child->cleanup();
    child->_parent = nullptr;
    _children.erase(it);
    child->release();
}

void Node::removeChildByTag(int tag, bool cleanup)
{
    if (Node* c = getChildByTag(tag))
        removeChild(c, cleanup);
}

void Node::removeAllChildrenWithCleanup(bool cleanup)
{
    std::vector<Node*> children;
    children.swap(_children);
    for (Node* c : children) {
        if (_running) {
            c->onExitTransitionDidStart();
            c->onExit();
        }
        if (cleanup)
            c->cleanup();
        c->_parent = nullptr;
        c->release();
    }
}

void Node::onEnter()
{
    // A child can remove siblings from its onEnter; walk a copy.
    std::vector<Node*> children = _children;
    for (Node* c : children)
        c->retain();
    for (Node* c : children) {
        if (c->_parent == this)
            c->onEnter();
        c->release();
    }
    resume();
    _running = true;
}

void Node::onEnterTransitionDidFinish()
{
    std::vector<Node*> children = _children;
    for (Node* c : children)
        c->onEnterTransitionDidFinish();
}

void Node::onExitTransitionDidStart()
{
    std::vector<Node*> children = _children;
    for (Node* c : children)
        c->onExitTransitionDidStart();
}

void Node::onExit()
{
    pause();
    _running = false;
    std::vector<Node*> children = _children;
    for (Node* c : children)
        c->onExit();
}

void Node::cleanup()
{
    stopAllActions();
    unscheduleAllCallbacks();
    for (Node* c : _children)
        c->cleanup();
}

const Mat4& Node::getNodeToParentTransform()
{
    if (_usingNormalizedPosition && _parent) {
        const Size& ps = _parent->getContentSize();
        _position = Vec2(_normalizedPosition.x * ps.width, _normalizedPosition.y * ps.height);
    }
    float x = _position.x, y = _position.y;
    if (_ignoreAnchorPointForPosition) {
        x += _anchorPointInPoints.x;
        y += _anchorPointInPoints.y;
    }
    // cocos2d turns clockwise for positive degrees.
    float cr = 1.0f, sr = 0.0f;
    if (_rotation != 0.0f) {
        float rad = -_rotation * (float)M_PI / 180.0f;
        cr = cosf(rad);
        sr = sinf(rad);
    }
    Mat4& m = _transform;
    m.a = cr * _scaleX;
    m.b = sr * _scaleX;
    m.c = -sr * _scaleY;
    m.d = cr * _scaleY;
    m.tx = x - (m.a * _anchorPointInPoints.x + m.c * _anchorPointInPoints.y);
    m.ty = y - (m.b * _anchorPointInPoints.x + m.d * _anchorPointInPoints.y);
    return m;
}

Mat4 Node::getNodeToWorldTransform()
{
    Mat4 t = getNodeToParentTransform();
    for (Node* p = _parent; p; p = p->_parent)
        t = p->getNodeToParentTransform() * t;
    return t;
}

Vec2 Node::convertToNodeSpace(const Vec2& worldPoint)
{
    return getWorldToNodeTransform().apply(worldPoint);
}

Vec2 Node::convertToWorldSpace(const Vec2& nodePoint)
{
    return getNodeToWorldTransform().apply(nodePoint);
}

Vec2 Node::convertTouchToNodeSpace(Touch* touch)
{
    return convertToNodeSpace(touch->getLocation());
}

Rect Node::getBoundingBox()
{
    const Mat4& t = getNodeToParentTransform();
    const Size& s = getContentSize();
    Vec2 p[4] = {t.apply(Vec2(0, 0)), t.apply(Vec2(s.width, 0)), t.apply(Vec2(0, s.height)),
                 t.apply(Vec2(s.width, s.height))};
    float x0 = p[0].x, x1 = p[0].x, y0 = p[0].y, y1 = p[0].y;
    for (const Vec2& q : p) {
        x0 = std::min(x0, q.x);
        x1 = std::max(x1, q.x);
        y0 = std::min(y0, q.y);
        y1 = std::max(y1, q.y);
    }
    return Rect(x0, y0, x1 - x0, y1 - y0);
}

void Node::visit(Renderer* renderer, const Mat4& parentTransform, u32 parentFlags)
{
    if (!_visible)
        return;
    _modelViewTransform = parentTransform * getNodeToParentTransform();
    sortChildren();
    size_t i = 0;
    for (; i < _children.size() && _children[i]->_localZOrder < 0; i++)
        _children[i]->visit(renderer, _modelViewTransform, parentFlags);
    draw(renderer, _modelViewTransform, parentFlags);
    for (; i < _children.size(); i++)
        _children[i]->visit(renderer, _modelViewTransform, parentFlags);
}

Action* Node::runAction(Action* action)
{
    getActionManager()->addAction(action, this, !_running);
    return action;
}

void Node::stopAllActions()
{
    getActionManager()->removeAllActionsFromTarget(this);
}

void Node::stopAction(Action* action)
{
    getActionManager()->removeAction(action);
}

void Node::stopActionByTag(int tag)
{
    getActionManager()->removeActionByTag(tag, this);
}

Action* Node::getActionByTag(int tag)
{
    return getActionManager()->getActionByTag(tag, this);
}

int Node::getNumberOfRunningActions()
{
    return getActionManager()->getNumberOfRunningActionsInTarget(this);
}

ActionManager* Node::getActionManager() const
{
    return Director::getInstance()->getActionManager();
}

Scheduler* Node::getScheduler() const
{
    return Director::getInstance()->getScheduler();
}

EventDispatcher* Node::getEventDispatcher() const
{
    return Director::getInstance()->getEventDispatcher();
}

void Node::scheduleUpdate()
{
    scheduleUpdateWithPriority(0);
}

void Node::scheduleUpdateWithPriority(int priority)
{
    getScheduler()->scheduleUpdate(this, priority, !_running);
}

void Node::unscheduleUpdate()
{
    getScheduler()->unscheduleUpdate(this);
}

void Node::schedule(SEL_SCHEDULE selector)
{
    schedule(selector, 0.0f, Scheduler::REPEAT_FOREVER, 0.0f);
}

void Node::schedule(SEL_SCHEDULE selector, float interval)
{
    schedule(selector, interval, Scheduler::REPEAT_FOREVER, 0.0f);
}

void Node::schedule(SEL_SCHEDULE selector, float interval, unsigned repeat, float delay)
{
    getScheduler()->schedule(selector, this, interval, repeat, delay, !_running);
}

void Node::scheduleOnce(SEL_SCHEDULE selector, float delay)
{
    schedule(selector, 0.0f, 0, delay);
}

void Node::schedule(const std::function<void(float)>& fn, float interval, const std::string& key)
{
    getScheduler()->schedule(fn, this, interval, Scheduler::REPEAT_FOREVER, 0.0f, !_running, key);
}

void Node::scheduleOnce(const std::function<void(float)>& fn, float delay, const std::string& key)
{
    getScheduler()->schedule(fn, this, 0.0f, 0, delay, !_running, key);
}

void Node::unschedule(SEL_SCHEDULE selector)
{
    getScheduler()->unschedule(selector, this);
}

void Node::unschedule(const std::string& key)
{
    getScheduler()->unschedule(key, this);
}

void Node::unscheduleAllCallbacks()
{
    getScheduler()->unscheduleAllForTarget(this);
}

void Node::pause()
{
    getScheduler()->pauseTarget(this);
    getActionManager()->pauseTarget(this);
}

void Node::resume()
{
    getScheduler()->resumeTarget(this);
    getActionManager()->resumeTarget(this);
}

// ---------------------------------------------------------- Scene, Layer

Scene* Scene::create()
{
    Scene* s = new Scene();
    s->init();
    s->autorelease();
    return s;
}

Scene::Scene()
{
    _ignoreAnchorPointForPosition = true;
    setAnchorPoint(Vec2(0.5f, 0.5f));
    setContentSize(Director::getInstance()->getWinSize());
}

Layer* Layer::create()
{
    Layer* l = new Layer();
    l->init();
    l->autorelease();
    return l;
}

Layer::Layer()
{
    _ignoreAnchorPointForPosition = true;
    setAnchorPoint(Vec2(0.5f, 0.5f));
}

bool Layer::init()
{
    setContentSize(Director::getInstance()->getWinSize());
    return true;
}

LayerColor* LayerColor::create()
{
    return create(Color4B(0, 0, 0, 0));
}

LayerColor* LayerColor::create(const Color4B& color)
{
    Size s = Director::getInstance()->getWinSize();
    return create(color, s.width, s.height);
}

LayerColor* LayerColor::create(const Color4B& color, float width, float height)
{
    LayerColor* l = new LayerColor();
    l->initWithColor(color, width, height);
    l->autorelease();
    return l;
}

bool LayerColor::initWithColor(const Color4B& color, float width, float height)
{
    Layer::init();
    _realColor = _displayedColor = Color3B(color.r, color.g, color.b);
    _realOpacity = _displayedOpacity = color.a;
    setContentSize(Size(width, height));
    return true;
}

void LayerColor::draw(Renderer*, const Mat4& t, u32)
{
    if (_displayedOpacity == 0)
        return;
    gfx::bindTexture(nullptr);
    gfx::Vertex* v = gfx::triangles(6);
    if (!v)
        return;
    u32 c = nodeColor(_displayedColor, _displayedOpacity);
    Vec2 p0 = t.apply(Vec2(0, 0)), p1 = t.apply(Vec2(_contentSize.width, 0)),
         p2 = t.apply(Vec2(0, _contentSize.height)), p3 = t.apply(Vec2(_contentSize.width, _contentSize.height));
    v[0] = {p0.x, p0.y, 0, 0, c};
    v[1] = {p1.x, p1.y, 0, 0, c};
    v[2] = {p2.x, p2.y, 0, 0, c};
    v[3] = {p1.x, p1.y, 0, 0, c};
    v[4] = {p3.x, p3.y, 0, 0, c};
    v[5] = {p2.x, p2.y, 0, 0, c};
}

// -------------------------------------------------------------- textures

Texture2D::~Texture2D()
{
    gfx::destroyTexture(&_tex);
}

bool Texture2D::initWithFile(const std::string& fullPath)
{
    return gfx::loadTexture(&_tex, fullPath.c_str());
}

bool Texture2D::initWithRGBA(const u8* rgba, int width, int height)
{
    return gfx::createTexture(&_tex, rgba, width, height);
}

Texture2D* TextureCache::addImage(const std::string& path)
{
    auto it = _textures.find(path);
    if (it != _textures.end())
        return it->second;
    std::string full = FileUtils::getInstance()->fullPathForFilename(path);
    Texture2D* t = new Texture2D();
    if (!t->initWithFile(full)) {
        dbg("cocos: no texture for %s (%s)", path.c_str(), full.c_str());
        t->release();
        return nullptr;
    }
    _textures[path] = t;
    return t;
}

void TextureCache::removeTexture(Texture2D* texture)
{
    for (auto it = _textures.begin(); it != _textures.end(); ++it) {
        if (it->second == texture) {
            texture->release();
            _textures.erase(it);
            return;
        }
    }
}

void TextureCache::removeUnusedTextures()
{
    for (auto it = _textures.begin(); it != _textures.end();) {
        if (it->second->getReferenceCount() == 1) {
            it->second->release();
            it = _textures.erase(it);
        } else {
            ++it;
        }
    }
}

Texture2D* TextureCache::getTextureForKey(const std::string& key) const
{
    auto it = _textures.find(key);
    return it == _textures.end() ? nullptr : it->second;
}

// ---------------------------------------------------------- sprite frames

SpriteFrame* SpriteFrame::createWithTexture(Texture2D* texture, const Rect& rect)
{
    return createWithTexture(texture, rect, false, Vec2::ZERO, rect.size);
}

SpriteFrame* SpriteFrame::createWithTexture(Texture2D* texture, const Rect& rect, bool rotated,
                                            const Vec2& offset, const Size& originalSize)
{
    SpriteFrame* f = new SpriteFrame();
    f->_texture = texture;
    CC_SAFE_RETAIN(texture);
    f->_rect = rect;
    f->_rotated = rotated;
    f->_offset = offset;
    f->_originalSize = originalSize;
    f->autorelease();
    return f;
}

SpriteFrame::~SpriteFrame()
{
    CC_SAFE_RELEASE(_texture);
}

SpriteFrameCache* SpriteFrameCache::getInstance()
{
    static SpriteFrameCache s;
    return &s;
}

void SpriteFrameCache::addSpriteFramesWithFile(const std::string& plist)
{
    if (std::find(_loadedFiles.begin(), _loadedFiles.end(), plist) != _loadedFiles.end())
        return;
    std::string full = FileUtils::getInstance()->fullPathForFilename(plist);
    FILE* f = fopen(full.c_str(), "r");
    if (!f) {
        dbg("cocos: no frames file for %s (%s)", plist.c_str(), full.c_str());
        return;
    }
    _loadedFiles.push_back(plist);
    std::string dir = full.substr(0, full.rfind('/') + 1);
    char line[512];
    Texture2D* texture = nullptr;
    int count = 0;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') {
            char texName[256];
            int w, h;
            if (sscanf(line, "# %255s %d %d", texName, &w, &h) == 3) {
                std::string t = texName;
                size_t dot = t.rfind('.');
                if (dot != std::string::npos)
                    t = t.substr(0, dot);
                // Keyed by the card path: the atlas is not a file the
                // game names itself.
                std::string texPath = dir + t + ".t3x";
                TextureCache* cache = Director::getInstance()->getTextureCache();
                texture = cache->addImage(texPath);
            }
            continue;
        }
        char name[256];
        float x, y, w, h, ox, oy, sw, sh;
        int rotated;
        if (sscanf(line, "%255s %f %f %f %f %f %f %f %f %d", name, &x, &y, &w, &h, &ox, &oy, &sw, &sh, &rotated) != 10)
            continue;
        if (!texture)
            continue;
        SpriteFrame* frame =
            SpriteFrame::createWithTexture(texture, Rect(x, y, w, h), rotated != 0, Vec2(ox, oy), Size(sw, sh));
        addSpriteFrame(frame, name);
        count++;
    }
    fclose(f);
    dbg("cocos: %d frames from %s", count, plist.c_str());
}

void SpriteFrameCache::addSpriteFrame(SpriteFrame* frame, const std::string& name)
{
    frame->retain();
    auto it = _frames.find(name);
    if (it != _frames.end())
        it->second->release();
    _frames[name] = frame;
}

SpriteFrame* SpriteFrameCache::getSpriteFrameByName(const std::string& name) const
{
    auto it = _frames.find(name);
    if (it == _frames.end()) {
        dbg("cocos: no sprite frame %s", name.c_str());
        return nullptr;
    }
    return it->second;
}

// ---------------------------------------------------------------- Sprite

Sprite* Sprite::create()
{
    Sprite* s = new Sprite();
    s->init();
    s->autorelease();
    return s;
}

Sprite* Sprite::create(const std::string& filename)
{
    Texture2D* t = Director::getInstance()->getTextureCache()->addImage(filename);
    Sprite* s = new Sprite();
    if (t)
        s->initWithTexture(t, Rect(0, 0, (float)t->getPixelsWide(), (float)t->getPixelsHigh()));
    else
        s->init();
    s->autorelease();
    return s;
}

Sprite* Sprite::createWithTexture(Texture2D* texture)
{
    Sprite* s = new Sprite();
    if (texture)
        s->initWithTexture(texture, Rect(0, 0, (float)texture->getPixelsWide(), (float)texture->getPixelsHigh()));
    else
        s->init();
    s->autorelease();
    return s;
}

Sprite* Sprite::createWithTexture(Texture2D* texture, const Rect& rect)
{
    Sprite* s = new Sprite();
    s->initWithTexture(texture, rect);
    s->autorelease();
    return s;
}

Sprite* Sprite::createWithSpriteFrame(SpriteFrame* frame)
{
    Sprite* s = new Sprite();
    if (frame)
        s->initWithSpriteFrame(frame);
    else
        s->init();
    s->autorelease();
    return s;
}

Sprite* Sprite::createWithSpriteFrameName(const std::string& name)
{
    return createWithSpriteFrame(SpriteFrameCache::getInstance()->getSpriteFrameByName(name));
}

Sprite::Sprite()
{
    setAnchorPoint(Vec2(0.5f, 0.5f));
}

Sprite::~Sprite()
{
    CC_SAFE_RELEASE(_spriteFrame);
    CC_SAFE_RELEASE(_texture);
}

bool Sprite::init()
{
    return initWithTexture(nullptr, Rect::ZERO);
}

bool Sprite::initWithTexture(Texture2D* texture, const Rect& rect)
{
    setTexture(texture);
    setTextureRect(rect);
    return true;
}

bool Sprite::initWithSpriteFrame(SpriteFrame* frame)
{
    setSpriteFrame(frame);
    return true;
}

void Sprite::setTexture(Texture2D* texture)
{
    if (texture == _texture)
        return;
    CC_SAFE_RETAIN(texture);
    CC_SAFE_RELEASE(_texture);
    _texture = texture;
}

void Sprite::setTextureRect(const Rect& rect)
{
    setTextureRect(rect, false, rect.size);
}

void Sprite::setTextureRect(const Rect& rect, bool rotated, const Size& untrimmedSize)
{
    _rect = rect;
    _rectRotated = rotated;
    setContentSize(untrimmedSize);
    _offsetPosition = Vec2((untrimmedSize.width - rect.size.width) * 0.5f, (untrimmedSize.height - rect.size.height) * 0.5f);
}

void Sprite::setSpriteFrame(SpriteFrame* frame)
{
    if (!frame)
        return;
    frame->retain();
    CC_SAFE_RELEASE(_spriteFrame);
    _spriteFrame = frame;
    setTexture(frame->getTexture());
    setTextureRect(frame->getRect(), frame->isRotated(), frame->getOriginalSize());
    _offsetPosition += frame->getOffset();
}

void Sprite::setSpriteFrame(const std::string& name)
{
    setSpriteFrame(SpriteFrameCache::getInstance()->getSpriteFrameByName(name));
}

void Sprite::draw(Renderer*, const Mat4& t, u32)
{
    if (!_texture || _displayedOpacity == 0 || _rect.size.width <= 0.0f)
        return;
    gfx::Texture* tex = _texture->gfxTexture();
    if (!tex->valid)
        return;
    float tw = (float)tex->width, th = (float)tex->height;
    float x0 = _offsetPosition.x, y0 = _offsetPosition.y;
    float x1 = x0 + _rect.size.width, y1 = y0 + _rect.size.height;
    float u0 = _rect.origin.x / tw, u1 = (_rect.origin.x + _rect.size.width) / tw;
    float vTop = _rect.origin.y / th, vBottom = (_rect.origin.y + _rect.size.height) / th;
    if (_flippedX)
        std::swap(u0, u1);
    if (_flippedY)
        std::swap(vTop, vBottom);
    u32 c = nodeColor(_displayedColor, _displayedOpacity);

    bool customBlend = !(_blend == BlendFunc::ALPHA_PREMULTIPLIED);
    if (customBlend)
        gfx::blend((GPU_BLENDFACTOR)_blend.src, (GPU_BLENDFACTOR)_blend.dst);
    gfx::bindTexture(tex);
    gfx::Vertex* v = gfx::triangles(6);
    if (v) {
        Vec2 bl = t.apply(Vec2(x0, y0)), br = t.apply(Vec2(x1, y0)), tl = t.apply(Vec2(x0, y1)),
             tr = t.apply(Vec2(x1, y1));
        v[0] = {bl.x, bl.y, u0, vBottom, c};
        v[1] = {br.x, br.y, u1, vBottom, c};
        v[2] = {tl.x, tl.y, u0, vTop, c};
        v[3] = {br.x, br.y, u1, vBottom, c};
        v[4] = {tr.x, tr.y, u1, vTop, c};
        v[5] = {tl.x, tl.y, u0, vTop, c};
    }
    if (customBlend)
        gfx::blendPremultiplied();
}

// ----------------------------------------------------------------- Label

namespace {
// The pixel size prepare_data.py's fonts are at: mkbcfnt's -s 24 is 24
// points at 96 dpi, so 32 pixels, where a cocos2d font size is FreeType's
// pixels. Measured against FreeType: "Civilians: 471" is 121 pixels at 24
// and 162 at 32, and the .bcfnt's is 162.
const float kFontAtlasPixels = 32.0f;
} // namespace

FontAtlas* FontAtlas::get(const std::string& ttfPath)
{
    static std::map<std::string, FontAtlas*> s_atlases;
    auto it = s_atlases.find(ttfPath);
    if (it != s_atlases.end())
        return it->second;
    FontAtlas* a = new FontAtlas();
    std::string full = FileUtils::getInstance()->fullPathForFilename(ttfPath);
    if (!gfx::loadFont(&a->font, full.c_str())) {
        dbg("cocos: no font %s (%s)", ttfPath.c_str(), full.c_str());
        delete a;
        a = nullptr;
    } else {
        FINF_s* finf = &a->font.cfnt->finf;
        a->lineFeed = finf->lineFeed;
        a->ascent = finf->ascent;
        a->baseline = finf->tglp->baselinePos;
    }
    s_atlases[ttfPath] = a;
    return a;
}

Label* Label::createWithTTF(const std::string& text, const std::string& fontFile, float fontSize,
                            const Size& dimensions, TextHAlignment hAlign, TextVAlignment vAlign)
{
    Label* l = new Label();
    l->_atlas = FontAtlas::get(fontFile);
    l->_fontSize = fontSize;
    l->_dimensions = dimensions;
    l->_hAlign = hAlign;
    l->_vAlign = vAlign;
    l->_text = text;
    l->_dirty = true;
    l->autorelease();
    return l;
}

Label::Label()
{
    setAnchorPoint(Vec2(0.5f, 0.5f));
}

void Label::setString(const std::string& text)
{
    if (text == _text)
        return;
    _text = text;
    _dirty = true;
}

void Label::setAlignment(TextHAlignment h, TextVAlignment v)
{
    if (h != _hAlign || v != _vAlign) {
        _hAlign = h;
        _vAlign = v;
        _dirty = true;
    }
}

void Label::setDimensions(float w, float h)
{
    _dimensions = Size(w, h);
    _dirty = true;
}

void Label::setMaxLineWidth(float w)
{
    _maxLineWidth = w;
    _dirty = true;
}

void Label::enableShadow(const Color4B& color, const Size& offset)
{
    _shadow = true;
    _shadowColor = color;
    _shadowOffset = offset;
}

void Label::disableEffect()
{
    _shadow = false;
}

float Label::getLineHeight() const
{
    return _atlas ? _atlas->lineFeed * _fontSize / kFontAtlasPixels : _fontSize;
}

namespace {
// The next code point of a UTF-8 string.
u32 nextCodePoint(const std::string& s, size_t& i)
{
    u8 c = s[i++];
    if (c < 0x80)
        return c;
    int extra = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
    u32 cp = c & (0x3f >> extra);
    while (extra-- > 0 && i < s.size())
        cp = cp << 6 | (s[i++] & 0x3f);
    return cp;
}
} // namespace

float Label::lineWidth(const std::string& line) const
{
    float scale = _fontSize / kFontAtlasPixels, w = 0.0f;
    CFNT_s* font = _atlas->font.cfnt;
    for (size_t i = 0; i < line.size();) {
        int glyph = fontGlyphIndexFromCodePoint(font, nextCodePoint(line, i));
        charWidthInfo_s* cwi = fontGetCharWidthInfo(font, glyph);
        w += cwi->charWidth * scale;
    }
    return w;
}

void Label::layout() const
{
    _dirty = false;
    _glyphs.clear();
    Size size;
    if (!_atlas) {
        const_cast<Label*>(this)->Node::setContentSize(size);
        return;
    }
    const float scale = _fontSize / kFontAtlasPixels;
    const float lineH = _atlas->lineFeed * scale;
    const float wrap = _dimensions.width > 0.0f ? _dimensions.width : _maxLineWidth;

    // Lines: the text's own breaks, then word wrap.
    std::vector<std::string> lines;
    size_t start = 0;
    while (true) {
        size_t nl = _text.find('\n', start);
        std::string para = _text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        if (wrap > 0.0f && lineWidth(para) > wrap) {
            std::string cur;
            size_t p = 0;
            while (p <= para.size()) {
                size_t sp = para.find(' ', p);
                std::string word = para.substr(p, sp == std::string::npos ? std::string::npos : sp - p);
                std::string attempt = cur.empty() ? word : cur + " " + word;
                if (!cur.empty() && lineWidth(attempt) > wrap) {
                    lines.push_back(cur);
                    cur = word;
                } else {
                    cur = attempt;
                }
                if (sp == std::string::npos)
                    break;
                p = sp + 1;
            }
            lines.push_back(cur);
        } else {
            lines.push_back(para);
        }
        if (nl == std::string::npos)
            break;
        start = nl + 1;
    }

    float textW = 0.0f;
    std::vector<float> widths;
    for (const std::string& l : lines) {
        widths.push_back(lineWidth(l));
        textW = std::max(textW, widths.back());
    }
    float textH = lineH * lines.size();
    size.width = _dimensions.width > 0.0f ? _dimensions.width : textW;
    size.height = _dimensions.height > 0.0f ? _dimensions.height : textH;
    const_cast<Label*>(this)->Node::setContentSize(size);

    float blockTop = size.height;
    if (_vAlign == TextVAlignment::CENTER)
        blockTop = size.height - (size.height - textH) * 0.5f;
    else if (_vAlign == TextVAlignment::BOTTOM)
        blockTop = textH;

    CFNT_s* font = _atlas->font.cfnt;
    const float cellDrop = (_atlas->ascent - _atlas->baseline) * scale;
    for (size_t li = 0; li < lines.size(); li++) {
        float pen = 0.0f;
        if (_hAlign == TextHAlignment::CENTER)
            pen = (size.width - widths[li]) * 0.5f;
        else if (_hAlign == TextHAlignment::RIGHT)
            pen = size.width - widths[li];
        float cellTop = blockTop - li * lineH - cellDrop;
        const std::string& line = lines[li];
        for (size_t i = 0; i < line.size();) {
            u32 cp = nextCodePoint(line, i);
            int glyph = fontGlyphIndexFromCodePoint(font, cp);
            fontGlyphPos_s pos;
            fontCalcGlyphPos(&pos, font, glyph, GLYPH_POS_CALC_VTXCOORD, scale, scale);
            if (cp != ' ' && pos.width > 0.0f) {
                Glyph g;
                g.sheet = pos.sheetIndex;
                g.x0 = pen + pos.vtxcoord.left;
                g.x1 = pen + pos.vtxcoord.right;
                g.y0 = cellTop - pos.vtxcoord.bottom;
                g.y1 = cellTop - pos.vtxcoord.top;
                g.u0 = pos.texcoord.left;
                g.u1 = pos.texcoord.right;
                g.v0 = pos.texcoord.bottom;
                g.v1 = pos.texcoord.top;
                _glyphs.push_back(g);
            }
            pen += pos.xAdvance;
        }
    }
}

const Size& Label::getContentSize() const
{
    if (_dirty)
        layout();
    return _contentSize;
}

void Label::draw(Renderer*, const Mat4& t, u32)
{
    if (_dirty)
        layout();
    if (!_atlas || _glyphs.empty() || _displayedOpacity == 0)
        return;
    auto emit = [&](float dx, float dy, const Color4B& col) {
        u8 a = col.a * _displayedOpacity / 255;
        u32 c = gfx::rgba(col.r * _displayedColor.r / 255 * a / 255, col.g * _displayedColor.g / 255 * a / 255,
                          col.b * _displayedColor.b / 255 * a / 255, a);
        for (const Glyph& g : _glyphs) {
            gfx::bindRawTexture(&_atlas->font.sheets[g.sheet], gfx::Combine::AlphaMask);
            gfx::Vertex* v = gfx::triangles(6);
            if (!v)
                return;
            Vec2 bl = t.apply(Vec2(g.x0 + dx, g.y0 + dy)), br = t.apply(Vec2(g.x1 + dx, g.y0 + dy)),
                 tl = t.apply(Vec2(g.x0 + dx, g.y1 + dy)), tr = t.apply(Vec2(g.x1 + dx, g.y1 + dy));
            v[0] = {bl.x, bl.y, g.u0, g.v0, c};
            v[1] = {br.x, br.y, g.u1, g.v0, c};
            v[2] = {tl.x, tl.y, g.u0, g.v1, c};
            v[3] = {br.x, br.y, g.u1, g.v0, c};
            v[4] = {tr.x, tr.y, g.u1, g.v1, c};
            v[5] = {tl.x, tl.y, g.u0, g.v1, c};
        }
    };
    if (_shadow)
        emit(_shadowOffset.width, _shadowOffset.height, _shadowColor);
    emit(0.0f, 0.0f, _textColor);
}

// -------------------------------------------------------------- DrawNode

DrawNode* DrawNode::create(float defaultLineWidth)
{
    DrawNode* d = new DrawNode();
    d->_lineWidth = defaultLineWidth;
    d->init();
    d->autorelease();
    return d;
}

void DrawNode::addTriangle(const Vec2& a, const Vec2& b, const Vec2& c, u32 color)
{
    _tris.push_back({a, color});
    _tris.push_back({b, color});
    _tris.push_back({c, color});
}

void DrawNode::drawPolygon(const Vec2* verts, int count, const Color4F& fill, float borderWidth, const Color4F& border)
{
    if (count < 3)
        return;
    u32 c = premulColor(fill, 1.0f);
    if (fill.a > 0.0f)
        for (int i = 1; i + 1 < count; i++)
            addTriangle(verts[0], verts[i], verts[i + 1], c);
    if (borderWidth > 0.0f && border.a > 0.0f)
        for (int i = 0; i < count; i++)
            drawSegment(verts[i], verts[(i + 1) % count], borderWidth, border);
}

void DrawNode::drawSolidRect(const Vec2& o, const Vec2& d, const Color4F& color)
{
    Vec2 v[4] = {o, Vec2(d.x, o.y), d, Vec2(o.x, d.y)};
    drawPolygon(v, 4, color, 0.0f, color);
}

void DrawNode::drawRect(const Vec2& o, const Vec2& d, const Color4F& color)
{
    drawLine(o, Vec2(d.x, o.y), color);
    drawLine(Vec2(d.x, o.y), d, color);
    drawLine(d, Vec2(o.x, d.y), color);
    drawLine(Vec2(o.x, d.y), o, color);
}

void DrawNode::drawSegment(const Vec2& from, const Vec2& to, float radius, const Color4F& color)
{
    Vec2 dir = (to - from).getNormalized();
    Vec2 n(-dir.y * radius, dir.x * radius);
    u32 c = premulColor(color, 1.0f);
    addTriangle(from - n, to - n, to + n, c);
    addTriangle(from - n, to + n, from + n, c);
}

void DrawNode::drawLine(const Vec2& from, const Vec2& to, const Color4F& color)
{
    drawSegment(from, to, _lineWidth * 0.5f, color);
}

void DrawNode::drawDot(const Vec2& pos, float radius, const Color4F& color)
{
    drawSolidCircle(pos, radius, 0.0f, 16, color);
}

void DrawNode::drawSolidCircle(const Vec2& center, float radius, float angle, unsigned segments, const Color4F& color)
{
    u32 c = premulColor(color, 1.0f);
    for (unsigned i = 0; i < segments; i++) {
        float a0 = angle + 2.0f * (float)M_PI * i / segments, a1 = angle + 2.0f * (float)M_PI * (i + 1) / segments;
        addTriangle(center, center + Vec2(cosf(a0), sinf(a0)) * radius, center + Vec2(cosf(a1), sinf(a1)) * radius, c);
    }
}

void DrawNode::clear()
{
    _tris.clear();
}

Rect DrawNode::getDrawnBounds() const
{
    if (_tris.empty())
        return Rect::ZERO;
    float x0 = _tris[0].p.x, x1 = x0, y0 = _tris[0].p.y, y1 = y0;
    for (const V& v : _tris) {
        x0 = std::min(x0, v.p.x);
        x1 = std::max(x1, v.p.x);
        y0 = std::min(y0, v.p.y);
        y1 = std::max(y1, v.p.y);
    }
    return Rect(x0, y0, x1 - x0, y1 - y0);
}

void DrawNode::draw(Renderer*, const Mat4& t, u32)
{
    if (_tris.empty() || _displayedOpacity == 0)
        return;
    gfx::bindTexture(nullptr);
    gfx::Vertex* v = gfx::triangles((int)_tris.size());
    if (!v)
        return;
    const float alpha = _displayedOpacity / 255.0f;
    for (const V& src : _tris) {
        Vec2 p = t.apply(src.p);
        u32 c = src.color;
        if (_displayedOpacity != 255) {
            u8 r = (c & 0xff) * alpha, g = ((c >> 8) & 0xff) * alpha, b = ((c >> 16) & 0xff) * alpha,
               a = (c >> 24) * alpha;
            c = gfx::rgba(r, g, b, a);
        }
        *v++ = {p.x, p.y, 0.0f, 0.0f, c};
    }
}

// ---------------------------------------------------------- ClippingNode

ClippingNode* ClippingNode::create()
{
    return create(nullptr);
}

ClippingNode* ClippingNode::create(Node* stencil)
{
    ClippingNode* c = new ClippingNode();
    c->init();
    c->setStencil(stencil);
    c->autorelease();
    return c;
}

ClippingNode::~ClippingNode()
{
    CC_SAFE_RELEASE(_stencil);
}

void ClippingNode::setStencil(Node* stencil)
{
    if (stencil == _stencil)
        return;
    if (_stencil && _running) {
        _stencil->onExitTransitionDidStart();
        _stencil->onExit();
    }
    CC_SAFE_RETAIN(stencil);
    CC_SAFE_RELEASE(_stencil);
    _stencil = stencil;
    if (_stencil)
        _stencil->setParent(nullptr);
    if (_stencil && _running) {
        _stencil->onEnter();
        _stencil->onEnterTransitionDidFinish();
    }
}

void ClippingNode::onEnter()
{
    Node::onEnter();
    if (_stencil)
        _stencil->onEnter();
}

void ClippingNode::onExit()
{
    if (_stencil)
        _stencil->onExit();
    Node::onExit();
}

void ClippingNode::visit(Renderer* renderer, const Mat4& parentTransform, u32 parentFlags)
{
    if (!_visible)
        return;
    if (!_stencil) {
        Node::visit(renderer, parentTransform, parentFlags);
        return;
    }
    if (_inverted)
        dbg("cocos: inverted ClippingNode drawn uninverted");
    Mat4 self = parentTransform * getNodeToParentTransform();
    Mat4 st = self * _stencil->getNodeToParentTransform();
    Rect r;
    if (DrawNode* dn = dynamic_cast<DrawNode*>(_stencil))
        r = dn->getDrawnBounds();
    else
        r = Rect(Vec2::ZERO, _stencil->getContentSize());
    Vec2 corners[4] = {st.apply(Vec2(r.getMinX(), r.getMinY())), st.apply(Vec2(r.getMaxX(), r.getMinY())),
                       st.apply(Vec2(r.getMinX(), r.getMaxY())), st.apply(Vec2(r.getMaxX(), r.getMaxY()))};
    float sx0 = 1e9f, sy0 = 1e9f, sx1 = -1e9f, sy1 = -1e9f;
    Director* d = Director::getInstance();
    for (const Vec2& c : corners) {
        float sx, sy;
        d->canvasToScreen(c, &sx, &sy);
        sx0 = std::min(sx0, sx);
        sx1 = std::max(sx1, sx);
        sy0 = std::min(sy0, sy);
        sy1 = std::max(sy1, sy);
    }
    ScissorRect s = {(int)floorf(sx0 + 0.5f), (int)floorf(sy0 + 0.5f), (int)floorf(sx1 + 0.5f), (int)floorf(sy1 + 0.5f)};
    if (!s_scissors.empty()) {
        const ScissorRect& o = s_scissors.back();
        s.x0 = std::max(s.x0, o.x0);
        s.y0 = std::max(s.y0, o.y0);
        s.x1 = std::min(s.x1, o.x1);
        s.y1 = std::min(s.y1, o.y1);
    }
    s_scissors.push_back(s);
    applyScissor();
    Node::visit(renderer, parentTransform, parentFlags);
    s_scissors.pop_back();
    applyScissor();
}

namespace utils {
Rect getCascadeBoundingBox(Node* node)
{
    Rect cbb;
    bool merged = false;
    for (Node* child : node->getChildren()) {
        if (!child->isVisible())
            continue;
        Rect box = getCascadeBoundingBox(child);
        if (box.size.width <= 0 || box.size.height <= 0)
            continue;
        cbb = merged ? cbb.unionWithRect(box) : box;
        merged = true;
    }
    const Size& cs = node->getContentSize();
    if (cs.width > 0 && cs.height > 0) {
        Mat4 t = node->getNodeToWorldTransform();
        Vec2 p[4] = {t.apply(Vec2(0, 0)), t.apply(Vec2(cs.width, 0)), t.apply(Vec2(0, cs.height)),
                     t.apply(Vec2(cs.width, cs.height))};
        float x0 = p[0].x, x1 = p[0].x, y0 = p[0].y, y1 = p[0].y;
        for (const Vec2& q : p) {
            x0 = std::min(x0, q.x);
            x1 = std::max(x1, q.x);
            y0 = std::min(y0, q.y);
            y1 = std::max(y1, q.y);
        }
        Rect box(x0, y0, x1 - x0, y1 - y0);
        cbb = merged ? cbb.unionWithRect(box) : box;
    }
    return cbb;
}
} // namespace utils

} // namespace cocos2d
