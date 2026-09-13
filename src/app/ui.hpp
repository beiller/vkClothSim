#pragma once
#include "sim/params.hpp"
#include <imgui.h>

struct UIState {
    SimParams sim;
    float bgColor[3] = {0.10f, 0.11f, 0.15f};
    bool showDemo = false;
};

void drawOverlay(UIState& ui, bool clothPinned, bool& clothReset, bool& ballReset);
