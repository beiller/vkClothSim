// ui.cpp
#include "app/ui.hpp"

void drawOverlay(UIState& ui, bool clothPinned, float ballVolumeRatio, bool& clothReset, bool& ballReset) {
    clothReset = false;
    ballReset = false;
    ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(320, 240), ImGuiCond_Always);
    ImGui::Begin("3dsim");
    ImGui::ColorEdit3("background", ui.bgColor);
    ImGui::Text("%.1f fps", ImGui::GetIO().Framerate);
    ImGui::Text("cloth: %s", clothPinned ? "held (pinned)" : "falling");
    ImGui::SeparatorText("cloth sim");
    ImGui::SliderFloat("mass", &ui.cloth.mass, 0.1f, 10.0f, "%.2f");
    ImGui::SliderFloat("damping", &ui.cloth.damping, 0.90f, 1.00f, "%.3f");
    ImGui::SliderInt("stiffness", &ui.cloth.stiffness, 1, 16);
    ImGui::SliderFloat("tension", &ui.cloth.tension, 0.5f, 1.5f, "%.2f");
    if (ImGui::Button("reset params"))
        ui.cloth = ClothSim{};
    {   // ball (falls under gravity, lands on the ground, pressure expand/deflates)
        ImGui::SeparatorText("ball (falls + pressure)");
        ImGui::SliderFloat("pressure", &ui.ballPressure, 0.2f, 2.0f, "%.2f");
        ImGui::Text("ball volume: %.2f x rest", ballVolumeRatio);
        if (ImGui::Button("reset ball"))
            ballReset = true;
    }
    if (ImGui::Button("reset (R)"))
        clothReset = true;
    if (ImGui::Button("demo window"))
        ui.showDemo = true;
    ImGui::End();
    if (ui.showDemo)
        ImGui::ShowDemoWindow(&ui.showDemo);
}
