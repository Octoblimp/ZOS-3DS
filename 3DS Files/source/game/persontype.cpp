#include "game/persontype.h"

#include <cstdio>
#include <stdexcept>

#include "cocos/director.h"
#include "game/utils.h"

// ---- Animation

Animation::Animation(const std::string& name, int frames, int frameMs, bool loop, const AnimationIndexImage* table)
    : _frameCount(frames), _frameMs(frameMs), _loop(loop)
{
    char msg[1024];
    _frames.resize(frames);
    for (int i = 0; i < frames; i++) {
        const AnimationIndexImage& f = table[i];
        if (f.x == -1) {
            snprintf(msg, sizeof(msg), "The %s animation has only %d frames in the source texture", name.c_str(),
                     i);
            throw std::runtime_error(msg);
        }
        if (i == 0) {
            _frameWidth = f.w;
            _frameHeight = f.h;
        } else if (f.w != _frameWidth || f.h != _frameHeight) {
            snprintf(msg, sizeof(msg),
                     "The %s animation image for frame = %d has dimensions (%d x %d), which are different to the "
                     "dimensions of the first frame (%d x %d)",
                     name.c_str(), i + 1, f.w, f.h, _frameWidth, _frameHeight);
            throw std::runtime_error(msg);
        }
        _frames[i].x = f.x;
        _frames[i].y = f.y;
    }
    if (table[frames].x != -1) {
        snprintf(msg, sizeof(msg), "The %s animation has more than %d frames in the source texture", name.c_str(),
                 frames);
        throw std::runtime_error(msg);
    }
}

void Animation::setTextureSize(int width, int height)
{
    float w = (float)width;
    float h = (float)height;
    for (AnimationFrame& f : _frames) {
        f.u0 = (float)f.x / w;
        f.u1 = (float)(f.x + _frameWidth) / w;
        f.v0 = (float)f.y / h;
        f.v1 = (float)(f.y + _frameHeight) / h;
    }
}

// ---- AnimationPair

AnimationPair::AnimationPair(const std::string& name, int frames, int frameMs, bool loop,
                             const AnimationIndexImage* visibleTable, const AnimationIndexImage* hiddenTable)
    : visible(name + " visible", frames, frameMs, loop, visibleTable),
      hidden(name + " hidden", frames, frameMs, loop, hiddenTable)
{
}

void AnimationPair::setTextureSize(int width, int height)
{
    visible.setTextureSize(width, height);
    hidden.setTextureSize(width, height);
}

// ---- PersonType

PersonType::PersonType(const std::string& texturePath) : _texturePath(texturePath) {}

PersonType::~PersonType()
{
    if (_texture) {
        gfx::destroyTexture(_texture);
        delete _texture;
    }
}

void PersonType::setup(int stepMs)
{
    float maxSpeed = (float)getMaxSpeed(stepMs);
    _zombieSpeed = _zombieSpeedBase;
    const char* what = nullptr;
    float speed = 0;
    if (maxSpeed < _walkSpeed) {
        what = "walk";
        speed = _walkSpeed;
    } else if (maxSpeed < _runSpeed) {
        what = "run";
        speed = _runSpeed;
    } else if (maxSpeed < _zombieSpeed) {
        what = "zombie walk";
        speed = _zombieSpeed;
    }
    if (what) {
        throw std::runtime_error(std::string("The ") + what + " speed of " + _name + " (" +
                                 Utils::floatToString(speed, 0) +
                                 " pixels/sec) is greater than the maximum allowable speed (" +
                                 Utils::floatToString(maxSpeed, 0) + " pixels/sec).\n" +
                                 "Either reduce the speed, or increase the simulation StepRate. " +
                                 "Note that increasing the step rate will reduce the frame rate.");
    }
    _dieDuration = getAnimation(Die)->visible.getDuration();
    _turnDuration = getAnimation(Turn)->visible.getDuration();
    _zombieRiseDuration = getAnimation(ZombieRise)->visible.getDuration();
    _zombieAttackDuration = getAnimation(ZombieAttack)->visible.getDuration();
    _shootDuration = getAnimation(Shoot)->visible.getDuration();
    _zombieDie1Duration = getAnimation(ZombieDie1)->visible.getDuration();
    _zombieDie2Duration = getAnimation(ZombieDie2)->visible.getDuration();
    _searchSteps = _searchInterval / stepMs;
    _zombieSearchSteps = _zombieSearchInterval / stepMs;
    _turnPerStep = (float)(((double)stepMs * 3.141592653589793) / (double)_halfTurnMs);
    _stepsPerRadian = 1.0f / _turnPerStep;
}

void PersonType::capSpeeds(int stepMs)
{
    float maxSpeed = (float)getMaxSpeed(stepMs);
    if (maxSpeed < _walkSpeed)
        _walkSpeed = maxSpeed;
    if (maxSpeed < _runSpeed)
        _runSpeed = maxSpeed;
    if (maxSpeed < _zombieSpeed)
        _zombieSpeed = maxSpeed;
}

void PersonType::loadTexture()
{
    // The game made a 16 bpp texture of the sheet; so does the port, the
    // RGBA4444 .t3x tools/prepare_data.py made of it.
    if (!_texture)
        _texture = new gfx::Texture();
    std::string path = cocos2d::FileUtils::getInstance()->fullPathForFilename(_texturePath);
    if (!gfx::loadTexture(_texture, path.c_str()))
        throw std::runtime_error("Failed to load file: " + _texturePath);
    for (auto& pair : _animations)
        pair->setTextureSize(_texture->width, _texture->height);
}

int PersonType::getDieAnimationDuration(int dieAnimation) const
{
    if (dieAnimation == 1)
        return _dieDuration;
    if (dieAnimation == 2)
        return _zombieDie2Duration;
    throw std::runtime_error("Invalid die animation " + Utils::intToString(dieAnimation));
}

int PersonType::getZombieDieAnimationDuration(int dieAnimation) const
{
    if (dieAnimation == 1)
        return _zombieDie1Duration;
    if (dieAnimation == 2)
        return _zombieDie2Duration;
    throw std::runtime_error("Invalid zombie die animation " + Utils::intToString(dieAnimation));
}

// ---- PersonTypeFactory

PersonTypeFactory::~PersonTypeFactory()
{
    for (PersonType*& t : _allPersonTypes) {
        delete t;
        t = nullptr;
    }
}

void PersonTypeFactory::setupPersonTypes(int stepMs)
{
    createPersonTypes();
    for (size_t i = 0; i < _allPersonTypes.size(); i++) {
        PersonType* t = _allPersonTypes[i];
        t->_index = (int)i;
        t->setup(stepMs);
        _personTypesByName[t->_name] = t;
    }
}

void PersonTypeFactory::loadStep(bool& done, int& percent)
{
    int count = (int)_allPersonTypes.size();
    if (_loaded >= count)
        throw std::runtime_error("PersonTypeFactory::loadStep() has been called too many times");
    _allPersonTypes[_loaded]->loadTexture();
    _loaded++;
    done = _loaded >= count;
    percent = (int)((unsigned)(_loaded * 100) / (unsigned)count);
}

PersonType* PersonTypeFactory::getPersonTypeByName(const std::string& name) const
{
    auto it = _personTypesByName.find(name);
    if (it == _personTypesByName.end())
        throw std::runtime_error("Unable to find person type with name " + name);
    return it->second;
}
