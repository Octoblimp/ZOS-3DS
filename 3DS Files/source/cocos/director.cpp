#include "director.h"

#include <cstdarg>
#include <cstdio>
#include <map>
#include <sys/stat.h>

#include <3ds.h>

#include "debuglog.h"
#include "paths.h"

namespace cocos2d {

// ---------------------------------------------------------------- actions

void ActionInterval::initWithDuration(float d)
{
    _duration = d;
    _elapsed = 0.0f;
    _firstTick = true;
}

void ActionInterval::startWithTarget(Node* target)
{
    FiniteTimeAction::startWithTarget(target);
    _elapsed = 0.0f;
    _firstTick = true;
}

void ActionInterval::step(float dt)
{
    if (_firstTick) {
        _firstTick = false;
        _elapsed = 0.0f;
    } else {
        _elapsed += dt;
    }
    float t = _duration > 1e-6f ? _elapsed / _duration : 1.0f;
    update(std::max(0.0f, std::min(1.0f, t)));
}

MoveTo* MoveTo::create(float duration, const Vec2& position)
{
    MoveTo* a = new MoveTo();
    a->initWithDuration(duration);
    a->_end = position;
    a->autorelease();
    return a;
}

void MoveTo::startWithTarget(Node* target)
{
    ActionInterval::startWithTarget(target);
    _start = target->getPosition();
}

void MoveTo::update(float t)
{
    if (_target)
        _target->setPosition(_start + (_end - _start) * t);
}

MoveBy* MoveBy::create(float duration, const Vec2& delta)
{
    MoveBy* a = new MoveBy();
    a->initWithDuration(duration);
    a->_delta = delta;
    a->autorelease();
    return a;
}

void MoveBy::startWithTarget(Node* target)
{
    ActionInterval::startWithTarget(target);
    _start = target->getPosition();
}

void MoveBy::update(float t)
{
    if (_target)
        _target->setPosition(_start + _delta * t);
}

ScaleTo* ScaleTo::create(float duration, float s)
{
    return create(duration, s, s);
}

ScaleTo* ScaleTo::create(float duration, float sx, float sy)
{
    ScaleTo* a = new ScaleTo();
    a->initWithDuration(duration);
    a->_endX = sx;
    a->_endY = sy;
    a->autorelease();
    return a;
}

void ScaleTo::startWithTarget(Node* target)
{
    ActionInterval::startWithTarget(target);
    _startX = target->getScaleX();
    _startY = target->getScaleY();
}

void ScaleTo::update(float t)
{
    if (_target)
        _target->setScale(_startX + (_endX - _startX) * t, _startY + (_endY - _startY) * t);
}

FadeTo* FadeTo::create(float duration, u8 opacity)
{
    FadeTo* a = new FadeTo();
    a->initWithDuration(duration);
    a->_to = opacity;
    a->autorelease();
    return a;
}

void FadeTo::startWithTarget(Node* target)
{
    ActionInterval::startWithTarget(target);
    _from = target->getOpacity();
}

void FadeTo::update(float t)
{
    if (_target)
        _target->setOpacity((u8)(_from + (_to - _from) * t));
}

FadeIn* FadeIn::create(float duration)
{
    FadeIn* a = new FadeIn();
    a->initWithDuration(duration);
    a->_to = 255;
    a->autorelease();
    return a;
}

void FadeIn::startWithTarget(Node* target)
{
    ActionInterval::startWithTarget(target);
    _from = target->getOpacity();
    _to = 255;
}

FadeOut* FadeOut::create(float duration)
{
    FadeOut* a = new FadeOut();
    a->initWithDuration(duration);
    a->_to = 0;
    a->autorelease();
    return a;
}

void FadeOut::startWithTarget(Node* target)
{
    ActionInterval::startWithTarget(target);
    _from = target->getOpacity();
    _to = 0;
}

DelayTime* DelayTime::create(float duration)
{
    DelayTime* a = new DelayTime();
    a->initWithDuration(duration);
    a->autorelease();
    return a;
}

Sequence* Sequence::create(FiniteTimeAction* first, ...)
{
    std::vector<FiniteTimeAction*> actions;
    va_list ap;
    va_start(ap, first);
    for (FiniteTimeAction* a = first; a; a = va_arg(ap, FiniteTimeAction*))
        actions.push_back(a);
    va_end(ap);
    return create(actions);
}

Sequence* Sequence::create(const std::vector<FiniteTimeAction*>& actions)
{
    Sequence* s = new Sequence();
    float total = 0.0f;
    for (FiniteTimeAction* a : actions) {
        a->retain();
        s->_actions.push_back(a);
        total += a->getDuration();
        s->_ends.push_back(total);
    }
    s->initWithDuration(total);
    s->autorelease();
    return s;
}

Sequence::~Sequence()
{
    for (FiniteTimeAction* a : _actions)
        a->release();
}

void Sequence::startWithTarget(Node* target)
{
    ActionInterval::startWithTarget(target);
    _current = -1;
}

void Sequence::stop()
{
    if (_current >= 0 && _current < (int)_actions.size())
        _actions[_current]->stop();
    ActionInterval::stop();
}

void Sequence::update(float t)
{
    if (_actions.empty())
        return;
    const float now = t * _duration;
    const int last = (int)_actions.size() - 1;
    Node* target = _target;
    if (_current < 0) {
        _current = 0;
        _actions[0]->startWithTarget(target);
    }
    while (true) {
        FiniteTimeAction* a = _actions[_current];
        float begin = _current == 0 ? 0.0f : _ends[_current - 1];
        float end = _ends[_current];
        if (_current < last && (now >= end || t >= 1.0f)) {
            a->update(1.0f);
            a->stop();
            if (!_target) // a callback stopped the sequence
                return;
            _current++;
            _actions[_current]->startWithTarget(target);
            continue;
        }
        float d = end - begin;
        float local = d > 1e-6f ? (now - begin) / d : 1.0f;
        a->update(std::max(0.0f, std::min(1.0f, local)));
        break;
    }
}

Spawn* Spawn::create(FiniteTimeAction* first, ...)
{
    Spawn* s = new Spawn();
    float total = 0.0f;
    va_list ap;
    va_start(ap, first);
    for (FiniteTimeAction* a = first; a; a = va_arg(ap, FiniteTimeAction*)) {
        a->retain();
        s->_actions.push_back(a);
        total = std::max(total, a->getDuration());
    }
    va_end(ap);
    s->initWithDuration(total);
    s->autorelease();
    return s;
}

Spawn::~Spawn()
{
    for (FiniteTimeAction* a : _actions)
        a->release();
}

void Spawn::startWithTarget(Node* target)
{
    ActionInterval::startWithTarget(target);
    for (FiniteTimeAction* a : _actions)
        a->startWithTarget(target);
}

void Spawn::stop()
{
    for (FiniteTimeAction* a : _actions)
        a->stop();
    ActionInterval::stop();
}

void Spawn::update(float t)
{
    for (FiniteTimeAction* a : _actions) {
        float d = a->getDuration();
        float local = d > 1e-6f ? std::min(1.0f, t * _duration / d) : 1.0f;
        a->update(local);
    }
}

RepeatForever* RepeatForever::create(ActionInterval* inner)
{
    RepeatForever* r = new RepeatForever();
    inner->retain();
    r->_inner = inner;
    r->autorelease();
    return r;
}

RepeatForever::~RepeatForever()
{
    CC_SAFE_RELEASE(_inner);
}

void RepeatForever::startWithTarget(Node* target)
{
    ActionInterval::startWithTarget(target);
    _inner->startWithTarget(target);
}

void RepeatForever::step(float dt)
{
    _inner->step(dt);
    if (_inner->isDone()) {
        float over = _inner->getElapsed() - _inner->getDuration();
        _inner->startWithTarget(_target);
        _inner->step(0.0f);
        _inner->step(over);
    }
}

EaseRateAction::~EaseRateAction()
{
    CC_SAFE_RELEASE(_inner);
}

void EaseRateAction::initWithAction(ActionInterval* inner, float rate)
{
    initWithDuration(inner->getDuration());
    inner->retain();
    _inner = inner;
    _rate = rate;
}

void EaseRateAction::startWithTarget(Node* target)
{
    ActionInterval::startWithTarget(target);
    _inner->startWithTarget(target);
}

void EaseRateAction::stop()
{
    _inner->stop();
    ActionInterval::stop();
}

EaseIn* EaseIn::create(ActionInterval* inner, float rate)
{
    EaseIn* e = new EaseIn();
    e->initWithAction(inner, rate);
    e->autorelease();
    return e;
}

void EaseIn::update(float t)
{
    _inner->update(powf(t, _rate));
}

EaseOut* EaseOut::create(ActionInterval* inner, float rate)
{
    EaseOut* e = new EaseOut();
    e->initWithAction(inner, rate);
    e->autorelease();
    return e;
}

void EaseOut::update(float t)
{
    _inner->update(powf(t, 1.0f / _rate));
}

EaseInOut* EaseInOut::create(ActionInterval* inner, float rate)
{
    EaseInOut* e = new EaseInOut();
    e->initWithAction(inner, rate);
    e->autorelease();
    return e;
}

void EaseInOut::update(float t)
{
    t *= 2.0f;
    if (t < 1.0f)
        _inner->update(0.5f * powf(t, _rate));
    else
        _inner->update(1.0f - 0.5f * powf(2.0f - t, _rate));
}

CallFunc* CallFunc::create(const std::function<void()>& fn)
{
    CallFunc* c = new CallFunc();
    c->_fn = fn;
    c->autorelease();
    return c;
}

void CallFunc::update(float)
{
    if (_fn)
        _fn();
}

RemoveSelf* RemoveSelf::create(bool cleanup)
{
    RemoveSelf* r = new RemoveSelf();
    r->_cleanup = cleanup;
    r->autorelease();
    return r;
}

void RemoveSelf::update(float)
{
    if (_target)
        _target->removeFromParentAndCleanup(_cleanup);
}

Show* Show::create()
{
    Show* s = new Show();
    s->autorelease();
    return s;
}

void Show::update(float)
{
    if (_target)
        _target->setVisible(true);
}

Hide* Hide::create()
{
    Hide* h = new Hide();
    h->autorelease();
    return h;
}

void Hide::update(float)
{
    if (_target)
        _target->setVisible(false);
}

// --------------------------------------------------------- ActionManager

void ActionManager::addAction(Action* action, Node* target, bool paused)
{
    if (!action || !target)
        return;
    action->retain();
    // Kept alive while its actions run, as cocos2d's ActionManager does.
    target->retain();
    _entries.push_back({action, target, paused, false});
    action->startWithTarget(target);
}

void ActionManager::removeAllActionsFromTarget(Node* target)
{
    for (Entry& e : _entries)
        if (e.target == target && !e.removed) {
            e.removed = true;
            if (e.action->getTarget())
                e.action->stop();
        }
    if (!_updating) {
        std::vector<Entry> keep;
        std::vector<Entry> drop;
        for (Entry& e : _entries)
            (e.removed ? drop : keep).push_back(e);
        _entries.swap(keep);
        for (Entry& e : drop) {
            e.action->release();
            e.target->release();
        }
    }
}

void ActionManager::removeAction(Action* action)
{
    for (Entry& e : _entries)
        if (e.action == action && !e.removed) {
            e.removed = true;
            if (action->getTarget())
                action->stop();
        }
}

void ActionManager::removeActionByTag(int tag, Node* target)
{
    for (Entry& e : _entries)
        if (e.target == target && !e.removed && e.action->getTag() == tag) {
            e.removed = true;
            if (e.action->getTarget())
                e.action->stop();
            return;
        }
}

Action* ActionManager::getActionByTag(int tag, Node* target) const
{
    for (const Entry& e : _entries)
        if (e.target == target && !e.removed && e.action->getTag() == tag)
            return e.action;
    return nullptr;
}

int ActionManager::getNumberOfRunningActionsInTarget(Node* target) const
{
    int n = 0;
    for (const Entry& e : _entries)
        if (e.target == target && !e.removed)
            n++;
    return n;
}

void ActionManager::pauseTarget(Node* target)
{
    for (Entry& e : _entries)
        if (e.target == target)
            e.paused = true;
}

void ActionManager::resumeTarget(Node* target)
{
    for (Entry& e : _entries)
        if (e.target == target)
            e.paused = false;
}

void ActionManager::update(float dt)
{
    _updating = true;
    // Actions added during the update start stepping next frame.
    size_t n = _entries.size();
    for (size_t i = 0; i < n; i++) {
        Entry e = _entries[i];
        if (e.removed || e.paused)
            continue;
        e.action->step(dt);
        if (!_entries[i].removed && e.action->isDone()) {
            _entries[i].removed = true;
            e.action->stop();
        }
    }
    _updating = false;
    std::vector<Entry> keep;
    std::vector<Entry> drop;
    for (Entry& e : _entries)
        (e.removed ? drop : keep).push_back(e);
    _entries.swap(keep);
    for (Entry& e : drop) {
        e.action->release();
        e.target->release();
    }
}

// -------------------------------------------------------------- Scheduler

void Scheduler::scheduleUpdate(Node* target, int priority, bool paused)
{
    for (Update& u : _updates)
        if (u.target == target && !u.dead)
            return;
    Update u = {target, priority, paused, false};
    // Stable by priority: lower first, then in the order scheduled.
    auto it = _updates.begin();
    while (it != _updates.end() && it->priority <= priority)
        ++it;
    _updates.insert(it, u);
}

void Scheduler::unscheduleUpdate(Node* target)
{
    for (Update& u : _updates)
        if (u.target == target)
            u.dead = true;
}

void Scheduler::schedule(SEL_SCHEDULE selector, Ref* target, float interval, unsigned repeat, float delay, bool paused)
{
    for (Timer& t : _timers)
        if (!t.dead && t.target == target && t.selector == selector && !t.fn) {
            t.interval = interval;
            return;
        }
    Timer t = {};
    t.target = target;
    t.selector = selector;
    t.interval = interval;
    t.elapsed = -1.0f;
    t.repeat = repeat;
    t.delay = delay;
    t.useDelay = delay > 0.0f;
    t.paused = paused;
    _timers.push_back(t);
}

void Scheduler::schedule(const std::function<void(float)>& fn, void* target, float interval, unsigned repeat,
                         float delay, bool paused, const std::string& key)
{
    for (Timer& t : _timers)
        if (!t.dead && t.target == target && t.fn && t.key == key) {
            t.interval = interval;
            return;
        }
    Timer t = {};
    t.target = target;
    t.selector = nullptr;
    t.fn = fn;
    t.key = key;
    t.interval = interval;
    t.elapsed = -1.0f;
    t.repeat = repeat;
    t.delay = delay;
    t.useDelay = delay > 0.0f;
    t.paused = paused;
    _timers.push_back(t);
}

void Scheduler::unschedule(SEL_SCHEDULE selector, Ref* target)
{
    for (Timer& t : _timers)
        if (t.target == target && !t.fn && t.selector == selector)
            t.dead = true;
}

void Scheduler::unschedule(const std::string& key, void* target)
{
    for (Timer& t : _timers)
        if (t.target == target && t.fn && t.key == key)
            t.dead = true;
}

void Scheduler::unscheduleAllForTarget(void* target)
{
    for (Timer& t : _timers)
        if (t.target == target)
            t.dead = true;
    for (Update& u : _updates)
        if (u.target == target)
            u.dead = true;
}

void Scheduler::pauseTarget(void* target)
{
    for (Timer& t : _timers)
        if (t.target == target)
            t.paused = true;
    for (Update& u : _updates)
        if (u.target == target)
            u.paused = true;
}

void Scheduler::resumeTarget(void* target)
{
    for (Timer& t : _timers)
        if (t.target == target)
            t.paused = false;
    for (Update& u : _updates)
        if (u.target == target)
            u.paused = false;
}

void Scheduler::update(float dt)
{
    if (_system)
        _system(dt);

    for (size_t i = 0; i < _updates.size(); i++) {
        Update u = _updates[i];
        if (!u.dead && !u.paused)
            u.target->update(dt);
    }
    _updates.erase(std::remove_if(_updates.begin(), _updates.end(), [](const Update& u) { return u.dead; }),
                   _updates.end());

    // As cocos2d's Timer: the first tick only starts the clock; a timer
    // fires with the time since it last fired.
    size_t n = _timers.size();
    for (size_t i = 0; i < n; i++) {
        if (_timers[i].dead || _timers[i].paused)
            continue;
        Timer& t = _timers[i];
        if (t.elapsed < 0.0f) {
            t.elapsed = 0.0f;
            t.timesExecuted = 0;
            continue;
        }
        t.elapsed += dt;
        bool forever = t.repeat == REPEAT_FOREVER;
        float fireDt = -1.0f;
        if (forever && !t.useDelay) {
            if (t.elapsed >= t.interval) {
                fireDt = t.elapsed;
                t.elapsed = 0.0f;
            }
        } else if (t.useDelay) {
            if (t.elapsed >= t.delay) {
                fireDt = t.delay;
                t.elapsed -= t.delay;
                t.timesExecuted++;
                t.useDelay = false;
            }
        } else if (t.elapsed >= t.interval) {
            fireDt = t.elapsed;
            t.elapsed = 0.0f;
            t.timesExecuted++;
        }
        bool finished = !forever && t.timesExecuted > t.repeat;
        // Copy what the call needs: it may schedule more and move _timers.
        std::function<void(float)> fn = t.fn;
        SEL_SCHEDULE sel = t.selector;
        void* target = t.target;
        if (finished)
            t.dead = true;
        if (fireDt >= 0.0f) {
            if (fn)
                fn(fireDt);
            else if (sel)
                (static_cast<Ref*>(target)->*sel)(fireDt);
        }
    }
    _timers.erase(std::remove_if(_timers.begin(), _timers.end(), [](const Timer& t) { return t.dead; }), _timers.end());
}

// ----------------------------------------------------------------- events

void Touch::setTouchInfo(int id, const Vec2& p, bool start)
{
    _id = id;
    if (start) {
        _startPoint = _prevPoint = _point = p;
    } else {
        _prevPoint = _point;
        _point = p;
    }
}

EventListenerTouchOneByOne* EventListenerTouchOneByOne::create()
{
    EventListenerTouchOneByOne* l = new EventListenerTouchOneByOne();
    l->_type = Type::TOUCH_ONE_BY_ONE;
    l->autorelease();
    return l;
}

EventListenerTouchAllAtOnce* EventListenerTouchAllAtOnce::create()
{
    EventListenerTouchAllAtOnce* l = new EventListenerTouchAllAtOnce();
    l->_type = Type::TOUCH_ALL_AT_ONCE;
    l->autorelease();
    return l;
}

void EventDispatcher::addEventListenerWithSceneGraphPriority(EventListener* listener, Node* node)
{
    listener->retain();
    listener->_node = node;
    listener->_fixedPriority = 0;
    listener->_registered = true;
    listener->_orderAdded = _added++;
    _listeners.push_back(listener);
}

void EventDispatcher::addEventListenerWithFixedPriority(EventListener* listener, int priority)
{
    listener->retain();
    listener->_node = nullptr;
    listener->_fixedPriority = priority;
    listener->_registered = true;
    listener->_orderAdded = _added++;
    _listeners.push_back(listener);
}

namespace {
void collectRemoved(std::vector<EventListener*>& listeners, int dispatching)
{
    if (dispatching)
        return;
    std::vector<EventListener*> keep;
    std::vector<EventListener*> drop;
    for (EventListener* l : listeners)
        (l->_registered ? keep : drop).push_back(l);
    listeners.swap(keep);
    for (EventListener* l : drop)
        l->release();
}

bool isDescendant(Node* node, Node* ancestor)
{
    for (Node* n = node; n; n = n->getParent())
        if (n == ancestor)
            return true;
    return false;
}

// cocos2d's EventDispatcher::visitTarget: the order nodes draw in, children
// below z 0 first. A higher order is drawn later, on top.
void visitOrder(Node* node, int& order)
{
    const std::vector<Node*>& children = node->getChildren();
    std::vector<Node*> sorted = children;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const Node* a, const Node* b) { return a->getLocalZOrder() < b->getLocalZOrder(); });
    size_t i = 0;
    for (; i < sorted.size() && sorted[i]->getLocalZOrder() < 0; i++)
        visitOrder(sorted[i], order);
    node->_eventOrder = ++order;
    for (; i < sorted.size(); i++)
        visitOrder(sorted[i], order);
}
} // namespace

