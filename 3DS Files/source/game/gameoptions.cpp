#include "game/gameoptions.h"

#include <cstdlib>
#include <stdexcept>

#include "game/utils.h"

GameOptions::OptionValue::OptionValue(int value) : OptionValue(value, Utils::intToString(value)) {}

GameOptions::OptionValue::OptionValue(int value, const std::string& formatted) : _value(value), _formatted(formatted)
{
}

void GameOptions::Option::addValue(const OptionValue& value)
{
    _values.push_back(value);
    if (_longest < (int)value.getFormattedValue().size())
        _longest = (int)value.getFormattedValue().size();
}

void GameOptions::Option::addIntegerSequence(int from, int to, int step)
{
    for (int v = from; v <= to; v += step)
        addValue(OptionValue(v));
}

void GameOptions::Option::addPercentSequence(int from, int to, int step)
{
    char text[256];
    for (int v = from; v <= to; v += step) {
        snprintf(text, sizeof(text), "%d%%", v);
        addValue(OptionValue(v, text));
    }
}

int GameOptions::Option::getSelectedValue() const
{
    if (_selected < 0 || _selected >= (int)_values.size())
        throw std::runtime_error("Invalid selected index for option " + _label);
    return _values[_selected].getValue();
}

bool GameOptions::Option::setSelectedValue(int value)
{
    for (size_t i = 0; i < _values.size(); i++) {
        if (_values[i].getValue() == value) {
            _selected = (int)i;
            return true;
        }
    }
    return false;
}

void GameOptions::Option::setSelectedValueRequired(int value)
{
    if (!setSelectedValue(value))
        throw std::runtime_error("Option " + _label + " does not contain a value equal to " +
                                 Utils::intToString(value));
}

void GameOptions::Option::setSelectedValueClosestAvailable(int value)
{
    int best = -1, bestDistance = 0x7fffffff;
    for (size_t i = 0; i < _values.size(); i++) {
        if (!_values[i].getAvailable())
            continue;
        const int d = std::abs(_values[i].getValue() - value);
        if (d <= bestDistance) {
            bestDistance = d;
            best = (int)i;
        }
    }
    _selected = best;
}

void GameOptions::Option::clearValues()
{
    _values.clear();
    _selected = 0;
    _longest = 0;
}

void GameOptions::Option::removeValuesGreaterThan(int value)
{
    while (!_values.empty() && _values.back().getValue() > value)
        _values.pop_back();
    if (_selected >= (int)_values.size())
        _selected = (int)_values.size() - 1;
}

void GameOptions::Option::setValueAvailableInFreeVersion(int value)
{
    for (OptionValue& v : _values) {
        if (v.getValue() == value) {
            v.setAvailableInFreeVersion(true);
            return;
        }
    }
}

void GameOptions::Option::setValuesInRangeAvailableInFreeVersion(int from, int to)
{
    for (OptionValue& v : _values)
        if (v.getValue() >= from && v.getValue() <= to)
            v.setAvailableInFreeVersion(true);
}

void GameOptions::Option::updateValueAvailability(bool fullVersion)
{
    for (OptionValue& v : _values)
        v.updateValueAvailability(fullVersion);
}

void GameOptions::Option::removeUnavailableValues()
{
    for (size_t i = 0; i < _values.size();) {
        if (!_values[i].getAvailable()) {
            _values.erase(_values.begin() + i);
            if ((int)i < _selected)
                _selected--;
        } else {
            i++;
        }
    }
}

GameOptions::GameOptions()
{
    _numCivs.setLabel("Num civilians");
    _armedCivs.setLabel("Armed civilians");
    _civAccuracy.setLabel("Civilian accuracy");
    _numZombies.setLabel("Num zombies");
    _zombieOutbreak.setLabel("Zombie outbreak");
    _zombieSpeed.setLabel("Zombie speed");
    _infectionTime.setLabel("Infection time");
    _numPolice.setLabel("Num police");
    _policeAccuracy.setLabel("Police accuracy");
    _soldierAccuracy.setLabel("Soldier accuracy");
    _options = {&_numCivs,       &_armedCivs,     &_civAccuracy, &_numZombies,     &_zombieOutbreak,
                &_zombieSpeed,   &_infectionTime, &_numPolice,   &_policeAccuracy, &_soldierAccuracy};
}

