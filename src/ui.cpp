#include "ui.hpp"

#include "vk/vkapp.hpp"
#include <algorithm>
#include <cstdio>
#include <functional>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <string>
#include <unordered_map>
#include <vector>

entt::entity spawnUiWindow(World& w, const UIWindow& cfg) {
    entt::entity e = w.reg.create();
    UIWindow c = cfg;
    c.order = w.uiSeq++;
    w.reg.emplace<UIWindow>(e, c);
    return e;
}

void spawnFieldWidget(World& w, entt::entity parent, const char* label, const FieldDesc* f, void* base) {
    char* p = (char*)base + f->offset;
    switch (f->kind) {
    case FieldKind::SliderF:
        spawnWidget(w, parent, UISliderF{label, f->lo, f->hi, "%.2f", [p] { return (float*)p; }});
        break;
    case FieldKind::SliderI: {
        int* ip = (int*)p;
        spawnWidget(w, parent, UISliderI{label, (int)f->lo, (int)f->hi, [ip] { return ip; }});
        break;
    }
    case FieldKind::Checkbox: {
        bool* bp = (bool*)p;
        spawnWidget(w, parent, UICheckbox{label, [bp] { return bp; }});
        break;
    }
    case FieldKind::Color:
        spawnWidget(w, parent, UIColorEdit{label, [p] { return (float*)p; }});
        break;
    case FieldKind::DragV3:
        spawnWidget(w, parent, UIDragV3{label, 0.1f, [p] { return (float*)p; }});
        break;
    case FieldKind::Ref: {
        // read-only: show the referenced entity's name (re-read each frame)
        const entt::entity target = *(const entt::entity*)p;
        spawnWidget(w, parent, UIText{label, [&w, target] {
                                          const auto* n = w.reg.try_get<Name>(target);
                                          return n ? n->id : "<none>";
                                      }});
        break;
    }
    }
}

void spawnFpsHud(World& w) {
    // app-level fps HUD, also a UI window entity
    const entt::entity fpsWin =
        spawnUiWindow(w, UIWindow{"##fps",
                                  [&w] {
                                      const VkExtent2D ext = w.app->extent();
                                      return V2{(float)ext.width - 120.0f, 8.0f};
                                  },
                                  V2{112.0f, 26.0f},
                                  ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus});
    spawnWidget(w, fpsWin, UIText{"", [&w] {
                                      char buf[32];
                                      std::snprintf(buf, sizeof(buf), "%.0f fps", w.fps);
                                      return std::string(buf);
                                  }});
}

void initImGui(VkApp& app) {
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForVulkan(app.glfwWindow(), true);
    ImGui_ImplVulkan_InitInfo ii{};
    ii.ApiVersion = VK_API_VERSION_1_1;
    ii.Instance = app.instance();
    ii.PhysicalDevice = app.pdev();
    ii.Device = app.device();
    ii.QueueFamily = app.queueFamily();
    ii.Queue = app.queue();
    ii.DescriptorPoolSize = 1024;
    ii.MinImageCount = app.imageCount();
    ii.ImageCount = app.imageCount();
    ii.PipelineInfoMain.RenderPass = app.renderPass();
    ii.PipelineInfoMain.Subpass = 0;
    ii.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    ii.CheckVkResultFn = [](VkResult err) {
        if (err != VK_SUCCESS)
            std::printf("ImGui VK err %d\n", (int)err);
    };
    ImGui_ImplVulkan_Init(&ii);
}