void EventDispatcher::removeEventListener(EventListener* listener)
{
    for (EventListener* l : _listeners)
        if (l == listener)
            l->_registered = false;
    collectRemoved(_listeners, _dispatching);
}

void EventDispatcher::removeEventListenersForTarget(Node* target, bool recursive)
{
    bool any = false;
    for (EventListener* l : _listeners)
        if (l->_registered && l->_node && (l->_node == target || (recursive && isDescendant(l->_node, target)))) {
            l->_registered = false;
            any = true;
        }
    if (any)
        collectRemoved(_listeners, _dispatching);
}

void EventDispatcher::removeAllEventListeners()
{
    for (EventListener* l : _listeners)
        l->_registered = false;
    collectRemoved(_listeners, _dispatching);
}

void EventDispatcher::sortedListeners(std::vector<EventListener*>& out, EventListener::Type type)
{
    out.clear();
    for (EventListener* l : _listeners)
        if (l->_registered && l->_type == type)
            out.push_back(l);
    if (out.empty())
        return;
    if (Scene* scene = Director::getInstance()->getRunningScene()) {
        int order = 0;
        visitOrder(scene, order);
    }
    std::stable_sort(out.begin(), out.end(), [](const EventListener* a, const EventListener* b) {
        // Fixed below 0, then the scene graph (top first), then fixed above.
        auto band = [](const EventListener* l) { return l->_node ? 1 : l->_fixedPriority < 0 ? 0 : 2; };
        int ba = band(a), bb = band(b);
        if (ba != bb)
            return ba < bb;
        if (ba == 1)
            return a->_node->_eventOrder > b->_node->_eventOrder;
        if (a->_fixedPriority != b->_fixedPriority)
            return a->_fixedPriority < b->_fixedPriority;
        return a->_orderAdded < b->_orderAdded;
    });
}

