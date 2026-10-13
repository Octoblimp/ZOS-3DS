// The port's preferences: what the game kept in Android's SharedPreferences
// through NativeBridge (decomp/classes/NativeBridge.c), in save/prefs.txt on
// the SD card as key=value lines. Saved on every change.
#pragma once

#include <map>
#include <string>

class Prefs {
public:
    static Prefs& getInstance();

    bool getBool(const std::string& key, bool defaultValue) const;
    void setBool(const std::string& key, bool value);

    // NativeBridge's: whether the tutorial was played to its end (Change
    // map from it). Until then Play starts it.
    bool getTutorialViewed() const { return getBool("tutorialViewed", false); }
    void setTutorialViewed() { setBool("tutorialViewed", true); }

    // The port's positional sound (SoundManager), off as in the game.
    bool getPositionalSound() const { return getBool("positionalSound", false); }
    void setPositionalSound(bool on) { setBool("positionalSound", on); }

private:
    Prefs();
    void save() const;

    std::string _path;
    std::map<std::string, std::string> _values;
};
