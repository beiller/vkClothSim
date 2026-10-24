#include "ui.hpp"

#include <cstdio>
#include "reflect.hpp"
#include <algorithm>
#include <imgui_impl_vulkan.h>
#include <unordered_map>

namespace {
struct UiCtx {
    ImGuiContext* ctx = nullptr;
    V2 px{};
    VkRenderPass rp = VK_NULL_HANDLE;
    VkFramebuffer fb = VK_NULL_HANDLE;
};

UiHandles g_h;
std::unordered_map<entt::entity, UiCtx> g_ctxs;
bool g_demoOpen = false;

entt::entity canvasWindow(const entt::registry& reg, entt::entity canvas) {
    for (auto [e, w] : reg.view<UIWindow>().each())
        if (w.canvas == canvas)
            return e;
    return entt::null;
}
} // namespace

void uiInit(const UiHandles& h) {
    g_h = h;
}

void uiShutdown() {
    for (auto& [e, c] : g_ctxs) {
        (void)e;
        ImGui::SetCurrentContext(c.ctx);
        ImGui_ImplVulkan_Shutdown();
        ImGui::DestroyContext();
        c.ctx = nullptr;
    }
    g_ctxs.clear();
}

void uiEnsureContext(const entt::registry& reg, entt::entity canvas, VkRenderPass rp, VkFramebuffer fb) {
    const entt::entity win = canvasWindow(reg, canvas);
    const V2 px = win != entt::null ? reg.get<UIWindow>(win).size : V2{320.0f, 200.0f};
    auto& c = g_ctxs[canvas];
    if (c.ctx)
        return;
    c.ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(c.ctx);
    ImGui::StyleColorsDark();
    c.px = px;
    c.rp = rp;
    c.fb = fb;
    ImGui_ImplVulkan_InitInfo ii{};
    ii.ApiVersion = VK_API_VERSION_1_1;
    ii.Instance = g_h.inst;
    ii.PhysicalDevice = g_h.pdev;
    ii.Device = g_h.dev;
    ii.QueueFamily = g_h.queueFamily;
    ii.Queue = g_h.queue;
    ii.DescriptorPoolSize = 64;
    ii.MinImageCount = (uint32_t)std::max(2, g_h.imageCount);
    ii.ImageCount = (uint32_t)std::max(2, g_h.imageCount);
    ii.PipelineInfoMain.RenderPass = rp;
    ii.PipelineInfoMain.Subpass = 0;
    ii.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    ii.CheckVkResultFn = [](VkResult err) {
        if (err != VK_SUCCESS)
            std::printf("ImGui VK err %d\n", (int)err);
    };
    ImGui_ImplVulkan_Init(&ii);
}

void uiFrame(const entt::registry& reg, const std::vector<UiFeed>& feeds) {
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
                if (ImGui::CollapsingHeader(sec->label.c_str()))
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
        for (entt::entity e : v)
            if (!reg.try_get<UITab>(e))
                drawWidget(e);
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
    for (const UiFeed& f : feeds) {
        auto it = g_ctxs.find(f.canvas);
        if (it == g_ctxs.end())
            continue;
        UiCtx& c = it->second;
        ImGui::SetCurrentContext(c.ctx);
        ImGui::GetIO().DisplaySize = ImVec2(c.px.x, c.px.y);
        ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, f.down);
        if (f.wheel != 0.0f)
            ImGui::GetIO().AddMouseWheelEvent(0.0f, f.wheel);
        ImGui::GetIO().AddMousePosEvent(f.pos.x, f.pos.y); // queued last, wins
        ImGui_ImplVulkan_NewFrame();
        ImGui::NewFrame();
        const entt::entity win = canvasWindow(reg, f.canvas);
        if (win != entt::null) {
            const UIWindow& cfg = reg.get<UIWindow>(win);
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(c.px.x, c.px.y), ImGuiCond_Always);
            ImGui::Begin(cfg.title.c_str(), nullptr, cfg.flags);
            drawKids(win);
            ImGui::End();
        }
        if (f.demo)
            ImGui::ShowDemoWindow(&g_demoOpen);
        ImGui::Render();
    }
}

void uiRender(entt::entity canvas, VkCommandBuffer cmd) {
    auto it = g_ctxs.find(canvas);
    if (it == g_ctxs.end())
        return;
    UiCtx& c = it->second;
    if (!c.fb || !c.rp || !g_h.canvasImage(canvas))
        return;
    ImGui::SetCurrentContext(c.ctx);
    VkImageMemoryBarrier bar{};
    bar.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    bar.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    bar.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    bar.srcQueueFamilyIndex = bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bar.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    bar.image = g_h.canvasImage(canvas);
    bar.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    bar.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &bar);
    VkClearValue bcv = {{0.0f, 0.0f, 0.0f, 0.0f}};
    VkRenderPassBeginInfo bpb{};
    bpb.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    bpb.renderPass = c.rp;
    bpb.framebuffer = c.fb;
    bpb.renderArea = {{0, 0}, {(uint32_t)c.px.x, (uint32_t)c.px.y}};
    bpb.clearValueCount = 1;
    bpb.pClearValues = &bcv;
    vkCmdBeginRenderPass(cmd, &bpb, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport vpt{0, (float)c.px.y, (float)c.px.x, -(float)c.px.y, 0, 1};
    vkCmdSetViewport(cmd, 0, 1, &vpt);
    VkRect2D sc{0, 0, (uint32_t)c.px.x, (uint32_t)c.px.y};
    vkCmdSetScissor(cmd, 0, 1, &sc);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    vkCmdEndRenderPass(cmd);
    bar.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    bar.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    bar.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    bar.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &bar);
}