void EventDispatcher::dispatchTouch(TouchPhase phase, const std::vector<Touch*>& touches)
{
    if (!_enabled)
        return;
    _dispatching++;
    for (EventListener* l : _listeners)
        l->retain();
    std::vector<EventListener*> listenersCopy = _listeners;

    auto live = [](EventListener* l) {
        return l->_registered && l->_enabled && (!l->_node || l->_node->isRunning());
    };

    std::vector<EventListener*> oneByOne, allAtOnce;
    sortedListeners(oneByOne, EventListener::Type::TOUCH_ONE_BY_ONE);
    sortedListeners(allAtOnce, EventListener::Type::TOUCH_ALL_AT_ONCE);

    std::vector<Touch*> remaining = touches;
    EventTouch event;
    bool stopped = false;
    for (Touch* touch : touches) {
        if (stopped)
            break;
        for (EventListener* base : oneByOne) {
            auto* l = static_cast<EventListenerTouchOneByOne*>(base);
            if (!live(l))
                continue;
            bool claimed = false;
            auto it = std::find(l->_claimed.begin(), l->_claimed.end(), touch);
            switch (phase) {
            case TouchPhase::BEGAN:
                if (l->onTouchBegan) {
                    claimed = l->onTouchBegan(touch, &event);
                    if (claimed && l->_registered)
                        l->_claimed.push_back(touch);
                }
                break;
            case TouchPhase::MOVED:
                if (it != l->_claimed.end()) {
                    claimed = true;
                    if (l->onTouchMoved)
                        l->onTouchMoved(touch, &event);
                }
                break;
            case TouchPhase::ENDED:
            case TouchPhase::CANCELLED:
                if (it != l->_claimed.end()) {
                    claimed = true;
                    l->_claimed.erase(it);
                    if (phase == TouchPhase::ENDED && l->onTouchEnded)
                        l->onTouchEnded(touch, &event);
                    else if (phase == TouchPhase::CANCELLED && l->onTouchCancelled)
                        l->onTouchCancelled(touch, &event);
                }
                break;
            }
            if (event.isStopped()) {
                stopped = true;
                break;
            }
            if (claimed && l->_registered && l->_swallow) {
                remaining.erase(std::remove(remaining.begin(), remaining.end(), touch), remaining.end());
                break;
            }
        }
    }

    if (!stopped && !remaining.empty()) {
        for (EventListener* base : allAtOnce) {
            auto* l = static_cast<EventListenerTouchAllAtOnce*>(base);
            if (!live(l))
                continue;
            switch (phase) {
            case TouchPhase::BEGAN:
                if (l->onTouchesBegan)
                    l->onTouchesBegan(remaining, &event);
                break;
            case TouchPhase::MOVED:
                if (l->onTouchesMoved)
                    l->onTouchesMoved(remaining, &event);
                break;
            case TouchPhase::ENDED:
                if (l->onTouchesEnded)
                    l->onTouchesEnded(remaining, &event);
                break;
            case TouchPhase::CANCELLED:
                if (l->onTouchesCancelled)
                    l->onTouchesCancelled(remaining, &event);
                break;
            }
            if (event.isStopped())
                break;
        }
    }

    _dispatching--;
    for (EventListener* l : listenersCopy)
        l->release();
    collectRemoved(_listeners, _dispatching);
}

