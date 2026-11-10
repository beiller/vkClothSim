-- the shared scene-ui builder: interprets a scene json "ui.widgets" spec (buildUi); the
-- widget vocabulary lives here, not in C++

function build(parent, spec)
    for _, w in ipairs(spec) do
        local t = w.type
        if t == "section" then
            local sec = ui.section(parent, w.label, w.collapsible)
            if w.children then
                build(sec, w.children)
            end
        elseif t == "slider-f" then
            ui.slider_f(parent, w.label, w.bind, w.lo, w.hi, w.fmt)
        elseif t == "slider-i" then
            ui.slider_i(parent, w.label, w.bind, w.lo, w.hi)
        elseif t == "checkbox" then
            ui.checkbox(parent, w.label, w.bind)
        elseif t == "color" then
            ui.color(parent, w.label, w.bind)
        elseif t == "drag-v3" then
            ui.drag_v3(parent, w.label, w.bind, w.speed)
        elseif t == "text" then
            ui.text(parent, w.label)
        elseif t == "button" then
            ui.button(parent, w.label, function()
                ui.fire_button(w.action)
            end)
        else
            error("unknown widget type '" .. t .. "'")
        end
    end
end

-- the per-scene main ui window: the engine calls this with the whole scene json;
-- a scene with no "ui" block (or ui.hidden) gets no window
function main(spec)
    local u = spec.ui
    if not u or u.hidden then return end
    local size = u.size or { 320, 720 }
    local win = ui.window(u.title or spec.name or "scene", size[1], size[2])
    if u.widgets then
        build(ui.tab(win, "scene"), u.widgets)
    end
    ui.editor(ui.tab(win, "editor"))
end

