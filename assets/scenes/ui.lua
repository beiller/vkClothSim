-- the shared scene-ui builder: interprets a scene json "ui.widgets" spec (buildUi); the
-- widget vocabulary lives here, not in C++. every widget is plain components:
-- a UIWidget marker + Parent + one widget component (+ Bind for bound values).

local M_PER_PX = 0.0022   -- uniform m/px, preserves aspect
local WIN_FLAGS = 294     -- ImGui NoSavedSettings | NoMove | NoCollapse | NoResize

-- a UI canvas window: canvas (GPU texture + quad + collider) plus the window that fills it;
-- pos is the canvas world position — required, there is no default placement
function make_window(title, w, h, pos)
    if not pos then error("make_window: pos {x,y,z} is required") end
    local ws = M_PER_PX
    local canvas = scene.entity()
    scene.replace(canvas, "Transform", { pos = pos })
    scene.emplace(canvas, "UICanvas", { w = w * ws, h = h * ws })
    scene.emplace(canvas, "GPUTexture", { w = w, h = h })
    local geom = scene.entity()
    scene.replace(geom, "Geometry", mesh.quad({ w = w * ws, h = h * ws }))
    local shape = scene.entity()
    scene.replace(shape, "BoxShape", { half = { w * ws / 2, h * ws / 2, 0.01 } })
    local col = scene.entity()
    scene.replace(col, "Collider", { shape = shape })
    scene.replace(canvas, "Renderable", { geom = geom, noShadow = true })
    local matEnt = scene.entity()
    scene.replace(matEnt, "Material", { tex = canvas, alphaMode = 2 })
    scene.replace(canvas, "MaterialRef", { material = matEnt })
    scene.replace(canvas, "KinematicBody", { colliders = { col } })
    local win = scene.entity()
    scene.emplace(win, "UIWindow", { title = title, canvas = canvas, w = w, h = h, flags = WIN_FLAGS, order = ui.seq() })
    return win
end

-- one widget entity: UIWidget marker + Parent + the component with its fields (+ Bind when bound)
local function widget(parent, comp, fields, bind)
    local e = scene.entity()
    scene.replace(e, "Parent", { e = parent })
    scene.emplace(e, "UIWidget", { order = ui.seq() })
    scene.emplace(e, comp, fields)
    if bind then
        scene.emplace(e, "Bind", { url = bind })
    end
    return e
end

-- convenience for direct use in scene scripts (the same components as build())
function tab(win, label)
    return widget(win, "UITab", { label = label })
end

function text(parent, label)
    return widget(parent, "UIText", { label = label })
end

-- "name.Component.field" (names may contain dots: the last two segments are Component.field)
local function parseBind(b)
    local field = b:match("%.[^%.]+$"):sub(2)
    local rest = b:sub(1, #b - #field - 1)
    local comp = rest:match("[^%.]+$")
    return scene.get_entity(rest:sub(1, #rest - #comp - 1)), comp, field
end

-- the field's own lo/hi (nil for script vars: they expect explicit lo/hi)
local function fieldRange(comp, field)
    if comp == "LuaScript" then
        return nil, nil
    end
    for _, f in ipairs(scene.fields(comp)) do
        if f.name == field then
            return f.lo, f.hi
        end
    end
    error("unknown field " .. comp .. "." .. field)
end

-- a float slider bound to "name.Component.field" (or a script var): initial value from the
-- field; lo/hi default to the field's own range; change = an optional "name.LuaScript.fn" callback
function bind_slider(parent, label, bind, lo, hi, fmt, change)
    local ent, comp, field = parseBind(bind)
    local flo, fhi = fieldRange(comp, field)
    local fields = { label = label, value = scene.get(bind), lo = lo or flo or 0.0, hi = hi or fhi or 1.0, fmt = fmt or "%.2f" }
    if change then
        fields.change_url = change
    end
    return widget(parent, "UISliderF", fields, bind)
end

local VALUE_WIDGETS = { ["slider-i"] = "UISliderI", checkbox = "UICheckbox", color = "UIColorEdit", ["drag-v3"] = "UIDragV3" }

function build(parent, spec)
    for _, w in ipairs(spec) do
        local t = w.type
        if t == "section" then
            local sec = widget(parent, "UISection", { label = w.label, collapsible = (w.collapsible == nil) and true or w.collapsible })
            if w.children then
                build(sec, w.children)
            end
        elseif t == "slider-f" then
            bind_slider(parent, w.label, w.bind, w.lo, w.hi, w.fmt, w.change)
        elseif VALUE_WIDGETS[t] then
            local ent, comp, field = parseBind(w.bind)
            local flo, fhi = fieldRange(comp, field)
            local fields = { label = w.label, value = scene.get(w.bind) }
            if t == "slider-i" then
                fields.lo = w.lo or flo or 0
                fields.hi = w.hi or fhi or 1
            elseif t == "drag-v3" then
                fields.speed = w.speed or 0.1
            end
            if w.change then
                fields.change_url = w.change
            end
            widget(parent, VALUE_WIDGETS[t], fields, w.bind)
        elseif t == "text" then
            text(parent, w.label)
        elseif t == "button" then
            widget(parent, "UIButton", { label = w.label, target_url = w.action })
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
    local win = make_window(u.title or spec.name or "scene", size[1], size[2], u.pos or { 0, 1.5, 2.8 })
    if u.widgets then
        build(tab(win, "scene"), u.widgets)
    end
    ui.editor(tab(win, "editor"))
end
