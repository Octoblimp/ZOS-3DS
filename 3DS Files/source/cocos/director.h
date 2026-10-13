// The cocos2d-x runtime the game drives: Director (scenes and the frame),
// Scheduler, ActionManager and the actions the game runs, EventDispatcher
// with touch listeners, scene transitions, and FileUtils.
//
// The bottom screen is the game's window. Its scenes are laid out on a
// 480x360 canvas (kDesignWidth x kDesignHeight) drawn at 2/3 onto the
// 320x240 screen: the game sizes its GUI in MDPI pixels (dpToSize 1) for
// phones about 480 wide, and at 1:1 its 140x96 buttons would not fit.
#pragma once

#include <functional>
#include <set>
#include <string>
#include <vector>

#include "node.h"

namespace cocos2d {

const float kDesignWidth = 480.0f;
const float kDesignHeight = 360.0f;

// ---------------------------------------------------------------- actions

class Action : public Ref {
public:
    static const int INVALID_TAG = -1;
    virtual void startWithTarget(Node* target) { _target = target; _originalTarget = target; }
    virtual void stop() { _target = nullptr; }
    virtual bool isDone() const { return true; }
    virtual void step(float dt) {}
    virtual void update(float t) {}
    Node* getTarget() const { return _target; }
    Node* getOriginalTarget() const { return _originalTarget; }
    int getTag() const { return _tag; }
    void setTag(int t) { _tag = t; }

protected:
    Node* _target = nullptr;
    Node* _originalTarget = nullptr;
    int _tag = INVALID_TAG;
};

class FiniteTimeAction : public Action {
public:
    float getDuration() const { return _duration; }
    void setDuration(float d) { _duration = d; }

protected:
    float _duration = 0.0f;
};

class ActionInterval : public FiniteTimeAction {
public:
    void startWithTarget(Node* target) override;
    bool isDone() const override { return _elapsed >= _duration; }
    void step(float dt) override;
    float getElapsed() const { return _elapsed; }

protected:
    void initWithDuration(float d);
    float _elapsed = 0.0f;
    bool _firstTick = true;
};

class ActionInstant : public FiniteTimeAction {
public:
    bool isDone() const override { return _done; }
    void step(float dt) override { update(1.0f); _done = true; }
    void startWithTarget(Node* target) override { FiniteTimeAction::startWithTarget(target); _done = false; }

protected:
    bool _done = false;
};

class MoveTo : public ActionInterval {
public:
    static MoveTo* create(float duration, const Vec2& position);
    void startWithTarget(Node* target) override;
    void update(float t) override;

private:
    Vec2 _start, _end;
};

class MoveBy : public ActionInterval {
public:
    static MoveBy* create(float duration, const Vec2& delta);
    void startWithTarget(Node* target) override;
    void update(float t) override;

private:
    Vec2 _start, _delta;
};

class ScaleTo : public ActionInterval {
public:
    static ScaleTo* create(float duration, float s);
    static ScaleTo* create(float duration, float sx, float sy);
    void startWithTarget(Node* target) override;
    void update(float t) override;

private:
    float _startX = 1, _startY = 1, _endX = 1, _endY = 1;
};

class FadeTo : public ActionInterval {
public:
    static FadeTo* create(float duration, u8 opacity);
    void startWithTarget(Node* target) override;
    void update(float t) override;

protected:
    u8 _from = 0, _to = 0;
};

class FadeIn : public FadeTo {
public:
    static FadeIn* create(float duration);
    void startWithTarget(Node* target) override;
};

class FadeOut : public FadeTo {
public:
    static FadeOut* create(float duration);
    void startWithTarget(Node* target) override;
};

class DelayTime : public ActionInterval {
public:
    static DelayTime* create(float duration);
};

class Sequence : public ActionInterval {
public:
    static Sequence* create(FiniteTimeAction* a, ...) __attribute__((sentinel));
    static Sequence* create(const std::vector<FiniteTimeAction*>& actions);
    ~Sequence() override;
    void startWithTarget(Node* target) override;
    void stop() override;
    void update(float t) override;

private:
    std::vector<FiniteTimeAction*> _actions;
    std::vector<float> _ends;
    int _current = -1;
};

class Spawn : public ActionInterval {
public:
    static Spawn* create(FiniteTimeAction* a, ...) __attribute__((sentinel));
    ~Spawn() override;
    void startWithTarget(Node* target) override;
    void stop() override;
    void update(float t) override;

private:
    std::vector<FiniteTimeAction*> _actions;
};

class RepeatForever : public ActionInterval {
public:
    static RepeatForever* create(ActionInterval* inner);
    ~RepeatForever() override;
    void startWithTarget(Node* target) override;
    void step(float dt) override;
    bool isDone() const override { return false; }

private:
    ActionInterval* _inner = nullptr;
};

class EaseRateAction : public ActionInterval {
public:
    ~EaseRateAction() override;
    void startWithTarget(Node* target) override;
    void stop() override;

protected:
    void initWithAction(ActionInterval* inner, float rate);
    ActionInterval* _inner = nullptr;
    float _rate = 1.0f;
};

class EaseIn : public EaseRateAction {
public:
    static EaseIn* create(ActionInterval* inner, float rate);
    void update(float t) override;
};

class EaseOut : public EaseRateAction {
public:
    static EaseOut* create(ActionInterval* inner, float rate);
    void update(float t) override;
};

class EaseInOut : public EaseRateAction {
public:
    static EaseInOut* create(ActionInterval* inner, float rate);
    void update(float t) override;
};

class CallFunc : public ActionInstant {
public:
    static CallFunc* create(const std::function<void()>& fn);
    void update(float t) override;

private:
    std::function<void()> _fn;
};

class RemoveSelf : public ActionInstant {
public:
    static RemoveSelf* create(bool cleanup = true);
    void update(float t) override;

private:
    bool _cleanup = true;
};

class Show : public ActionInstant {
public:
    static Show* create();
    void update(float t) override;
};

class Hide : public ActionInstant {
public:
    static Hide* create();
    void update(float t) override;
};

class ActionManager {
public:
    void addAction(Action* action, Node* target, bool paused);
    void removeAllActionsFromTarget(Node* target);
    void removeAction(Action* action);
    void removeActionByTag(int tag, Node* target);
    Action* getActionByTag(int tag, Node* target) const;
    int getNumberOfRunningActionsInTarget(Node* target) const;
    void pauseTarget(Node* target);
    void resumeTarget(Node* target);
    void update(float dt);

private:
    struct Entry {
        Action* action;
        Node* target;
        bool paused;
        bool removed;
    };
    std::vector<Entry> _entries;
    bool _updating = false;
};

// -------------------------------------------------------------- scheduler

class Scheduler {
public:
    static const unsigned REPEAT_FOREVER = 0xfffffffe;

