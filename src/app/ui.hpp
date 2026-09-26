#pragma once
#include "api.hpp"
#include "sim/params.hpp"
#include <imgui.h>

struct UIState {
    SimParams sim;
    int joltIters = 10;
    int clothSteps = 3;
    float bgColor[3] = {0.10f, 0.11f, 0.15f};
    bool showDemo = false;
};

void drawOverlay(UIState& ui, Camera& cam, bool clothPinned, bool& clothReset, bool& ballReset);
