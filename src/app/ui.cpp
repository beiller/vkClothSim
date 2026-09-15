#include "app/ui.hpp"

void drawOverlay(UIState& ui, bool clothPinned, bool& clothReset, bool& ballReset) {
    clothReset = false;
    ballReset = false;
    ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(320, 410), ImGuiCond_Always);
    ImGui::Begin("3dsim");
    ImGui::ColorEdit3("background", ui.bgColor);
    ImGui::Text("%.1f fps", ImGui::GetIO().Framerate);
    ImGui::Text("cloth: %s", clothPinned ? "held (pinned)" : "falling");
    ImGui::Text("sim: GPU (cloth + ball)");
    ImGui::SeparatorText("soft body sim (cloth + ball)");
    ImGui::SliderFloat("mass", &ui.sim.mass, 0.1f, 10.0f, "%.2f");
    ImGui::SliderFloat("damping", &ui.sim.damping, 0.90f, 1.00f, "%.3f");
    ImGui::SliderInt("passes", &ui.sim.passes, 1, 16);
    ImGui::SliderFloat("stiffness", &ui.sim.stiffness, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("tension", &ui.sim.tension, 0.5f, 1.5f, "%.2f");
    ImGui::SliderFloat("friction", &ui.sim.friction, 0.0f, 50.0f, "%.2f");
    ImGui::SeparatorText("cloth substeps / frame");
    ImGui::SliderInt("cloth", &ui.clothSteps, 1, 32);
    ImGui::SeparatorText("jolt iterations");
    ImGui::SliderInt("velocity", &ui.joltIters, 1, 64);
    if (ImGui::Button("reset params"))
        ui.sim = SimParams{};
    if (ImGui::Button("reset ball"))
        ballReset = true;
    if (ImGui::Button("reset (R)"))
        clothReset = true;
    if (ImGui::Button("demo window"))
        ui.showDemo = true;
    ImGui::End();
    if (ui.showDemo)
        ImGui::ShowDemoWindow(&ui.showDemo);
}