// ---------------------------------------------------------- transitions

TransitionScene::~TransitionScene()
{
    CC_SAFE_RELEASE(_inScene);
    CC_SAFE_RELEASE(_outScene);
}

bool TransitionScene::initWithDuration(float t, Scene* scene)
{
    _duration = t;
    _inScene = scene;
    _inScene->retain();
    _outScene = Director::getInstance()->getRunningScene();
    if (!_outScene) {
        _outScene = Scene::create();
        _outScene->init();
    }
    _outScene->retain();
    return true;
}

void TransitionScene::onEnter()
{
    Scene::onEnter();
    getEventDispatcher()->setEnabled(false);
    _outScene->onExitTransitionDidStart();
    _inScene->onEnter();
}

void TransitionScene::onExit()
{
    Scene::onExit();
    getEventDispatcher()->setEnabled(true);
    _outScene->onExit();
    _inScene->onEnterTransitionDidFinish();
}

void TransitionScene::cleanup()
{
    Scene::cleanup();
    if (_isSendCleanupToScene)
        _outScene->cleanup();
}

void TransitionScene::finish()
{
    _inScene->setVisible(true);
    _inScene->setPosition(0, 0);
    _inScene->setScale(1.0f);
    _inScene->setRotation(0.0f);
    _outScene->setVisible(false);
    _outScene->setPosition(0, 0);
    _outScene->setScale(1.0f);
    _outScene->setRotation(0.0f);
    schedule(CC_SCHEDULE_SELECTOR(TransitionScene::setNewScene), 0.0f);
}