void shutdownImGui() {
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void imguiNewFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void drawUi(World& w) {
    auto& reg = w.reg;
    std::unordered_map<entt::entity, std::vector<entt::entity>> kids;
    for (auto [c, wg, p] : reg.view<UIWidget, Parent>().each()) {
        (void)wg;
        kids[p.e].push_back(c);
    }
    auto orderKids = [&](entt::entity parent) {
        auto it = kids.find(parent);
        if (it == kids.end())
            return std::vector<entt::entity>{};
        auto v = it->second;
        std::sort(v.begin(), v.end(), [&](entt::entity a, entt::entity b) {
            return reg.get<UIWidget>(a).order < reg.get<UIWidget>(b).order;
        });
        return v;
    };
    std::function<void(entt::entity)> drawWidget;
    std::function<void(entt::entity)> drawKids;
    drawWidget = [&](entt::entity e) {
        // scope the widget's ID to its entity; sibling sections reuse labels ("metallic", "on", ...)
        ImGui::PushID((int)e);
        if (const auto* s = reg.try_get<UISliderF>(e))
            ImGui::SliderFloat(s->label.c_str(), s->value(), s->lo, s->hi, s->fmt.c_str());
        else if (const auto* s = reg.try_get<UISliderI>(e))
            ImGui::SliderInt(s->label.c_str(), s->value(), s->lo, s->hi);
        else if (const auto* c = reg.try_get<UICheckbox>(e))
            ImGui::Checkbox(c->label.c_str(), c->value());
        else if (const auto* c = reg.try_get<UIColorEdit>(e))
            ImGui::ColorEdit3(c->label.c_str(), c->value());
        else if (const auto* d = reg.try_get<UIDragV3>(e))
            ImGui::DragFloat3(d->label.c_str(), d->value(), d->speed);
        else if (const auto* t = reg.try_get<UIText>(e)) {
            if (t->text)
                ImGui::TextUnformatted(t->text().c_str());
            else
                ImGui::TextUnformatted(t->label.c_str());
        } else if (const auto* b = reg.try_get<UIButton>(e)) {
            if (ImGui::Button(b->label.c_str()))
                b->on();
        } else if (const auto* sec = reg.try_get<UISection>(e)) {
            if (sec->collapsible) {
                if (ImGui::CollapsingHeader(sec->label.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                    drawKids(e);
            } else {
                ImGui::SeparatorText(sec->label.c_str());
                drawKids(e);
            }
        }
        ImGui::PopID();
    };
    drawKids = [&](entt::entity parent) {
        const auto v = orderKids(parent);
        // content pass: every non-tab widget of this level
        for (entt::entity e : v)
            if (!reg.try_get<UITab>(e))
                drawWidget(e);
        // tab pass: tab children form this level's tab bar (convention), drawn after the content.
        // BeginTabBar/BeginTabItem manage their own ID pushes, so tabs are not PushID-scoped
        // (wrapping them would make the tab ID depend on selection); EndTabItem must be called
        // only for opened items.
        bool barOpen = false;
        for (entt::entity e : v) {
            const auto* tab = reg.try_get<UITab>(e);
            if (!tab)
                continue;
            if (!barOpen) {
                ImGui::BeginTabBar("##tabs");
                barOpen = true;
            }
            if (ImGui::BeginTabItem(tab->label.c_str())) {
                drawKids(e);
                ImGui::EndTabItem();
            }
        }
        if (barOpen)
            ImGui::EndTabBar();
    };
    std::vector<entt::entity> wins;
    reg.view<UIWindow>().each([&](entt::entity e, auto&) { wins.push_back(e); });
    std::sort(wins.begin(), wins.end(),
              [&](entt::entity a, entt::entity b) { return reg.get<UIWindow>(a).order < reg.get<UIWindow>(b).order; });
    for (entt::entity win : wins) {
        const UIWindow& cfg = reg.get<UIWindow>(win);
        if (cfg.visible && !cfg.visible())
            continue;
        // NoMove windows (like the fps HUD) track their pos every frame; others are placed once and
        // can be moved/resized freely (state persists in imgui.ini)
        const bool pin = cfg.flags & ImGuiWindowFlags_NoMove;
        ImGui::SetNextWindowPos(ImVec2(cfg.pos().x, cfg.pos().y), pin ? ImGuiCond_Always : ImGuiCond_Appearing);
        if (cfg.size.x > 0.0f && cfg.size.y > 0.0f)
            ImGui::SetNextWindowSize(ImVec2(cfg.size.x, cfg.size.y), ImGuiCond_Appearing);
        if (cfg.collapseOnAppear)
            ImGui::SetNextWindowCollapsed(true, ImGuiCond_Appearing);
        ImGui::Begin(cfg.title.c_str(), nullptr, cfg.flags);
        drawKids(win);
        ImGui::End();
    }
}
