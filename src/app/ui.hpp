// ui.hpp
// The Dear ImGui overlay (the HUD + the controls). Decoupled from the renderer (Vulkan)
// and the physics: it only depends on ImGui + the UIState (the user-tunable parameters).
#pragma once
#include <imgui.h>

// The cloth's sim parameters (tuned in real time via the UI; read by stepCloth each step).
struct ClothSim {
    float mass = 1.0f;
    float damping = 0.999f;
    int stiffness = 4;
    float tension = 1.0f;
};

// The user-tunable parameters: the cloth's sim params + the ball's pressure + the scene
// options. Read by the physics (stepCloth/stepBall) + the renderer (the background color)
// each frame; written by the ImGui overlay (drawOverlay).
struct UIState {
    ClothSim cloth;
    float ballPressure = 1.0f;   // 1 = rest, >1 inflate, <1 deflate
    float bgColor[3] = {0.10f, 0.11f, 0.15f};
    bool showDemo = false;
};

// The ImGui overlay (the HUD + the controls). Draw into the current ImGui frame (after
// ImGui::NewFrame). `clothPinned` + `ballVolumeRatio` are the scene state (for the HUD);
// `clothReset` / `ballReset` are set when the user clicks the matching button.
void drawOverlay(UIState& ui, bool clothPinned, float ballVolumeRatio, bool& clothReset, bool& ballReset);