    void scheduleUpdate(Node* target, int priority, bool paused);
    void unscheduleUpdate(Node* target);
    void schedule(SEL_SCHEDULE selector, Ref* target, float interval, unsigned repeat, float delay, bool paused);
    void schedule(const std::function<void(float)>& fn, void* target, float interval, unsigned repeat,
                  float delay, bool paused, const std::string& key);
    void unschedule(SEL_SCHEDULE selector, Ref* target);
    void unschedule(const std::string& key, void* target);
    void unscheduleAllForTarget(void* target);
    void pauseTarget(void* target);
    void resumeTarget(void* target);
    void update(float dt);

    // The engine's own per-frame work (ActionManager), run first.
    void setSystemUpdate(const std::function<void(float)>& fn) { _system = fn; }

private:
    struct Timer {
        void* target;
        SEL_SCHEDULE selector;
        std::function<void(float)> fn;
        std::string key;
        float interval;
        float elapsed;
        unsigned repeat;
        unsigned timesExecuted;
        float delay;
        bool useDelay;
        bool paused;
        bool dead;
    };
    struct Update {
        Node* target;
        int priority;
        bool paused;
        bool dead;
    };
    std::vector<Timer> _timers;
    std::vector<Update> _updates;
    std::function<void(float)> _system;
};

// ----------------------------------------------------------------- events

class Touch : public Ref {
public:
    Touch() { _referenceCount = 1; }
    int getID() const { return _id; }
    // In the canvas's coordinates, y up, as cocos2d's GL view has them.
    Vec2 getLocation() const { return _point; }
    Vec2 getPreviousLocation() const { return _prevPoint; }
    Vec2 getStartLocation() const { return _startPoint; }
    Vec2 getDelta() const { return _point - _prevPoint; }
    Vec2 getLocationInView() const { return Vec2(_point.x, kDesignHeight - _point.y); }

