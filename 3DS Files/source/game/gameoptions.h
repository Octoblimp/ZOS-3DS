// GameOptions (decomp/classes/GameOptions.c, GameOptions__Option.c,
// GameOptions__OptionValue.c, GameOptions__Snapshot.c): the settings a game
// starts with, as SettingsMenu shows them. Ten options, each a list of
// values and the one picked; Snapshot is the values picked, which
// Simulation reads.
//
// The free version offered some values only (those "available in the free
// version"); the rest were bought. The port has everything unlocked, so
// updateValueAvailability(true) is what it calls.
#pragma once

#include <cstdio>
#include <string>
#include <vector>

class GameOptions {
public:
    // Where the zombies come in: along one edge, or anywhere at once.
    enum ZombieOutbreak {
        ZombieOutbreakNorth,
        ZombieOutbreakEast,
        ZombieOutbreakSouth,
        ZombieOutbreakWest,
        ZombieOutbreakMapWide,
    };

    enum ZombieSpeed {
        ZombieSpeedVerySlow,
        ZombieSpeedSlow,
        ZombieSpeedNormal,
        ZombieSpeedFast,
        ZombieSpeedVeryFast,
    };

    // 0x0c bytes: a value, its text, and whether it can be picked.
    class OptionValue {
    public:
        explicit OptionValue(int value);
        OptionValue(int value, const std::string& formatted);
        int getValue() const { return _value; }
        const std::string& getFormattedValue() const { return _formatted; }
        void setAvailableInFreeVersion(bool available) { _availableInFreeVersion = available; }
        void updateValueAvailability(bool fullVersion)
        {
            _available = fullVersion ? true : _availableInFreeVersion;
        }
        bool getAvailable() const { return _available; }

    private:
        int _value;                             // 0x00
        std::string _formatted;                 // 0x04
        bool _availableInFreeVersion = false;   // 0x08
        bool _available = false;                // 0x09
    };

    // 0x18 bytes.
    class Option {
    public:
        const std::string& getLabel() const { return _label; }
        void setLabel(const std::string& label) { _label = label; }
        void addValue(const OptionValue& value);
        // from, from + step, ... up to to; as numbers, or "N%".
        void addIntegerSequence(int from, int to, int step);
        void addPercentSequence(int from, int to, int step);
        const std::vector<OptionValue>& getValues() const { return _values; }
        int getNumValues() const { return (int)_values.size(); }
        const OptionValue& getValue(int index) const { return _values[index]; }
        // Throws "Invalid selected index for option <label>".
        int getSelectedValue() const;
        // False if no value is equal.
        bool setSelectedValue(int value);
        // Throws "Option <label> does not contain a value equal to <value>".
        void setSelectedValueRequired(int value);
        int getSelectedValueIndex() const { return _selected; }
        void setSelectedValueIndex(int index) { _selected = index; }
        // The available value nearest, the later of two as near.
        void setSelectedValueClosestAvailable(int value);
        // In characters, for the menu's column.
        int getLongestFormattedValueLength() const { return _longest; }
        void clearValues();
        // From the end, while the last is greater.
        void removeValuesGreaterThan(int value);
        void setValueAvailableInFreeVersion(int value);
        void setValuesInRangeAvailableInFreeVersion(int from, int to);
        void updateValueAvailability(bool fullVersion);
        bool selectedValueAvailable() const { return _values[_selected].getAvailable(); }
        void removeUnavailableValues();

    private:
        std::string _label;                 // 0x00
        std::vector<OptionValue> _values;   // 0x04
        int _selected = 0;                  // 0x10
        int _longest = 0;                   // 0x14
    };

    // 0x28 bytes. Accuracies and armedCivs are percentages; infectionTime is
    // in seconds.
    struct Snapshot {
        Snapshot() = default;
        Snapshot(int numCivs_, int armedCivs_, int civAccuracy_, int numZombies_, ZombieOutbreak zombieOutbreak_,
                 ZombieSpeed zombieSpeed_, int infectionTime_, int numPolice_, int policeAccuracy_,
                 int soldierAccuracy_)
            : numCivs(numCivs_), armedCivs(armedCivs_), civAccuracy(civAccuracy_), numZombies(numZombies_),
              zombieOutbreak(zombieOutbreak_), zombieSpeed(zombieSpeed_), infectionTime(infectionTime_),
              numPolice(numPolice_), policeAccuracy(policeAccuracy_), soldierAccuracy(soldierAccuracy_)
        {
        }

        int numCivs = 0;                                        // 0x00
        int armedCivs = 0;                                      // 0x04
        int civAccuracy = 0;                                    // 0x08
        int numZombies = 0;                                     // 0x0c
        ZombieOutbreak zombieOutbreak = ZombieOutbreakNorth;    // 0x10
        ZombieSpeed zombieSpeed = ZombieSpeedNormal;            // 0x14
        int infectionTime = 0;                                  // 0x18
        int numPolice = 0;                                      // 0x1c
        int policeAccuracy = 0;                                 // 0x20
        int soldierAccuracy = 0;                                // 0x24
    };

    GameOptions();
    GameOptions(const GameOptions&) = delete;
    GameOptions& operator=(const GameOptions&) = delete;

    const std::vector<Option*>& getOptions() const { return _options; }
    int getNumOptions() const { return (int)_options.size(); }
    Option* getOption(int index) const { return _options[index]; }
    Option& getOptionNumCivs() { return _numCivs; }
    Option& getOptionArmedCivs() { return _armedCivs; }
    Option& getOptionCivAccuracy() { return _civAccuracy; }
    Option& getOptionNumZombies() { return _numZombies; }
    Option& getOptionZombieOutbreak() { return _zombieOutbreak; }
    Option& getOptionZombieSpeed() { return _zombieSpeed; }
    Option& getOptionInfectionTime() { return _infectionTime; }
    Option& getOptionNumPolice() { return _numPolice; }
    Option& getOptionPoliceAccuracy() { return _policeAccuracy; }
    Option& getOptionSoldierAccuracy() { return _soldierAccuracy; }

    // The values for a map: its width (2000/2048 or 4000/4096/8000) sets how
    // many people there can be; outbreak is the map's preset, and no more
    // zombies than maxZombies are offered. Throws "Unexpected playing area
    // width N" or "Unexpected map size".
    void populateValues(int width, int height, ZombieOutbreak outbreak, int maxZombies);
    Snapshot getSnapshot() const;
    // The ten values picked, as ints.
    void writeToSaveFile(FILE* f);
    // Each value read is picked if the option has it.
    void readFromSaveFile(FILE* f);
    void updateValueAvailability(bool fullVersion);
    bool allSelectedValuesAvailable();
    void removeUnavailableValues();

private:
    Option _numCivs;            // 0x00
    Option _armedCivs;          // 0x18
    Option _civAccuracy;        // 0x30
    Option _numZombies;         // 0x48
    Option _zombieOutbreak;     // 0x60
    Option _zombieSpeed;        // 0x78
    Option _infectionTime;      // 0x90
    Option _numPolice;          // 0xa8
    Option _policeAccuracy;     // 0xc0
    Option _soldierAccuracy;    // 0xd8
    std::vector<Option*> _options;  // 0xf0
};