void TransitionScene::hideOutShowIn()
{
    _inScene->setVisible(true);
    _outScene->setVisible(false);
}

void TransitionScene::setNewScene(float)
{
    unschedule(CC_SCHEDULE_SELECTOR(TransitionScene::setNewScene));
    Director* d = Director::getInstance();
    _isSendCleanupToScene = d->isSendCleanupToScene();
    d->replaceScene(_inScene);
    getEventDispatcher()->setEnabled(true);
    _outScene->setVisible(true);
}

void TransitionScene::visit(Renderer* renderer, const Mat4& parentTransform, u32 parentFlags)
{
    Scene::visit(renderer, parentTransform, parentFlags);
    if (_isInSceneOnTop) {
        _outScene->visit(renderer, parentTransform, parentFlags);
        _inScene->visit(renderer, parentTransform, parentFlags);
    } else {
        _inScene->visit(renderer, parentTransform, parentFlags);
        _outScene->visit(renderer, parentTransform, parentFlags);
    }
}

TransitionMoveInL* TransitionMoveInL::create(float t, Scene* scene)
{
    TransitionMoveInL* s = new TransitionMoveInL();
    s->initWithDuration(t, scene);
    s->autorelease();
    return s;
}

void TransitionMoveInL::onEnter()
{
    TransitionScene::onEnter();
    initScenes();
    ActionInterval* move = MoveTo::create(_duration, Vec2::ZERO);
    _inScene->runAction(Sequence::create(EaseOut::create(move, 2.0f),
                                         CallFunc::create(CC_CALLBACK_0(TransitionScene::finish, this)), nullptr));
}

