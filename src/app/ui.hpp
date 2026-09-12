// ui.hpp
// The Dear ImGui overlay (the HUD + the controls). Decoupled from the renderer (Vulkan)
// and the physics: it only depends on ImGui + the UIState (the user-tunable parameters).
#pragma once
#include "app/params.hpp" // SimParams (the user-tunable soft-body sim params)
#include <imgui.h>

// The user-tunable parameters: the shared soft-body sim params (cloth + ball) + the scene
// options. Read by the renderer (the sim params + the background color) each frame; written
// by the ImGui overlay (drawOverlay).
struct UIState {
    SimParams sim; // the shared soft-body sim params (applied to cloth + ball)
    float bgColor[3] = {0.10f, 0.11f, 0.15f};
    bool showDemo = false;
};

// The ImGui overlay (the HUD + the controls). Draw into the current ImGui frame (after
// ImGui::NewFrame). `clothPinned` is the scene state (for the HUD); `clothReset` /
// `ballReset` are set when the user clicks the matching button.
void drawOverlay(UIState& ui, bool clothPinned, bool& clothReset, bool& ballReset);
