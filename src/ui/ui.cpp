#include "ui.hpp"

#include <cstdio>
#include "reflect.hpp"
#include "url.hpp"
#include <algorithm>
#include <cstring>
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
std::function<void(const std::string&, entt::entity)> g_buttonHook;
std::function<void(const std::string&, entt::entity)> g_changeHook;
bool g_demoOpen = false;

void fireChange(const std::string& url, bool changed, entt::entity e) {
    if (changed && !url.empty() && g_changeHook)
        g_changeHook(url, e);
}

entt::entity canvasWindow(const entt::registry& reg, entt::entity canvas) {
    for (auto [e, w] : reg.view<UIWindow>().each())
        if (w.canvas == canvas)
            return e;
    return entt::null;
}

struct BindLink {
    UrlRef r;
    bool live = false;
    bool write = false;
    bool disabled = false;
};

bool bindResolve(const entt::registry& reg, const Bind* b, BindLink& l) {
    if (!b)
        return true;
    std::string err;
    if (!resolveUrl(reg, b->url, l.r, err))
        return false;
    l.live = static_cast<BindDir>(b->dir) != BindDir::Write;
    l.write = static_cast<BindDir>(b->dir) != BindDir::Read;
    l.disabled = static_cast<BindDir>(b->dir) == BindDir::Read;
    return true;
}

void bindSyncIn(const BindLink& l, void* value, std::size_t bytes) {
    if (l.live)
        std::memcpy(value, l.r.base, bytes);
}

void bindWriteBack(const BindLink& l, const void* value, bool changed, std::size_t bytes) {
    if (l.write && changed)
        std::memcpy(l.r.base, value, bytes);
}

void drawBoundText(const entt::registry& reg, const UrlRef& r) {
    const char* p = (const char*)r.base;
    char buf[32];
    switch (r.field->kind) {
    case FieldKind::Ref: {
        const entt::entity target = *(const entt::entity*)p;
        const auto* n = reg.try_get<Name>(target);
        ImGui::TextUnformatted(n ? n->id.c_str() : "<none>");
        return;
    }
    case FieldKind::Str:
        ImGui::TextUnformatted(((const std::string*)p)->c_str());
        return;
    case FieldKind::StrArr:
        std::snprintf(buf, sizeof buf, "%zu elems", ((const std::vector<std::string>*)p)->size());
        break;
    case FieldKind::FloatArr:
        std::snprintf(buf, sizeof buf, "%zu elems", ((const std::vector<float>*)p)->size());
        break;
    case FieldKind::UIntArr:
        std::snprintf(buf, sizeof buf, "%zu elems", ((const std::vector<uint32_t>*)p)->size());
        break;
    case FieldKind::EntArr:
        std::snprintf(buf, sizeof buf, "%zu elems", ((const std::vector<entt::entity>*)p)->size());
        break;
    case FieldKind::SliderF:
        std::snprintf(buf, sizeof buf, "%.2f", *(const float*)p);
        break;
    case FieldKind::SliderI:
        std::snprintf(buf, sizeof buf, "%d", *(const int*)p);
        break;
    case FieldKind::Checkbox:
        std::snprintf(buf, sizeof buf, "%s", *(const bool*)p ? "on" : "off");
        break;
    case FieldKind::Color:
    case FieldKind::DragV3: {
        const float* v = (const float*)p;
        std::snprintf(buf, sizeof buf, "%.2f %.2f %.2f", v[0], v[1], v[2]);
        break;
    }
    case FieldKind::Quat:
        return;
    }
    ImGui::TextUnformatted(buf);
}
} // namespace

void uiSetButtonHook(std::function<void(const std::string&, entt::entity)> fn) {
    g_buttonHook = std::move(fn);
}

void uiSetChangeHook(std::function<void(const std::string&, entt::entity)> fn) {
    g_changeHook = std::move(fn);
}

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