void TransitionMoveInL::initScenes()
{
    _inScene->setPosition(startPosition());
}

Vec2 TransitionMoveInL::startPosition() const
{
    return Vec2(-kDesignWidth, 0.0f);
}

TransitionMoveInB* TransitionMoveInB::create(float t, Scene* scene)
{
    TransitionMoveInB* s = new TransitionMoveInB();
    s->initWithDuration(t, scene);
    s->autorelease();
    return s;
}

Vec2 TransitionMoveInB::startPosition() const
{
    return Vec2(0.0f, -kDesignHeight);
}

TransitionMoveInR* TransitionMoveInR::create(float t, Scene* scene)
{
    TransitionMoveInR* s = new TransitionMoveInR();
    s->initWithDuration(t, scene);
    s->autorelease();
    return s;
}

Vec2 TransitionMoveInR::startPosition() const
{
    return Vec2(kDesignWidth, 0.0f);
}

TransitionMoveInT* TransitionMoveInT::create(float t, Scene* scene)
{
    TransitionMoveInT* s = new TransitionMoveInT();
    s->initWithDuration(t, scene);
    s->autorelease();
    return s;
}

Vec2 TransitionMoveInT::startPosition() const
{
    return Vec2(0.0f, kDesignHeight);
}

// --------------------------------------------------------------- Director

Director* Director::getInstance()
{
    static Director* s = new Director();
    return s;
}

Director::Director()
{
    _scheduler.setSystemUpdate([this](float dt) { _actionManager.update(dt); });
    _lastTick = svcGetSystemTick();
}