void GameOptions::populateValues(int width, int /*height*/, ZombieOutbreak outbreak, int maxZombies)
{
    const bool large = width == 4000 || width == 4096 || width == 8000;
    const bool small = width == 2000 || width == 2048;
    if (!large && !small)
        throw std::runtime_error("Unexpected playing area width " + Utils::intToString(width));

    // Civilians: up to 3000 on a small map, 4000 on a large; 500 picked, up
    // to 1000 free.
    _numCivs.clearValues();
    for (int v : {100, 200, 500, 1000, 1500, 2000, 2500, 3000})
        _numCivs.addValue(OptionValue(v));
    if (large) {
        _numCivs.addValue(OptionValue(3500));
        _numCivs.addValue(OptionValue(4000));
    }
    _numCivs.setSelectedValueRequired(500);
    _numCivs.setValueAvailableInFreeVersion(_numCivs.getSelectedValue());
    _numCivs.setValuesInRangeAvailableInFreeVersion(0, 1000);

    _armedCivs.clearValues();
    _armedCivs.addPercentSequence(0, 100, 10);
    _armedCivs.setSelectedValueRequired(10);
    _armedCivs.setValueAvailableInFreeVersion(_armedCivs.getSelectedValue());
    _armedCivs.setValuesInRangeAvailableInFreeVersion(0, 20);

    _civAccuracy.clearValues();
    _civAccuracy.addPercentSequence(0, 100, 10);
    _civAccuracy.setSelectedValueRequired(10);
    _civAccuracy.setValueAvailableInFreeVersion(_civAccuracy.getSelectedValue());
    _civAccuracy.setValuesInRangeAvailableInFreeVersion(0, 20);

    // Zombies: 200 picked on a large map, 100 on a small; no more than the
    // map allows.
    _numZombies.clearValues();
    for (int v : {5, 10, 20, 50, 100, 200, 500})
        _numZombies.addValue(OptionValue(v));
    if (large)
        _numZombies.addValue(OptionValue(1000));
    _numZombies.setSelectedValueRequired(large ? 200 : 100);
    _numZombies.removeValuesGreaterThan(maxZombies);
    _numZombies.setValueAvailableInFreeVersion(_numZombies.getSelectedValue());
    _numZombies.setValuesInRangeAvailableInFreeVersion(50, 500);

    _zombieOutbreak.clearValues();
    _zombieOutbreak.addValue(OptionValue(ZombieOutbreakNorth, "North"));
    _zombieOutbreak.addValue(OptionValue(ZombieOutbreakEast, "East"));
    _zombieOutbreak.addValue(OptionValue(ZombieOutbreakSouth, "South"));
    _zombieOutbreak.addValue(OptionValue(ZombieOutbreakWest, "West"));
    _zombieOutbreak.addValue(OptionValue(ZombieOutbreakMapWide, "Map Wide"));
    _zombieOutbreak.setSelectedValueRequired(outbreak);
    _zombieOutbreak.setValueAvailableInFreeVersion(_zombieOutbreak.getSelectedValue());
    _zombieOutbreak.setValueAvailableInFreeVersion(ZombieOutbreakNorth);
    _zombieOutbreak.setValueAvailableInFreeVersion(ZombieOutbreakEast);

    _zombieSpeed.clearValues();
    _zombieSpeed.addValue(OptionValue(ZombieSpeedVerySlow, "Very Slow"));
    _zombieSpeed.addValue(OptionValue(ZombieSpeedSlow, "Slow"));
    _zombieSpeed.addValue(OptionValue(ZombieSpeedNormal, "Normal"));
    _zombieSpeed.addValue(OptionValue(ZombieSpeedFast, "Fast"));
    _zombieSpeed.addValue(OptionValue(ZombieSpeedVeryFast, "Very Fast"));
    _zombieSpeed.setSelectedValueRequired(ZombieSpeedNormal);
    _zombieSpeed.setValueAvailableInFreeVersion(_zombieSpeed.getSelectedValue());
    _zombieSpeed.setValueAvailableInFreeVersion(ZombieSpeedSlow);
    _zombieSpeed.setValueAvailableInFreeVersion(ZombieSpeedNormal);

    _infectionTime.clearValues();
    _infectionTime.addValue(OptionValue(1, "1 sec"));
    _infectionTime.addValue(OptionValue(5, "5 sec"));
    _infectionTime.addValue(OptionValue(10, "10 sec"));
    _infectionTime.addValue(OptionValue(30, "30 sec"));
    _infectionTime.addValue(OptionValue(60, "1 min"));
    _infectionTime.addValue(OptionValue(120, "2 min"));
    _infectionTime.addValue(OptionValue(180, "3 min"));
    _infectionTime.addValue(OptionValue(300, "5 min"));
    _infectionTime.setSelectedValueRequired(10);
    _infectionTime.setValueAvailableInFreeVersion(_infectionTime.getSelectedValue());
    _infectionTime.setValueAvailableInFreeVersion(10);
    _infectionTime.setValueAvailableInFreeVersion(30);
    _infectionTime.setValueAvailableInFreeVersion(60);

    _numPolice.clearValues();
    _numPolice.addIntegerSequence(0, 10, 1);
    _numPolice.addIntegerSequence(15, 50, 5);
    _numPolice.setSelectedValueRequired(4);
    _numPolice.setValueAvailableInFreeVersion(_numPolice.getSelectedValue());
    _numPolice.setValuesInRangeAvailableInFreeVersion(2, 6);

    _policeAccuracy.clearValues();
    _policeAccuracy.addPercentSequence(0, 100, 10);
    _policeAccuracy.setSelectedValueRequired(30);
    _policeAccuracy.setValueAvailableInFreeVersion(_policeAccuracy.getSelectedValue());
    _policeAccuracy.setValuesInRangeAvailableInFreeVersion(0, 30);

    _soldierAccuracy.clearValues();
    _soldierAccuracy.addPercentSequence(0, 100, 10);
    _soldierAccuracy.setSelectedValueRequired(80);
    _soldierAccuracy.setValueAvailableInFreeVersion(_soldierAccuracy.getSelectedValue());
    _soldierAccuracy.setValuesInRangeAvailableInFreeVersion(60, 80);
}

