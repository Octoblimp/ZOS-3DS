#include "game/guiutils.h"

#include "game/guiglobals.h"
#include "game/utils.h"

using namespace cocos2d;

namespace GUIUtils {

ButtonData* getButtonData(Node* node)
{
    return node ? dynamic_cast<ButtonData*>(node->getUserObject()) : nullptr;
}

bool hasVisibleParents(Node* node)
{
    for (Node* p = node->getParent(); p; p = p->getParent())
        if (!p->isVisible())
            return false;
    return true;
}

bool checkTouch(Node* node, const Vec2& location)
{
    if (!node || !node->isVisible() || !hasVisibleParents(node))
        return false;
    const Size size = node->getContentSize();
    return Rect(0.0f, 0.0f, size.width, size.height).containsPoint(node->convertToNodeSpace(location));
}

bool checkTouchAny(const std::set<Node*>& nodes, const Vec2& location)
{
    return touchedNode(nodes, location) != nullptr;
}

Node* touchedNode(const std::set<Node*>& nodes, const Vec2& location)
{
    for (Node* n : nodes)
        if (checkTouch(n, location))
            return n;
    return nullptr;
}

void createButtonData(Node* node, const std::string& name, const Color3B& color)
{
    ButtonData* data = new ButtonData();
    data->normal = name;
    data->pressed = name;
    const size_t png = data->pressed.find(".png");
    if (png != std::string::npos)
        data->pressed.replace(png, 4, "_Press.png");
    data->color = color;
    node->setUserObject(data);
    data->release();
}

void setPressed(Node* button)
{
    ButtonData* data = getButtonData(button);
    Sprite* sprite = dynamic_cast<Sprite*>(button);
    if (data && sprite)
        sprite->setSpriteFrame(SpriteFrameCache::getInstance()->getSpriteFrameByName(data->pressed));
}

void resetInteraction(Node* button)
{
    ButtonData* data = getButtonData(button);
    if (!data)
        return;
    if (Sprite* sprite = dynamic_cast<Sprite*>(button))
        sprite->setSpriteFrame(SpriteFrameCache::getInstance()->getSpriteFrameByName(data->normal));
    if (Node* label = button->getChildByTag(1))
        label->setColor(data->color);
}

Label* createLabel(const std::string& text, float size)
{
    return Label::createWithTTF(text.empty() ? " " : text, GUIGlobals::kFont, GUIGlobals::dpToSize * size);
}

Label* createButtonLabel(Sprite* sprite, const std::string& text)
{
    Label* label = Label::createWithTTF(text, GUIGlobals::kFont, GUIGlobals::dpToSize * 18.0f);
    const Size size = sprite->getContentSize();
    label->setPosition(Vec2(size.width * 0.5f, label->getContentSize().height * -0.5f));
    sprite->addChild(label, 0, 1);
    return label;
}

Sprite* createButton(const std::string& frame, const std::string& text, Node* parent, float x, float y, int z)
{
    Sprite* sprite = Sprite::createWithSpriteFrameName(frame);
    createButtonData(sprite, frame, Color3B(255, 255, 255));
    sprite->setPosition(Vec2(x, y));
    Label* label = createButtonLabel(sprite, text);
    const float labelHeight = label->getContentSize().height;
    sprite->setAnchorPoint(Vec2(0.5f, 0.5f - labelHeight * 0.5f / sprite->getContentSize().height));
    parent->addChild(sprite, z);
    return sprite;
}

DrawNode* createDrawRectangle(float w, float h, const Color4F& color)
{
    DrawNode* node = DrawNode::create(2.0f);
    const Vec2 corners[4] = {Vec2(0.0f, 0.0f), Vec2(w, 0.0f), Vec2(w, h), Vec2(0.0f, h)};
    node->drawPolygon(corners, 4, color, 0.0f, color);
    node->setContentSize(Size(w, h));
    return node;
}

ClippingNode* createClipper(float w, float h)
{
    ClippingNode* clipper = ClippingNode::create(createDrawRectangle(w, h, Color4F(0.0f, 0.0f, 0.0f, 1.0f)));
    clipper->setInverted(false);
    return clipper;
}

Size spriteSizeFromFrameCache(const std::string& name)
{
    SpriteFrame* frame = SpriteFrameCache::getInstance()->getSpriteFrameByName(name);
    if (!frame) {
        Utils::log("%s not found", name.c_str());
        return Size(0.0f, 0.0f);
    }
    return frame->getOriginalSize();
}

Size textDimensionsFromFontSize(const std::string& font, float size)
{
    return Label::createWithTTF("x", font, size)->getContentSize();
}

} // namespace GUIUtils