void Director::runWithScene(Scene* scene)
{
    replaceScene(scene);
}

void Director::replaceScene(Scene* scene)
{
    if (!scene)
        return;
    if (_nextScene) {
        if (_nextScene->isRunning())
            _nextScene->onExit();
        _nextScene->cleanup();
        _nextScene->release();
        _nextScene = nullptr;
    }
    _sendCleanupToScene = true;
    scene->retain();
    _nextScene = scene;
}

void Director::setNextScene()
{
    bool runningIsTransition = dynamic_cast<TransitionScene*>(_runningScene) != nullptr;
    bool newIsTransition = dynamic_cast<TransitionScene*>(_nextScene) != nullptr;
    if (!newIsTransition) {
        if (_runningScene) {
            _runningScene->onExitTransitionDidStart();
            _runningScene->onExit();
        }
        if (_sendCleanupToScene && _runningScene)
            _runningScene->cleanup();
    }
    if (_runningScene)
        _runningScene->release();
    _runningScene = _nextScene; // keeps replaceScene's retain
    _nextScene = nullptr;
    if (!runningIsTransition && _runningScene) {
        _runningScene->onEnter();
        _runningScene->onEnterTransitionDidFinish();
    }
}

void Director::setTopScreenNode(Node* node)
{
    if (node == _topNode)
        return;
    if (_topNode) {
        _topNode->onExitTransitionDidStart();
        _topNode->onExit();
        _topNode->cleanup();
        _topNode->release();
    }
    _topNode = node;
    if (_topNode) {
        _topNode->retain();
        _topNode->onEnter();
        _topNode->onEnterTransitionDidFinish();
    }
}

void Director::canvasToScreen(const Vec2& p, float* sx, float* sy) const
{
    *sx = p.x * (320.0f / kDesignWidth);
    *sy = (kDesignHeight - p.y) * (240.0f / kDesignHeight);
}

void Director::handleTouch()
{
    if (!_touch)
        _touch = new Touch();
    touchPosition tp;
    if ((_keysDown | _keysHeld) & KEY_TOUCH)
        hidTouchRead(&tp);
    auto canvas = [&]() {
        return Vec2((tp.px + 0.5f) * (kDesignWidth / 320.0f), (240.0f - (tp.py + 0.5f)) * (kDesignHeight / 240.0f));
    };
    std::vector<Touch*> touches = {_touch};
    if (_keysDown & KEY_TOUCH) {
        _touch->setTouchInfo(0, canvas(), true);
        _touching = true;
        _eventDispatcher.dispatchTouch(EventDispatcher::TouchPhase::BEGAN, touches);
    } else if (_touching && (_keysHeld & KEY_TOUCH)) {
        Vec2 p = canvas();
        if (p != _touch->getLocation()) {
            _touch->setTouchInfo(0, p, false);
            _eventDispatcher.dispatchTouch(EventDispatcher::TouchPhase::MOVED, touches);
        }
    } else if (_touching) {
        _touching = false;
        // The pen's last position: KEY_TOUCH is already up, so no new read.
        _touch->setTouchInfo(0, _touch->getLocation(), false);
        _eventDispatcher.dispatchTouch(EventDispatcher::TouchPhase::ENDED, touches);
    }
}

bool Director::mainLoop()
{
    if (_quit || !aptMainLoop())
        return false;

    u64 now = svcGetSystemTick();
    if (_lastTick) {
        const u64 frame = now - _lastTick;
        _frameTicks += frame;
        if (frame > _worstTicks)
            _worstTicks = frame;
    }
    _deltaTime = (float)(now - _lastTick) / (float)SYSCLOCK_ARM11;
    _lastTick = now;
    // As cocos2d after a stall (a breakpoint, a long load): one frame's time.
    if (_deltaTime > 0.2f || _deltaTime <= 0.0f)
        _deltaTime = 1.0f / 60.0f;

    hidScanInput();
    _keysDown = ::hidKeysDown();
    _keysHeld = ::hidKeysHeld();
    _keysUp = ::hidKeysUp();

    if (_nextScene)
        setNextScene();
    handleTouch();
    u64 t[kPhases + 1];
    t[0] = now;
    t[1] = svcGetSystemTick();
    _scheduler.update(_deltaTime);
    if (_nextScene)
        setNextScene();
    t[2] = svcGetSystemTick();

    gfx::frameBegin();
    t[3] = svcGetSystemTick();
    gfx::beginScreen(gfx::Screen::Top, gfx::rgba(0, 0, 0, 255));
    if (_topNode) {
        gfx::blendPremultiplied();
        _topNode->visit(nullptr, Mat4::IDENTITY, 0);
    }
    t[4] = svcGetSystemTick();
    Color4B cc(_clearColor);
    gfx::beginScreen(gfx::Screen::Bottom, gfx::rgba(cc.r, cc.g, cc.b, 255));
    gfx::ortho(0.0f, kDesignWidth, 0.0f, kDesignHeight);
    if (_runningScene) {
        gfx::blendPremultiplied();
        _runningScene->visit(nullptr, Mat4::IDENTITY, 0);
    }
    t[5] = svcGetSystemTick();
    gfx::frameEnd();

    drainAutoreleasePool();
    t[6] = svcGetSystemTick();
    for (int i = 0; i < kPhases; i++)
        _phaseTicks[i] += t[i + 1] - t[i];
    _gpuMs += C3D_GetDrawingTime();
    _profiledFrames++;
    _totalFrames++;
    return true;
}