void uiFrame(entt::registry& reg, const std::vector<UiFeed>& feeds) {
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
        BindLink l;
        const bool draw = bindResolve(reg, reg.try_get<Bind>(e), l);
        if (draw) {
            if (auto* s = reg.try_get<UISliderF>(e)) {
                bindSyncIn(l, &s->value, sizeof s->value);
                if (l.disabled)
                    ImGui::BeginDisabled();
                const bool changed = ImGui::SliderFloat(s->label.c_str(), &s->value, s->lo, s->hi, s->fmt.c_str());
                if (l.disabled)
                    ImGui::EndDisabled();
                bindWriteBack(l, &s->value, changed, sizeof s->value);
                fireChange(s->change_url, changed, e);
            } else if (auto* s = reg.try_get<UISliderI>(e)) {
                bindSyncIn(l, &s->value, sizeof s->value);
                if (l.disabled)
                    ImGui::BeginDisabled();
                const bool changed = ImGui::SliderInt(s->label.c_str(), &s->value, s->lo, s->hi);
                if (l.disabled)
                    ImGui::EndDisabled();
                bindWriteBack(l, &s->value, changed, sizeof s->value);
                fireChange(s->change_url, changed, e);
            } else if (auto* c = reg.try_get<UICheckbox>(e)) {
                bindSyncIn(l, &c->value, sizeof c->value);
                if (l.disabled)
                    ImGui::BeginDisabled();
                const bool changed = ImGui::Checkbox(c->label.c_str(), &c->value);
                if (l.disabled)
                    ImGui::EndDisabled();
                bindWriteBack(l, &c->value, changed, sizeof c->value);
                fireChange(c->change_url, changed, e);
            } else if (auto* c = reg.try_get<UIColorEdit>(e)) {
                bindSyncIn(l, c->value, sizeof c->value);
                if (l.disabled)
                    ImGui::BeginDisabled();
                const bool changed = ImGui::ColorEdit3(c->label.c_str(), c->value);
                if (l.disabled)
                    ImGui::EndDisabled();
                bindWriteBack(l, c->value, changed, sizeof c->value);
                fireChange(c->change_url, changed, e);
            } else if (auto* d = reg.try_get<UIDragV3>(e)) {
                bindSyncIn(l, d->value, sizeof d->value);
                if (l.disabled)
                    ImGui::BeginDisabled();
                const bool changed = ImGui::DragFloat3(d->label.c_str(), d->value, d->speed);
                if (l.disabled)
                    ImGui::EndDisabled();
                bindWriteBack(l, d->value, changed, sizeof d->value);
                fireChange(d->change_url, changed, e);
            } else if (auto* t = reg.try_get<UIText>(e)) {
                if (reg.try_get<Bind>(e))
                    drawBoundText(reg, l.r);
                else
                    ImGui::TextUnformatted(t->label.c_str());
            } else if (auto* b = reg.try_get<UIButton>(e)) {
                if (ImGui::Button(b->label.c_str()) && g_buttonHook)
                    g_buttonHook(b->target_url, e);
                reg.get<UIButton>(e).pressed = ImGui::IsItemActive();
            } else if (auto* sec = reg.try_get<UISection>(e)) {
                if (sec->collapsible) {
                    if (ImGui::CollapsingHeader(sec->label.c_str()))
                        drawKids(e);
                } else {
                    ImGui::SeparatorText(sec->label.c_str());
                    drawKids(e);
                }
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

void spawnFieldWidget(entt::registry& reg, int& seq, entt::entity parent, const char* label, const FieldDesc* f,
                      const std::string& url) {
    auto withBind = [&reg, &url](entt::entity e, BindDir dir = BindDir::Both) {
        reg.emplace<Bind>(e, Bind{url, "", static_cast<int>(dir)});
    };
    switch (f->kind) {
    case FieldKind::SliderF:
        withBind(spawnWidget(reg, seq, parent, UISliderF{label, 0.0f, f->lo, f->hi}));
        break;
    case FieldKind::SliderI:
        withBind(spawnWidget(reg, seq, parent, UISliderI{label, 0, (int)f->lo, (int)f->hi}));
        break;
    case FieldKind::Checkbox:
        withBind(spawnWidget(reg, seq, parent, UICheckbox{label, false}));
        break;
    case FieldKind::Color:
        withBind(spawnWidget(reg, seq, parent, UIColorEdit{label}));
        break;
    case FieldKind::DragV3:
        withBind(spawnWidget(reg, seq, parent, UIDragV3{label}));
        break;
    case FieldKind::Ref:
    case FieldKind::Str:
    case FieldKind::StrArr:
    case FieldKind::FloatArr:
    case FieldKind::UIntArr:
    case FieldKind::EntArr:
        withBind(spawnWidget(reg, seq, parent, UIText{}), BindDir::Read);
        break;
    case FieldKind::Quat:
        break;
    }
}
