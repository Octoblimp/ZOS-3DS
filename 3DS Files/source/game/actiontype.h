// ActionType: what a tap on the map does (GUIScene's 0x29c; the values from
// GameNode::performAction and GUIScene::onTouchesEnded). The game's names
// are not in the binary.
#pragma once

namespace ActionType {

enum ActionType {
    Soldiers = 1,
    // The small bomb, and the large.
    Mk81Bombs = 2,
    Mk82Bombs = 3,
    Helicopters = 4,
    // No action picked (GUIScene::create, exitAction): a tap does nothing.
    None = 5,
};

} // namespace ActionType