Director::FrameProfile Director::takeFrameProfile()
{
    FrameProfile p = {};
    p.frames = _profiledFrames;
    if (_profiledFrames) {
        const float ms = 1000.0f / (float)SYSCLOCK_ARM11 / (float)_profiledFrames;
        p.input = _phaseTicks[kPhaseInput] * ms;
        p.update = _phaseTicks[kPhaseUpdate] * ms;
        p.wait = _phaseTicks[kPhaseWait] * ms;
        p.top = _phaseTicks[kPhaseTop] * ms;
        p.bottom = _phaseTicks[kPhaseBottom] * ms;
        p.submit = _phaseTicks[kPhaseSubmit] * ms;
        p.gpu = _gpuMs / (float)_profiledFrames;
        p.average = _frameTicks * ms;
        p.worst = _worstTicks * 1000.0f / (float)SYSCLOCK_ARM11;
    }
    for (u64& ticks : _phaseTicks)
        ticks = 0;
    _frameTicks = _worstTicks = 0;
    _gpuMs = 0.0f;
    _profiledFrames = 0;
    return p;
}

// -------------------------------------------------------------- FileUtils

namespace {
std::string lower(std::string s)
{
    for (char& c : s)
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
    return s;
}

// The extension prepare_data.py gave each kind of file.
std::string cardName(const std::string& name)
{
    size_t dot = name.rfind('.');
    if (dot == std::string::npos)
        return name;
    std::string ext = lower(name.substr(dot + 1));
    std::string base = name.substr(0, dot);
    if (ext == "png" || ext == "jpg" || ext == "jpeg")
        return base + ".t3x";
    if (ext == "plist")
        return base + ".frames";
    if (ext == "ttf")
        return base + ".bcfnt";
    if (ext == "mp3")
        return base + ".wav";
    return name;
}

std::map<std::string, std::string> s_resolved;

// Whether a directory exists, asked once each. A search path is tried for
// every file, and a stat that misses costs as much as one that hits (~20 ms
// on Azahar): the GUI's search paths made each sound 40 ms slower to find.
std::map<std::string, bool> s_dirs;

bool dirExists(const std::string& dir)
{
    auto it = s_dirs.find(dir);
    if (it != s_dirs.end())
        return it->second;
    struct stat st;
    bool exists = stat(dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
    s_dirs[dir] = exists;
    return exists;
}
} // namespace

FileUtils* FileUtils::getInstance()
{
    static FileUtils s;
    return &s;
}

void FileUtils::addSearchPath(const std::string& path, bool front)
{
    std::string p = path;
    if (!p.empty() && p.back() != '/')
        p += '/';
    if (front)
        _searchPaths.insert(_searchPaths.begin(), p);
    else
        _searchPaths.push_back(p);
    s_resolved.clear();
}

bool FileUtils::isFileExist(const std::string& path) const
{
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

std::string FileUtils::fullPathForFilename(const std::string& filename) const
{
    if (filename.empty() || filename.compare(0, 5, "sdmc:") == 0 || filename.compare(0, 6, "romfs:") == 0)
        return filename;
    auto it = s_resolved.find(filename);
    if (it != s_resolved.end())
        return it->second;
    // The build's RomFS or the card's folder (paths.h).
    const std::string root = std::string(zosAssetRoot()) + "/";
    std::string name = cardName(filename);
    std::string found;
    size_t slash = name.rfind('/');
    std::string subdir = slash == std::string::npos ? std::string() : name.substr(0, slash);
    for (const std::string& sp : _searchPaths) {
        std::string dir = root + sp + subdir;
        if (!dir.empty() && dir.back() == '/')
            dir.pop_back();
        if (!dirExists(dir))
            continue;
        std::string candidate = root + sp + name;
        if (isFileExist(candidate)) {
            found = candidate;
            break;
        }
    }
    if (found.empty())
        found = root + name;
    s_resolved[filename] = found;
    return found;
}

std::vector<u8> FileUtils::getDataFromFile(const std::string& filename) const
{
    std::vector<u8> data;
    FILE* f = fopen(fullPathForFilename(filename).c_str(), "rb");
    if (!f)
        return data;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size > 0) {
        data.resize(size);
        if (fread(data.data(), 1, size, f) != (size_t)size)
            data.clear();
    }
    fclose(f);
    return data;
}

std::string FileUtils::getStringFromFile(const std::string& filename) const
{
    std::vector<u8> d = getDataFromFile(filename);
    return std::string(d.begin(), d.end());
}

std::string FileUtils::getWritablePath() const
{
    mkdir(ZOS_DATA_ROOT, 0777);
    mkdir(ZOS_DATA_ROOT "/save", 0777);
    return ZOS_DATA_ROOT "/save/";
}

} // namespace cocos2d