GameOptions::Snapshot GameOptions::getSnapshot() const
{
    return Snapshot(_numCivs.getSelectedValue(), _armedCivs.getSelectedValue(), _civAccuracy.getSelectedValue(),
                    _numZombies.getSelectedValue(), (ZombieOutbreak)_zombieOutbreak.getSelectedValue(),
                    (ZombieSpeed)_zombieSpeed.getSelectedValue(), _infectionTime.getSelectedValue(),
                    _numPolice.getSelectedValue(), _policeAccuracy.getSelectedValue(),
                    _soldierAccuracy.getSelectedValue());
}

void GameOptions::writeToSaveFile(FILE* f)
{
    for (Option* o : _options)
        Utils::writeInt(f, o->getSelectedValue());
}

void GameOptions::readFromSaveFile(FILE* f)
{
    for (Option* o : _options)
        o->setSelectedValue(Utils::readInt(f));
}

void GameOptions::updateValueAvailability(bool fullVersion)
{
    for (Option* o : _options)
        o->updateValueAvailability(fullVersion);
}

bool GameOptions::allSelectedValuesAvailable()
{
    for (Option* o : _options)
        if (o->getNumValues() > 0 && !o->selectedValueAvailable())
            return false;
    return true;
}

void GameOptions::removeUnavailableValues()
{
    for (Option* o : _options)
        o->removeUnavailableValues();
}
