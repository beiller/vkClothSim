-- the shared scene-ui builder: interprets a scene json "ui.widgets" spec (buildUi); the
-- widget vocabulary lives here, not in C++
local M = {}

local function build(parent, spec)
    for _, w in ipairs(spec) do
        local t = w.type
        if t == "section" then
            local sec = scene.ui_section(parent, w.label, w.collapsible)
            if w.children then
                build(sec, w.children)
            end
        elseif t == "slider-f" then
            scene.ui_slider_f(parent, w.label, w.bind, w.lo, w.hi, w.fmt)
        elseif t == "slider-i" then
            scene.ui_slider_i(parent, w.label, w.bind, w.lo, w.hi)
        elseif t == "checkbox" then
            scene.ui_checkbox(parent, w.label, w.bind)
        elseif t == "color" then
            scene.ui_color(parent, w.label, w.bind)
        elseif t == "drag-v3" then
            scene.ui_drag_v3(parent, w.label, w.bind, w.speed)
        elseif t == "text" then
            scene.ui_text(parent, w.label)
        elseif t == "button" then
            scene.ui_button(parent, w.label, function()
                scene.fire_button(w.action)
            end)
        else
            error("unknown widget type '" .. t .. "'")
        end
    end
end

-- the per-scene main ui window: the engine calls this with the whole scene json;
-- a scene with no "ui" block (or ui.hidden) gets no window
local function main(spec)
    local ui = spec.ui
    if not ui or ui.hidden then return end
    local size = ui.size or { 320, 720 }
    local win = scene.ui_window(ui.title or spec.name or "scene", 20, 20, size[1], size[2], true)
    if ui.widgets then
        build(scene.ui_tab(win, "scene"), ui.widgets)
    end
    scene.ui_editor(scene.ui_tab(win, "editor"))
end

M.build = build
M.main = main
return M