namespace {
Mesh quadMesh(const V2& ws) {
    Mesh m;
    const float x = ws.x * 0.5f, y = ws.y * 0.5f;
    m.pos = {-x, -y, 0.0f, x, -y, 0.0f, -x, y, 0.0f, x, y, 0.0f};
    m.nrm = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    m.uv = {0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    m.indices = {0, 1, 2, 2, 1, 3};
    return m;
}

entt::entity spawnUiCanvas(entt::registry& reg, int& seq, const std::string& title, const V2& px, const V3& pos,
                           int flags) {
    const V2 ws{px.x * 0.0022f, px.y * 0.0022f}; // uniform m/px, preserves aspect
    const entt::entity canvas = reg.create();
    reg.emplace<UICanvas>(canvas, UICanvas{ws});
    reg.emplace<GPUTexture>(canvas, GPUTexture{(uint32_t)px.x, (uint32_t)px.y});
    reg.emplace<Transform>(canvas, Transform{pos});
    const entt::entity geom = reg.create();
    reg.emplace<Geometry>(geom, Geometry{quadMesh(ws)});
    const entt::entity shape = reg.create();
    reg.emplace<BoxShape>(shape, BoxShape{V3{ws.x * 0.5f, ws.y * 0.5f, 0.01f}});
    const entt::entity col = reg.create();
    reg.emplace<Collider>(col, Collider{shape});
    reg.emplace<Renderable>(canvas, geom, true);
    const entt::entity matEnt = reg.create();
    Material m;
    m.tex = canvas;
    m.alphaMode = 2;
    reg.emplace<Material>(matEnt, m);
    reg.emplace<MaterialRef>(canvas, matEnt);
    reg.emplace<KinematicBody>(canvas, KinematicBody{{col}});
    const entt::entity win = reg.create();
    UIWindow w;
    w.title = title;
    w.canvas = canvas;
    w.size = px;
    w.flags = flags;
    w.order = seq++;
    reg.emplace<UIWindow>(win, w);
    return win;
}
} // namespace

entt::entity spawnUiWindow(entt::registry& reg, int& seq, const std::string& title, const V2& px, const V3& pos) {
    return spawnUiCanvas(reg, seq, title, px, pos, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                              ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);
}

void spawnFpsHud(entt::registry& reg, int& seq, std::function<std::string()> text, const V3& pos, const V2& px) {
    const int flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                      ImGuiWindowFlags_NoBringToFrontOnFocus;
    const entt::entity win = spawnUiCanvas(reg, seq, "##fps", px, pos, flags);
    spawnWidget(reg, seq, win, UIText{"", std::move(text)});
}

// harmless fallback when the live component is gone (removed): writes land here, display reads zero
char s_dead[32];

void spawnFieldWidget(entt::registry& reg, int& seq, entt::entity parent, const char* label, const FieldDesc* f, const std::function<const void*(void)>& baseFn) {
    // re-resolved on every draw: entt component storage can move (pool reallocation) after capture
    auto pv = [baseFn, off = f->offset] {
        const void* b = baseFn();
        return b ? (char*)b + off : nullptr;
    };
    switch (f->kind) {
    case FieldKind::SliderF:
        spawnWidget(reg, seq, parent, UISliderF{label, f->lo, f->hi, "%.2f", [pv] {
            char* p = pv();
            return (float*)(p ? p : s_dead);
        }});
        break;
    case FieldKind::SliderI:
        spawnWidget(reg, seq, parent, UISliderI{label, (int)f->lo, (int)f->hi, [pv] {
            char* p = pv();
            return (int*)(p ? p : s_dead);
        }});
        break;
    case FieldKind::Checkbox:
        spawnWidget(reg, seq, parent, UICheckbox{label, [pv] {
            char* p = pv();
            return (bool*)(p ? p : s_dead);
        }});
        break;
    case FieldKind::Color:
        spawnWidget(reg, seq, parent, UIColorEdit{label, [pv] {
            char* p = pv();
            return (float*)(p ? p : s_dead);
        }});
        break;
    case FieldKind::DragV3:
        spawnWidget(reg, seq, parent, UIDragV3{label, 0.1f, [pv] {
            char* p = pv();
            return (float*)(p ? p : s_dead);
        }});
        break;
    case FieldKind::Ref: {
        const entt::registry* rp = &reg;
        spawnWidget(reg, seq, parent, UIText{label, [rp, pv] {
            char* p = pv();
            if (!p)
                return std::string("<gone>");
            const entt::entity target = *(const entt::entity*)p;
            const auto* n = rp->try_get<Name>(target);
            return n ? n->id : "<none>";
        }});
        break;
    }
    case FieldKind::Str:
        spawnWidget(reg, seq, parent, UIText{label, [pv] {
            char* p = pv();
            if (!p)
                return std::string("<gone>");
            return *(const std::string*)p;
        }});
        break;
    case FieldKind::FloatArr:
        spawnWidget(reg, seq, parent, UIText{label, [pv] {
            char* p = pv();
            if (!p)
                return std::string("<gone>");
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%zu elems", ((const std::vector<float>*)p)->size());
            return std::string(buf);
        }});
        break;
    case FieldKind::UIntArr:
        spawnWidget(reg, seq, parent, UIText{label, [pv] {
            char* p = pv();
            if (!p)
                return std::string("<gone>");
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%zu elems", ((const std::vector<uint32_t>*)p)->size());
            return std::string(buf);
        }});
        break;
    case FieldKind::EntArr:
        spawnWidget(reg, seq, parent, UIText{label, [pv] {
            char* p = pv();
            if (!p)
                return std::string("<gone>");
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%zu elems", ((const std::vector<entt::entity>*)p)->size());
            return std::string(buf);
        }});
        break;
    case FieldKind::Quat:
        break;
    }
}