    void setTouchInfo(int id, const Vec2& p, bool start);

private:
    int _id = 0;
    Vec2 _point, _prevPoint, _startPoint;
};

class Event {
public:
    void stopPropagation() { _stopped = true; }
    bool isStopped() const { return _stopped; }
    bool _stopped = false;
};
class EventTouch : public Event {};

class EventListener : public Ref {
public:
    enum class Type { TOUCH_ONE_BY_ONE, TOUCH_ALL_AT_ONCE };
    Type getType() const { return _type; }
    void setEnabled(bool e) { _enabled = e; }
    bool isEnabled() const { return _enabled; }
    Node* getAssociatedNode() const { return _node; }

    Type _type = Type::TOUCH_ONE_BY_ONE;
    bool _enabled = true;
    Node* _node = nullptr;
    int _fixedPriority = 0;
    bool _registered = false;
    unsigned _orderAdded = 0;
};

class EventListenerTouchOneByOne : public EventListener {
public:
    static EventListenerTouchOneByOne* create();
    void setSwallowTouches(bool s) { _swallow = s; }
    bool isSwallowTouches() const { return _swallow; }

    std::function<bool(Touch*, Event*)> onTouchBegan;
    std::function<void(Touch*, Event*)> onTouchMoved;
    std::function<void(Touch*, Event*)> onTouchEnded;
    std::function<void(Touch*, Event*)> onTouchCancelled;

    bool _swallow = false;
    std::vector<Touch*> _claimed;
};

class EventListenerTouchAllAtOnce : public EventListener {
public:
    static EventListenerTouchAllAtOnce* create();
    std::function<void(const std::vector<Touch*>&, Event*)> onTouchesBegan;
    std::function<void(const std::vector<Touch*>&, Event*)> onTouchesMoved;
    std::function<void(const std::vector<Touch*>&, Event*)> onTouchesEnded;
    std::function<void(const std::vector<Touch*>&, Event*)> onTouchesCancelled;
};

class EventDispatcher {
public:
    void addEventListenerWithSceneGraphPriority(EventListener* listener, Node* node);
    void addEventListenerWithFixedPriority(EventListener* listener, int priority);
    void removeEventListener(EventListener* listener);
    void removeEventListenersForTarget(Node* target, bool recursive = false);
    void removeAllEventListeners();
    void setEnabled(bool e) { _enabled = e; }
    bool isEnabled() const { return _enabled; }

    enum class TouchPhase { BEGAN, MOVED, ENDED, CANCELLED };
    void dispatchTouch(TouchPhase phase, const std::vector<Touch*>& touches);

private:
    void sortedListeners(std::vector<EventListener*>& out, EventListener::Type type);
    std::vector<EventListener*> _listeners;
    bool _enabled = true;
    int _dispatching = 0;
    unsigned _added = 0;
};

// ---------------------------------------------------------------- scenes

class TransitionScene : public Scene {
public:
    ~TransitionScene() override;
    bool initWithDuration(float t, Scene* scene);
    void finish();
    void hideOutShowIn();
    void onEnter() override;
    void onExit() override;
    void cleanup() override;
    void visit(Renderer* renderer, const Mat4& parentTransform, u32 parentFlags) override;
    Scene* getInScene() const { return _inScene; }
    float getDuration() const { return _duration; }

protected:
    void setNewScene(float dt);
    Scene* _inScene = nullptr;
    Scene* _outScene = nullptr;
    float _duration = 0.0f;
    bool _isInSceneOnTop = true;
    bool _isSendCleanupToScene = false;
};

// The incoming scene slides in from the left, eased out (rate 2), as
// cocos2d's TransitionMoveInL.
class TransitionMoveInL : public TransitionScene {
public:
    static TransitionMoveInL* create(float t, Scene* scene);
    void onEnter() override;

protected:
    virtual void initScenes();
    virtual Vec2 startPosition() const;
};

class TransitionMoveInB : public TransitionMoveInL {
public:
    static TransitionMoveInB* create(float t, Scene* scene);

protected:
    Vec2 startPosition() const override;
};

class TransitionMoveInR : public TransitionMoveInL {
public:
    static TransitionMoveInR* create(float t, Scene* scene);

protected:
    Vec2 startPosition() const override;
};

class TransitionMoveInT : public TransitionMoveInL {
public:
    static TransitionMoveInT* create(float t, Scene* scene);

protected:
    Vec2 startPosition() const override;
};

// --------------------------------------------------------------- director

class Director {
public:
    static Director* getInstance();

