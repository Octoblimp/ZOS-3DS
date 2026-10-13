#include "game/prefs.h"

#include <cstdio>

#include "cocos/director.h"
#include "game/utils.h"

Prefs& Prefs::getInstance()
{
    static Prefs prefs;
    return prefs;
}

Prefs::Prefs()
{
    _path = cocos2d::FileUtils::getInstance()->getWritablePath() + "prefs.txt";
    FILE* f = fopen(_path.c_str(), "r");
    if (f == nullptr)
        return;
    char line[256];
    while (fgets(line, sizeof(line), f) != nullptr) {
        std::string s(line);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
            s.pop_back();
        const size_t eq = s.find('=');
        if (eq != std::string::npos && eq > 0)
            _values[s.substr(0, eq)] = s.substr(eq + 1);
    }
    fclose(f);
}

bool Prefs::getBool(const std::string& key, bool defaultValue) const
{
    auto it = _values.find(key);
    if (it == _values.end())
        return defaultValue;
    return it->second == "1";
}

void Prefs::setBool(const std::string& key, bool value)
{
    const std::string v = value ? "1" : "0";
    auto it = _values.find(key);
    if (it != _values.end() && it->second == v)
        return;
    _values[key] = v;
    save();
}

void Prefs::save() const
{
    FILE* f = fopen(_path.c_str(), "w");
    if (f == nullptr) {
        Utils::log("Error opening file: %s", _path.c_str());
        return;
    }
    for (const auto& kv : _values)
        fprintf(f, "%s=%s\n", kv.first.c_str(), kv.second.c_str());
    fclose(f);
}
