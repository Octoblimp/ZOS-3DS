// The GUI's listener interfaces: how the menus tell GUIScene (and
// ControlsMenu) what was picked. Their order is the game's vtables' (the
// menus call +8 and +0xc).
#pragma once

#include "game/actiontype.h"

// GUIScene, from ActionsMenu and WeaponsMenu.
class ActionSelectedListener {
public:
    virtual ~ActionSelectedListener() = default;
    // The panel slid out (open) or back.
    virtual void onActionsMenuToggled(bool open) = 0;
    virtual void onActionSelected(ActionType::ActionType action) = 0;
};

// GUIScene, from ControlsMenu.
class ControlSelectedListener {
public:
    virtual ~ControlSelectedListener() = default;
    // The settings panel shown or hidden.
    virtual void onSettingsSelected(bool visible) = 0;
    virtual void onSettingsRestart() = 0;
};

// ControlsMenu, from SettingsMenu.
class SettingsSelectedListener {
public:
    enum SettingsType {
        Restart = 0,
    };
    virtual ~SettingsSelectedListener() = default;
    virtual void onSettingsSelected(SettingsType type) = 0;
};