    Size getVisibleSize() const { return Size(kDesignWidth, kDesignHeight); }
    Vec2 getVisibleOrigin() const { return Vec2::ZERO; }
    Size getWinSize() const { return getVisibleSize(); }
    Size getWinSizeInPixels() const { return getVisibleSize(); }
    float getContentScaleFactor() const { return 1.0f; }

    Scene* getRunningScene() const { return _runningScene; }
    void runWithScene(Scene* scene);
    void replaceScene(Scene* scene);
    bool isSendCleanupToScene() const { return _sendCleanupToScene; }

    Scheduler* getScheduler() { return &_scheduler; }
    ActionManager* getActionManager() { return &_actionManager; }
    EventDispatcher* getEventDispatcher() { return &_eventDispatcher; }
    TextureCache* getTextureCache() { return &_textureCache; }
    float getDeltaTime() const { return _deltaTime; }
    unsigned getTotalFrames() const { return _totalFrames; }
    void setClearColor(const Color4F& c) { _clearColor = c; }
    void setAnimationInterval(float) {}

    // What draws on the top screen, in its own 400x240 pixels; the port's
    // addition (the game had one screen).
    void setTopScreenNode(Node* node);
    Node* getTopScreenNode() const { return _topNode; }

    // One frame: input, scheduler and actions, both screens. False once the
    // app should quit.
    bool mainLoop();

    // Where the frames went since the last call, for the log (the port's):
    // each phase's average in ms. wait is C3D_FrameBegin's, for the screens'
    // refresh and the GPU's last frame; gpu is citro3d's measure of the GPU
    // drawing a frame, which an emulator does not time.
    struct FrameProfile {
        int frames;
        float input, update, top, bottom, submit, wait, gpu;
        // The longest frame, start to start, and the average.
        float worst, average;
    };
    FrameProfile takeFrameProfile();
    void end() { _quit = true; }

    // The canvas -> bottom screen pixels (from the top left), for scissors.
    void canvasToScreen(const Vec2& p, float* sx, float* sy) const;

    // Buttons and circle pad, this frame (hidKeysDown/Held/Up), for the
    // game's added controls. Not keysDown() etc.: libctru's hid.h #defines
    // keysDown/keysHeld/keysUp/touchRead/scanKeys/circleRead to its hid*
    // functions, and a member so named shadows them, so mainLoop's
    // "_keysDown = hidKeysDown()" assigned the field to itself.
    u32 buttonsDown() const { return _keysDown; }
    u32 buttonsHeld() const { return _keysHeld; }
    u32 buttonsUp() const { return _keysUp; }

private:
    Director();
    void setNextScene();
    void handleTouch();

    Scene* _runningScene = nullptr;
    Scene* _nextScene = nullptr;
    bool _sendCleanupToScene = false;
    Node* _topNode = nullptr;
    Scheduler _scheduler;
    ActionManager _actionManager;
    EventDispatcher _eventDispatcher;
    TextureCache _textureCache;
    Color4F _clearColor = Color4F(0, 0, 0, 1);
    float _deltaTime = 0.0f;
    u64 _lastTick = 0;
    unsigned _totalFrames = 0;
    // takeFrameProfile's sums, in ticks (gpu in ms).
    enum { kPhaseInput, kPhaseUpdate, kPhaseWait, kPhaseTop, kPhaseBottom, kPhaseSubmit, kPhases };
    u64 _phaseTicks[kPhases] = {};
    u64 _frameTicks = 0, _worstTicks = 0;
    float _gpuMs = 0.0f;
    int _profiledFrames = 0;
    bool _quit = false;

    Touch* _touch = nullptr;
    bool _touching = false;
    u32 _keysDown = 0, _keysHeld = 0, _keysUp = 0;
};

// -------------------------------------------------------------- FileUtils

class FileUtils {
public:
    static FileUtils* getInstance();
    void addSearchPath(const std::string& path, bool front = false);
    // A file the game names, as it named it in the APK's assets/, to its
    // path on the card. Images become .t3x; the rest keep their names.
    std::string fullPathForFilename(const std::string& filename) const;
    bool isFileExist(const std::string& path) const;
    std::string getStringFromFile(const std::string& filename) const;
    std::vector<u8> getDataFromFile(const std::string& filename) const;
    // sdmc:/ZombieOutbreakSim/save/, made if missing.
    std::string getWritablePath() const;

private:
    std::vector<std::string> _searchPaths;
};

} // namespace cocos2d
